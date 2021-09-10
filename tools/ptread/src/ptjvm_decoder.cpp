/* This is a Intel PT decoder for Java Virtual Machine
 * It outputs the bytecode-level execution
 * both in Interpretation mode and in JIT mode
 */
#include <limits.h>
#include <intel-pt.h>
#include <stdint.h>
#include <asm/types.h>
#include <errno.h>
#include <stdlib.h>
#include <inttypes.h>
#include <map>
#include "../include/pevent.hpp"
#include "../include/ptjvm_decoder.hpp"
#include "../include/load_file.hpp"

#define PERF_RECORD_AUXTRACE 71
#define PERF_RECORD_AUX_ADVANCE 72

struct attr_config {
  struct pt_cpu cpu;
  int nr_cpus;
  uint8_t mtc_freq;
  uint8_t nom_freq;
  uint32_t cpuid_0x15_eax;
  uint32_t cpuid_0x15_ebx;
  uint64_t sample_type;
  uint16_t time_shift;
  uint32_t time_mult;
  uint64_t time_zero;
  uint64_t addr0_a;
  uint64_t addr0_b;
};

static attr_config attr;

struct aux_advance_event {
  __u32 cpu;
  __u32 tid;
};

/* The decoder to use. */
struct ptjvm_decoder {
  struct pt_packet_decoder *pkt;

  /* to indicate data loss */
  bool loss;

  /* A collection of decoder-specific flags. */
  struct pt_conf_flags flags;

  /* The current address space. */
  struct pt_asid asid;

  /* pt config */
  struct pt_config config;

};

/* Supported address range configurations. */
enum pt_addr_cfg {
  pt_addr_cfg_disabled = 0,
  pt_addr_cfg_filter = 1,
  pt_addr_cfg_stop = 2
};

struct auxtrace_event {
  __u64 size;
  __u64 offset;
  __u64 reference;
  __u32 idx;
  __u32 tid;
  __u32 cpu;
  __u32 reservered__; /* for alignment */
};

static int ptjvm_init_decoder(struct ptjvm_decoder *decoder) {
  if (!decoder)
    return -pte_internal;

  memset(decoder, 0, sizeof(*decoder));

  return 0;
}

static uint64_t ppt_invalid_offset = 0;
static uint64_t ppt_unknown_offset = 0;
static uint64_t ppt_pad_offset = 0;
static uint64_t ppt_psb_offset = 0;
static uint64_t ppt_psbend_offset = 0;
static uint64_t ppt_fup_offset = 0;
static uint64_t ppt_tip_offset = 0;
static uint64_t ppt_tip_pge_offset = 0;
static uint64_t ppt_tip_pgd_offset = 0;
static uint64_t ppt_tnt_8_offset = 0;
static uint64_t ppt_tnt_64_offset = 0;
static uint64_t ppt_mode_offset = 0;
static uint64_t ppt_pip_offset = 0;
static uint64_t ppt_vmcs_offset = 0;
static uint64_t ppt_cbr_offset = 0;
static uint64_t ppt_tsc_offset = 0;
static uint64_t ppt_tma_offset = 0;
static uint64_t ppt_mtc_offset = 0;
static uint64_t ppt_cyc_offset = 0;
static uint64_t ppt_stop_offset = 0;
static uint64_t ppt_ovf_offset = 0;
static uint64_t ppt_mnt_offset = 0;
static uint64_t ppt_exstop_offset = 0;
static uint64_t ppt_mwait_offset = 0;
static uint64_t ppt_pwre_offset = 0;
static uint64_t ppt_pwrx_offset = 0;
static uint64_t ppt_ptw_offset = 0;

static uint64_t sext(uint64_t val, uint8_t sign)
{
  uint64_t signbit, mask;

  signbit = 1ull << (sign - 1);
  mask = ~0ull << sign;

  return val & signbit ? val | mask : val & ~mask;
}

static int print_ip_payload(uint64_t offset, struct pt_packet_ip &packet)
{
  switch (packet.ipc) {
  case pt_ipc_suppressed:
    printf("%x: ????????????????", pt_ipc_suppressed);
    return 0;

  case pt_ipc_update_16:
    printf("%x: ????????????%04"
            PRIx64, pt_ipc_update_16, packet.ip);
    return 0;

  case pt_ipc_update_32:
    printf("%x: ????????%08"
          PRIx64, pt_ipc_update_32, packet.ip);
    return 0;

  case pt_ipc_update_48:
    printf("%x: ????%012"
          PRIx64, pt_ipc_update_48, packet.ip);
    return 0;

  case pt_ipc_sext_48:
    printf("%x: %016" PRIx64,
          pt_ipc_sext_48, sext(packet.ip, 48));
    return 0;

  case pt_ipc_full:
    printf("%x: %016" PRIx64,
          pt_ipc_full, packet.ip);
    return 0;
  }

  printf("%x: %016" PRIx64,
        packet.ipc, packet.ip);
  fprintf(stderr, "bad ipc %lx %d", offset, -pte_bad_packet);
  return -1;
}

static int print_tnt_payload(uint64_t offset, struct pt_packet_tnt &packet)
{
  uint64_t tnt;
  uint8_t bits;

  bits = packet.bit_size;
  tnt = packet.payload;

  for (; bits > 0; --bits)
    printf("%c", tnt & (1ull << (bits - 1)) ? '!' : '.');

  return 0;
}

static int decode(ptjvm_decoder *decoder,
                  const char *prog, bool dump) {
  if (!decoder || !decoder->pkt) {
    return -pte_internal;
  }

  struct pt_packet_decoder *pkt = decoder->pkt;
  int status, taken, errcode;
  uint64_t offset1, offset2;
  for (;;) {
    status = pt_pkt_sync_forward(pkt);
    if (status < 0) {
      if (status == -pte_eos) {
        break;
      }
      fprintf(stderr, "%s: decode sync error %s.\n", prog,
              pt_errstr(pt_errcode(status)));
      return status;
    }
    pt_pkt_get_offset(pkt, &offset1);
    for (;;) {
      struct pt_packet packet;
      size_t size = sizeof(packet);

      status = pt_pkt_next(pkt, &packet, size);
      if (status < 0) {
        break;
      }

      pt_pkt_get_offset(pkt, &offset2);

      switch(packet.type) {
        default:
          fprintf(stderr, "%s: unknown packet %lx.\n", prog, offset1);
          break;
        case ppt_invalid:
          if(dump)
            printf("<unknown>\n");
          ppt_invalid_offset += (offset2-offset1);
          break;
        case ppt_unknown:
          if (dump)
            printf("<invalid>\n");
          ppt_unknown_offset += (offset2-offset1);
          break;
        case ppt_pad:
          if (dump)
            printf("pad\n");
          ppt_pad_offset += (offset2-offset1);
          break;
        case ppt_psb:
          if (dump)
            printf("psb\n");
          ppt_psb_offset += (offset2-offset1);
          break;
        case ppt_psbend:
          if (dump)
            printf("pabend\n");
          ppt_psbend_offset += (offset2-offset1);
          break;
        case ppt_fup:
          if (dump) {
            printf("fup ");
            print_ip_payload(offset1, packet.payload.ip);
            printf("\n");
          }
          ppt_fup_offset += (offset2-offset1);
          break;
        case ppt_tip:
          if (dump) {
            printf("tip ");
            print_ip_payload(offset1, packet.payload.ip);
            printf("\n");
          }
          ppt_tip_offset += (offset2-offset1);
          break;
        case ppt_tip_pge:
          if (dump) {
            printf("tip.pge ");
            print_ip_payload(offset1, packet.payload.ip);
            printf("\n");
          }
          ppt_tip_pge_offset += (offset2-offset1);
          break;
        case ppt_tip_pgd:
          if (dump) {
            printf("tip.pgd ");
            print_ip_payload(offset1, packet.payload.ip);
            printf("\n");
          }
          ppt_tip_pgd_offset += (offset2-offset1);
          break;
        case ppt_tnt_8:
          if (dump) {
            printf("tnt.8 ");
            print_tnt_payload(offset1, packet.payload.tnt);
            printf("\n");
          }
          ppt_tnt_8_offset += (offset2-offset1);
          break;
        case ppt_tnt_64:
          if (dump) {
            printf("tnt.64 ");
            print_tnt_payload(offset1, packet.payload.tnt);
            printf("\n");
          }
          ppt_tnt_64_offset += (offset2-offset1);
          break;
        case ppt_mode: {
          if (dump) {
            struct pt_packet_mode &mode = packet.payload.mode;
            switch (mode.leaf) {
              case pt_mol_exec: {
                const char *csd, *csl, *sep;
                csd = mode.bits.exec.csd ? "cs.d" : "";
                csl = mode.bits.exec.csl ? "cs.l" : "";
                sep = csd[0] && csl[0] ? ", " : "";
                printf("mode.exec ");
                printf("%s%s%s\n", csd, sep, csl);
                break;
              }
              case pt_mol_tsx: {
                const char *intx, *abrt, *sep;
                intx = mode.bits.tsx.intx ? "intx" : "";
                abrt = mode.bits.tsx.abrt ? "abrt" : "";
                sep = intx[0] && abrt[0] ? ", " : "";
                printf("mode.tsx ");
                printf("%s%s%s\n",intx, sep, abrt);
                break;
              }
              default:
                printf("mode.unknown ");
                printf("leaf: %x\n", mode.leaf);
                break;
            }
          }
          ppt_mode_offset += (offset2-offset1);
          break;
        }
        case ppt_pip:
          if (dump) {
            printf("pip ");
            printf("%" PRIx64 "%s ", packet.payload.pip.cr3, packet.payload.pip.nr ? ", nr" : "");
            printf("cr3 ");
            printf("%016" PRIx64 "\n", packet.payload.pip.cr3);
          }
          ppt_pip_offset += (offset2-offset1);
          break;
        case ppt_vmcs:
          if (dump) {
            printf("vmcs ");
            printf("%" PRIx64" ", packet.payload.vmcs.base);
            printf("vmcs ");
            printf("%016" PRIx64 "\n", packet.payload.vmcs.base);
          }
          ppt_vmcs_offset += (offset2-offset1);
          break;
        case ppt_cbr:
          if (dump) {
            printf("cbr ");
            printf("%x\n", packet.payload.cbr.ratio);
          }
          ppt_cbr_offset += (offset2-offset1);
          break;
        case ppt_tsc:
          if (dump) {
            printf("tsc ");
            printf("%" PRIx64 "\n", packet.payload.tsc.tsc);
          }
          ppt_tsc_offset += (offset2-offset1);
          break;
        case ppt_tma:
          if (dump) {
            printf("tma ");
            printf("%x, %x\n", packet.payload.tma.ctc, packet.payload.tma.fc);
          }
          ppt_tma_offset += (offset2-offset1);
          break;
        case ppt_mtc:
          if (dump) {
            printf("mtc ");
            printf("%x\n", packet.payload.mtc.ctc);
          }
          ppt_mtc_offset += (offset2-offset1);
          break;
        case ppt_cyc:
          if (dump) {
            printf("cyc ");
            printf("%" PRIx64"\n", packet.payload.cyc.value);
          }
          ppt_cyc_offset += (offset2-offset1);
          break;
        case ppt_stop:
          if (dump)
            printf("stop\n");
          ppt_stop_offset += (offset2-offset1);
          break;
        case ppt_ovf:
          if (dump)
            printf("ovf\n");
          ppt_ovf_offset += (offset2-offset1);
          break;
        case ppt_mnt:
          if (dump) {
            printf("mnt ");
            printf("%" PRIx64"\n", packet.payload.mnt.payload);
          }
          ppt_mnt_offset += (offset2-offset1);
          break;
        case ppt_exstop:
          if (dump) {
            printf("exstop ");
            printf("%s\n", packet.payload.exstop.ip ? "ip" : "");
          }
          ppt_exstop_offset += (offset2-offset1);
          break;
        case ppt_mwait:
          if (dump) {
            printf("mwait ");
            printf("%08x, %08x\n", packet.payload.mwait.hints, packet.payload.mwait.ext);
          }
          ppt_mwait_offset += (offset2-offset1);
          break;
        case ppt_pwre:
          if (dump) {
            printf("pwre ");
            printf("c%u.%u%s\n", (packet.payload.pwre.state + 1) & 0xf, 
                  (packet.payload.pwre.sub_state + 1) & 0xf, packet.payload.pwre.hw ? ", hw" : "");
          }
          ppt_pwre_offset += (offset2-offset1);
          break;
        case ppt_pwrx:
          if (dump) {
            const char *wr;
            printf("pwrx ");
            printf("c%u, c%u\n", (packet.payload.pwrx.last + 1) & 0xf,
                    (packet.payload.pwrx.deepest + 1) & 0xf);
          }
          ppt_pwrx_offset += (offset2-offset1);
          break;
        case ppt_ptw:
          if (dump) {
            printf("ptw");
            printf("%x: %" PRIx64 "%s\n", packet.payload.ptw.plc, packet.payload.ptw.payload,
                    packet.payload.ptw.ip ? ", ip" : "");
          }
          ppt_ptw_offset += (offset2-offset1);
          break;
      }

      offset1 = offset2;
    }

    /* We're done when we reach the end of the trace stream. */
    if (status == -pte_eos)
      break;
    else {
      fprintf(stderr, "%s: decode error %s.\n", prog, pt_errstr(pt_errcode(status)));
    }
  }
  return status;
}

static int alloc_decoder(struct ptjvm_decoder *decoder,
                         const struct pt_config *conf, const char *prog) {
  struct pt_config config;
  int errcode;

  if (!decoder || !conf || !prog)
    return -pte_internal;

  config = *conf;
  config.flags = decoder->flags;
  decoder->config = *conf;

  decoder->pkt = pt_pkt_alloc_decoder(&config);
  if (!decoder->pkt) {
    fprintf(stderr, "%s: failed to create query decoder.\n", prog);
    return -pte_nomem;
  }

  return 0;
}

static void free_decoder(struct ptjvm_decoder *decoder) {
  if (!decoder)
    return;

  if (decoder->pkt)
    pt_pkt_free_decoder(decoder->pkt);
}

static int get_arg_uint64(uint64_t *value, const char *option, const char *arg,
                          const char *prog) {
  char *rest;

  if (!value || !option || !prog) {
    fprintf(stderr, "%s: internal error.\n", prog ? prog : "?");
    return 0;
  }

  if (!arg || arg[0] == 0 || (arg[0] == '-' && arg[1] == '-')) {
    fprintf(stderr, "%s: %s: missing argument.\n", prog, option);
    return 0;
  }

  errno = 0;
  *value = strtoull(arg, &rest, 0);
  if (errno || *rest) {
    fprintf(stderr, "%s: %s: bad argument: %s.\n", prog, option, arg);
    return 0;
  }

  return 1;
}

static int get_arg_uint32(uint32_t *value, const char *option, const char *arg,
                          const char *prog) {
  uint64_t val;

  if (!get_arg_uint64(&val, option, arg, prog))
    return 0;

  if (val > UINT32_MAX) {
    fprintf(stderr, "%s: %s: value too big: %s.\n", prog, option, arg);
    return 0;
  }

  *value = (uint32_t)val;

  return 1;
}

static int get_arg_uint16(uint16_t *value, const char *option, const char *arg,
                          const char *prog) {
  uint64_t val;

  if (!get_arg_uint64(&val, option, arg, prog))
    return 0;

  if (val > UINT16_MAX) {
    fprintf(stderr, "%s: %s: value too big: %s.\n", prog, option, arg);
    return 0;
  }

  *value = (uint16_t)val;

  return 1;
}

static int get_arg_uint8(uint8_t *value, const char *option, const char *arg,
                         const char *prog) {
  uint64_t val;

  if (!get_arg_uint64(&val, option, arg, prog))
    return 0;

  if (val > UINT8_MAX) {
    fprintf(stderr, "%s: %s: value too big: %s.\n", prog, option, arg);
    return 0;
  }

  *value = (uint8_t)val;

  return 1;
}

static int pt_cpu_parse(struct pt_cpu *cpu, const char *s) {
  const char sep = '/';
  char *endptr;
  long family, model, stepping;

  if (!cpu || !s)
    return -pte_invalid;

  family = strtol(s, &endptr, 0);
  if (s == endptr || *endptr == '\0' || *endptr != sep)
    return -pte_invalid;

  if (family < 0 || family > USHRT_MAX)
    return -pte_invalid;

  /* skip separator */
  s = endptr + 1;

  model = strtol(s, &endptr, 0);
  if (s == endptr || (*endptr != '\0' && *endptr != sep))
    return -pte_invalid;

  if (model < 0 || model > UCHAR_MAX)
    return -pte_invalid;

  if (*endptr == '\0')
    /* stepping was omitted, it defaults to 0 */
    stepping = 0;
  else {
    /* skip separator */
    s = endptr + 1;

    stepping = strtol(s, &endptr, 0);
    if (*endptr != '\0')
      return -pte_invalid;

    if (stepping < 0 || stepping > UCHAR_MAX)
      return -pte_invalid;
  }

  cpu->vendor = pcv_intel;
  cpu->family = (uint16_t)family;
  cpu->model = (uint8_t)model;
  cpu->stepping = (uint8_t)stepping;

  return 0;
}

int ptjvm_decode(TracePart tracepart, bool dump) {
  struct ptjvm_decoder decoder;
  struct pt_config config;
  int errcode;
  size_t off;
  uint64_t sum_offset;
  const char *prog = "PacketDump";

  ppt_invalid_offset = 0;
  ppt_unknown_offset = 0;
  ppt_pad_offset = 0;
  ppt_psb_offset = 0;
  ppt_psbend_offset = 0;
  ppt_fup_offset = 0;
  ppt_tip_offset = 0;
  ppt_tip_pge_offset = 0;
  ppt_tip_pgd_offset = 0;
  ppt_tnt_8_offset = 0;
  ppt_tnt_64_offset = 0;
  ppt_mode_offset = 0;
  ppt_pip_offset = 0;
  ppt_vmcs_offset = 0;
  ppt_cbr_offset = 0;
  ppt_tsc_offset = 0;
  ppt_tma_offset = 0;
  ppt_mtc_offset = 0;
  ppt_cyc_offset = 0;
  ppt_stop_offset = 0;
  ppt_ovf_offset = 0;
  ppt_mnt_offset = 0;
  ppt_exstop_offset = 0;
  ppt_mwait_offset = 0;
  ppt_pwre_offset = 0;
  ppt_pwrx_offset = 0;
  ppt_ptw_offset = 0;

  if (!tracepart.pt_size)
    return 0;

  pt_config_init(&config);
  errcode = ptjvm_init_decoder(&decoder);
  if (errcode < 0) {
    fprintf(stderr, "%s: Fail initializing decoder: %s.\n", prog,
            pt_errstr(pt_errcode(errcode)));
    goto err;
  }

  config.cpu = attr.cpu;
  config.mtc_freq = attr.mtc_freq;
  config.nom_freq = attr.nom_freq;
  config.cpuid_0x15_eax = attr.cpuid_0x15_eax;
  config.cpuid_0x15_ebx = attr.cpuid_0x15_ebx;
  config.addr_filter.config.addr_cfg = pt_addr_cfg_filter;
  config.addr_filter.addr0_a = attr.addr0_a;
  config.addr_filter.addr0_b = attr.addr0_b;
  config.flags.variant.query.keep_tcal_on_ovf = 1;
  config.begin = tracepart.pt_buffer;
  config.end = tracepart.pt_buffer + tracepart.pt_size;

  decoder.loss = tracepart.loss;

  if (config.cpu.vendor) {
    errcode = pt_cpu_errata(&config.errata, &config.cpu);
    if (errcode < 0) {
      fprintf(stderr, "%s: [0, 0: config error: %s]\n", prog,
              pt_errstr(pt_errcode(errcode)));
      goto err;
    }
  }

  errcode = alloc_decoder(&decoder, &config, prog);
  if (errcode < 0) {
    fprintf(stderr, "%s: fail to allocate decoder.\n", prog);
    goto err;
  }

  decode(&decoder, prog, dump);

  sum_offset = ppt_invalid_offset + ppt_unknown_offset + ppt_pad_offset +
                ppt_psb_offset + ppt_psbend_offset + ppt_fup_offset +
                ppt_tip_offset + ppt_tip_pge_offset + ppt_tip_pgd_offset +
                ppt_tnt_8_offset + ppt_tnt_64_offset + ppt_mode_offset +
                ppt_pip_offset + ppt_vmcs_offset + ppt_cbr_offset +
                ppt_tsc_offset + ppt_tma_offset + ppt_mtc_offset +
                ppt_cyc_offset + ppt_stop_offset + ppt_ovf_offset +
                ppt_mnt_offset + ppt_exstop_offset + ppt_mwait_offset +
                ppt_pwre_offset + ppt_pwrx_offset + ppt_ptw_offset;
  if (dump) {
  printf("*************Percentage statistics:\n");
  printf("SUM: %ld\n", sum_offset);
  printf("invalid: %ld %lf%%\n", ppt_invalid_offset, (double)ppt_invalid_offset/(double)sum_offset);
  printf("unknown: %ld %lf%%\n", ppt_unknown_offset, (double)ppt_unknown_offset/(double)sum_offset);
  printf("pad: %ld %lf%%\n", ppt_pad_offset, (double)ppt_pad_offset/(double)sum_offset);
  printf("psb: %ld %lf%%\n", ppt_psb_offset, (double)ppt_psb_offset/(double)sum_offset);
  printf("psbend: %ld %lf%%\n", ppt_psbend_offset, (double)ppt_psbend_offset/(double)sum_offset);
  printf("fup: %ld %lf%%\n", ppt_fup_offset, (double)ppt_fup_offset/(double)sum_offset);
  printf("tip: %ld %lf%%\n", ppt_tip_offset, (double)ppt_tip_offset/(double)sum_offset);
  printf("tip.pge: %ld %lf%%\n", ppt_tip_pge_offset, (double)ppt_tip_pge_offset/(double)sum_offset);
  printf("tip.pgd: %ld %lf%%\n", ppt_tip_pgd_offset, (double)ppt_tip_pgd_offset/(double)sum_offset);
  printf("tnt.8: %ld %lf%%\n", ppt_tnt_8_offset, (double)ppt_tnt_8_offset/(double)sum_offset);
  printf("tnt.64: %ld %lf\n", ppt_tnt_64_offset, (double)ppt_tnt_64_offset/(double)sum_offset);
  printf("mode: %ld %lf\n", ppt_mode_offset, (double)ppt_mode_offset/(double)sum_offset);
  printf("pip: %ld %lf\n", ppt_pip_offset, (double)ppt_pip_offset/(double)sum_offset);
  printf("vmcs: %ld %lf\n", ppt_vmcs_offset, (double)ppt_vmcs_offset/(double)sum_offset);
  printf("cbr: %ld %lf\n", ppt_cbr_offset, (double)ppt_cbr_offset/(double)sum_offset);
  printf("tsc: %ld %lf\n", ppt_tsc_offset, (double)ppt_tsc_offset/(double)sum_offset);
  printf("tma: %ld %lf\n", ppt_tma_offset, (double)ppt_tma_offset/(double)sum_offset);
  printf("mtc: %ld %lf\n", ppt_mtc_offset, (double)ppt_mtc_offset/(double)sum_offset);
  printf("cyc: %ld %lf\n", ppt_cyc_offset, (double)ppt_cyc_offset/(double)sum_offset);
  printf("stop: %ld %lf\n", ppt_stop_offset, (double)ppt_stop_offset/(double)sum_offset);
  printf("ovf: %ld %lf\n", ppt_ovf_offset, (double)ppt_ovf_offset/(double)sum_offset);
  printf("mnt: %ld %lf\n", ppt_mnt_offset, (double)ppt_mnt_offset/(double)sum_offset);
  printf("exstop: %ld %lf\n", ppt_exstop_offset, (double)ppt_exstop_offset/(double)sum_offset);
  printf("mwait: %ld %lf\n", ppt_mwait_offset, (double)ppt_mwait_offset/(double)sum_offset);
  printf("pwre: %ld %lf\n", ppt_pwre_offset, (double)ppt_pwre_offset/(double)sum_offset);
  printf("pwrx: %ld %lf\n", ppt_pwrx_offset, (double)ppt_pwrx_offset/(double)sum_offset);
  printf("ptw: %ld %lf\n", ppt_ptw_offset, (double)ppt_ptw_offset/(double)sum_offset);
  printf("*****************\n\n");
  }

  fprintf(stderr, "*************Percentage statistics:\n");
  fprintf(stderr, "SUM: %ld\n", sum_offset);
  fprintf(stderr, "invalid: %ld %lf%%\n", ppt_invalid_offset, (double)ppt_invalid_offset/(double)sum_offset);
  fprintf(stderr, "unknown: %ld %lf%%\n", ppt_unknown_offset, (double)ppt_unknown_offset/(double)sum_offset);
  fprintf(stderr, "pad: %ld %lf%%\n", ppt_pad_offset, (double)ppt_pad_offset/(double)sum_offset);
  fprintf(stderr, "psb: %ld %lf%%\n", ppt_psb_offset, (double)ppt_psb_offset/(double)sum_offset);
  fprintf(stderr, "psbend: %ld %lf%%\n", ppt_psbend_offset, (double)ppt_psbend_offset/(double)sum_offset);
  fprintf(stderr, "fup: %ld %lf%%\n", ppt_fup_offset, (double)ppt_fup_offset/(double)sum_offset);
  fprintf(stderr, "tip: %ld %lf%%\n", ppt_tip_offset, (double)ppt_tip_offset/(double)sum_offset);
  fprintf(stderr, "tip.pge: %ld %lf%%\n", ppt_tip_pge_offset, (double)ppt_tip_pge_offset/(double)sum_offset);
  fprintf(stderr, "tip.pgd: %ld %lf%%\n", ppt_tip_pgd_offset, (double)ppt_tip_pgd_offset/(double)sum_offset);
  fprintf(stderr, "tnt.8: %ld %lf%%\n", ppt_tnt_8_offset, (double)ppt_tnt_8_offset/(double)sum_offset);
  fprintf(stderr, "tnt.64: %ld %lf\n", ppt_tnt_64_offset, (double)ppt_tnt_64_offset/(double)sum_offset);
  fprintf(stderr, "mode: %ld %lf\n", ppt_mode_offset, (double)ppt_mode_offset/(double)sum_offset);
  fprintf(stderr, "pip: %ld %lf\n", ppt_pip_offset, (double)ppt_pip_offset/(double)sum_offset);
  fprintf(stderr, "vmcs: %ld %lf\n", ppt_vmcs_offset, (double)ppt_vmcs_offset/(double)sum_offset);
  fprintf(stderr, "cbr: %ld %lf\n", ppt_cbr_offset, (double)ppt_cbr_offset/(double)sum_offset);
  fprintf(stderr, "tsc: %ld %lf\n", ppt_tsc_offset, (double)ppt_tsc_offset/(double)sum_offset);
  fprintf(stderr, "tma: %ld %lf\n", ppt_tma_offset, (double)ppt_tma_offset/(double)sum_offset);
  fprintf(stderr, "mtc: %ld %lf\n", ppt_mtc_offset, (double)ppt_mtc_offset/(double)sum_offset);
  fprintf(stderr, "cyc: %ld %lf\n", ppt_cyc_offset, (double)ppt_cyc_offset/(double)sum_offset);
  fprintf(stderr, "stop: %ld %lf\n", ppt_stop_offset, (double)ppt_stop_offset/(double)sum_offset);
  fprintf(stderr, "ovf: %ld %lf\n", ppt_ovf_offset, (double)ppt_ovf_offset/(double)sum_offset);
  fprintf(stderr, "mnt: %ld %lf\n", ppt_mnt_offset, (double)ppt_mnt_offset/(double)sum_offset);
  fprintf(stderr, "exstop: %ld %lf\n", ppt_exstop_offset, (double)ppt_exstop_offset/(double)sum_offset);
  fprintf(stderr, "mwait: %ld %lf\n", ppt_mwait_offset, (double)ppt_mwait_offset/(double)sum_offset);
  fprintf(stderr, "pwre: %ld %lf\n", ppt_pwre_offset, (double)ppt_pwre_offset/(double)sum_offset);
  fprintf(stderr, "pwrx: %ld %lf\n", ppt_pwrx_offset, (double)ppt_pwrx_offset/(double)sum_offset);
  fprintf(stderr, "ptw: %ld %lf\n", ppt_ptw_offset, (double)ppt_ptw_offset/(double)sum_offset);
  fprintf(stderr, "*****************\n\n");
  free_decoder(&decoder);
  return 0;

err:
  free_decoder(&decoder);
  return -1;
}

static size_t get_sample_size(uint64_t sample_type, bool &has_cpu,
                              size_t &cpu_off) {
  size_t size = 0;
  if (sample_type & PERF_SAMPLE_TID) {
    size += 8;
  }

  if (sample_type & PERF_SAMPLE_TIME) {
    size += 8;
  }

  if (sample_type & PERF_SAMPLE_ID) {
    size += 8;
  }

  if (sample_type & PERF_SAMPLE_STREAM_ID) {
    size += 8;
  }

  has_cpu = false;
  if (sample_type & PERF_SAMPLE_CPU) {
    cpu_off = size;
    has_cpu = true;
    size += 8;
  }

  if (sample_type & PERF_SAMPLE_IDENTIFIER) {
    size += 8;
  }
  return size;
}

static int trace_file_handle(FILE *trace, size_t &begin, size_t &end,
                             bool &pt_trace, int &cpu, size_t sample_size,
                             size_t cpu_off) {
  struct perf_event_header header;
  int errcode;

  pt_trace = false;
  if (!trace) {
    return -pte_internal;
  }

  begin = ftell(trace);
  errcode = fread(&header, sizeof(header), 1, trace);
  if (errcode <= 0)
    return errcode;

  if (header.type == PERF_RECORD_AUXTRACE) {
    pt_trace = true;
    struct auxtrace_event aux;
    errcode = fread(&aux, sizeof(aux), 1, trace);
    if (errcode <= 0)
      return errcode;
    cpu = aux.cpu;
    begin = ftell(trace);
    errcode = fseek(trace, aux.size, SEEK_CUR);
    if (errcode)
      return -1;
    end = ftell(trace);
    return 1;
  } else if (header.type == PERF_RECORD_AUX_ADVANCE) {
    pt_trace = true;
    struct aux_advance_event aux;
    errcode = fread(&aux, sizeof(aux), 1, trace);
    if (errcode < 0)
      return errcode;
    cpu = aux.cpu;
    begin = ftell(trace);
    end = begin;
    return 1;
  }

  size_t loc = header.size - sizeof(header) - sample_size + cpu_off;
  errcode = fseek(trace, loc, SEEK_CUR);
  if (errcode)
    return -1;

  errcode = fread(&cpu, sizeof(cpu), 1, trace);
  if (errcode <= 0)
    return -1;

  errcode = fseek(trace, sample_size - cpu_off - sizeof(cpu), SEEK_CUR);
  if (errcode)
    return -1;

  end = ftell(trace);
  return 1;
}

struct SBTracePart {
  list<pair<size_t, size_t>> sb_offsets;
  size_t sb_size = 0;
};

struct PTTracePart {
  bool loss = false;
  list<pair<size_t, size_t>> pt_offsets;
  size_t pt_size = 0;
};

int ptjvm_split(const char *trace_data, map<int, list<TracePart>> &splits) {
  int errcode;
  FILE *trace = fopen(trace_data, "rb");
  if (!trace) {
    fprintf(stderr, "Ptjvm split: trace data cannot be opened.\n");
    return -1;
  }

  errcode = fread(&attr, sizeof(attr), 1, trace);
  if (errcode <= 0) {
    fclose(trace);
    fprintf(stderr, "Ptjvm split: Ivalid trace data format.\n");
    return errcode;
  }

  size_t sample_size, cpu_off;
  bool has_cpu;
  sample_size = get_sample_size(attr.sample_type, has_cpu, cpu_off);
  if (!has_cpu) {
    fclose(trace);
    fprintf(stderr, "Ptjvm split: Tracing with cpu sample disabled.\n");
    return -1;
  }

  map<int, pair<list<PTTracePart>, SBTracePart>> coarse_traceparts;
  while (true) {
    size_t begin, end;
    bool pt_trace;
    int cpu_id;
    errcode = trace_file_handle(trace, begin, end, pt_trace, cpu_id,
                                sample_size, cpu_off);
    if (errcode < 0) {
      fclose(trace);
      fprintf(stderr, "Ptjvm split: Illegal trace data format.\n");
      return -1;
    } else if (errcode == 0) {
      break;
    }
    if (cpu_id >= attr.nr_cpus) {
      fclose(trace);
      fprintf(stderr, "Ptjvm split: Sample has an illegal cpu id %d.\n",
              cpu_id);
      return -1;
    }
    if (pt_trace) {
      if (begin == end) {
        coarse_traceparts[cpu_id].first.push_back(PTTracePart());
        coarse_traceparts[cpu_id].first.back().loss = true;
      } else {
        if (coarse_traceparts[cpu_id].first.empty()) {
          coarse_traceparts[cpu_id].first.push_back(PTTracePart());
        }
        coarse_traceparts[cpu_id].first.back().pt_offsets.push_back(
            make_pair(begin, end));
        coarse_traceparts[cpu_id].first.back().pt_size += (end - begin);
      }
    } else {
      coarse_traceparts[cpu_id].second.sb_offsets.push_back(
          make_pair(begin, end));
      coarse_traceparts[cpu_id].second.sb_size += (end - begin);
    }
  }

  for (auto cpupart : coarse_traceparts) {
    uint8_t *sb_buffer = (uint8_t *)malloc(cpupart.second.second.sb_size);
    if (!sb_buffer) {
      fprintf(stderr, "Ptjvm split: Unable to alloc sb buffer.\n");
      fclose(trace);
      return -pte_nomem;
    }
    size_t off = 0;
    for (auto offset : cpupart.second.second.sb_offsets) {
      fseek(trace, offset.first, SEEK_SET);
      errcode = fread(sb_buffer + off, offset.second - offset.first, 1, trace);
      off += offset.second - offset.first;
    }

    for (auto part : cpupart.second.first) {
      list<TracePart> split_parts;
      uint8_t *pt_buffer = (uint8_t *)malloc(part.pt_size);
      if (!pt_buffer) {
        fprintf(stderr, "Ptjvm split: Unable to alloc pt buffer.\n");
        fclose(trace);
        return -pte_nomem;
      }
      size_t off = 0;
      for (auto offset : part.pt_offsets) {
        fseek(trace, offset.first, SEEK_SET);
        errcode = fread(pt_buffer + off, offset.second - offset.first, 1, trace);
        off += offset.second - offset.first;
      }
      split_parts.push_back(TracePart());
      split_parts.back().pt_buffer = pt_buffer;
      split_parts.back().pt_size = part.pt_size;

      if (part.loss && !split_parts.empty())
        split_parts.front().loss = true;
      splits[cpupart.first].insert(splits[cpupart.first].end(),
                                   split_parts.begin(), split_parts.end());
    }
    free(sb_buffer);
  }
  fclose(trace);
  return 0;
}
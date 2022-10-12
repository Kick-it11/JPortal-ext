#include "decoder/pt_jvm_decoder.hpp"
#include "pt/pt.hpp"
#include "decoder/decode_result.hpp"

/* This is a Intel PT decoder for Java Virtual Machine
 * It outputs the bytecode-level execution
 * both in Interpretation mode and in JIT mode
 */
#include <limits.h>
#include "sideband/sideband_decoder.hpp"
#include "decoder/pt_jvm_decoder.hpp"
#include "pt/pt_opcodes.hpp"

#include "runtime/jvm_runtime.hpp"
#include "runtime/codelets_entry.hpp"
#include "decoder/decode_result.hpp"
#include "runtime/jit_image.hpp"
#include "runtime/jit_section.hpp"
#include "utilities/load_file.hpp"
#include "utilities/definitions.hpp"
#include "java/analyser.hpp"
#include "insn/pt_insn.hpp"
#include "insn/pt_ild.hpp"

#define PERF_RECORD_AUXTRACE 71
#define PERF_RECORD_AUX_ADVANCE 72

struct aux_advance_event {
  __u32 cpu;
  __u32 tid;
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

#define SYNC_SPLIT_NUMBER 2000

struct SBTracePart {
  list<pair<size_t, size_t>> sb_offsets;
  size_t sb_size = 0;
};

struct PTTracePart {
  bool loss = false;
  list<pair<size_t, size_t>> pt_offsets;
  size_t pt_size = 0;
};

static int ptjvm_fine_split(FILE *trace, PTTracePart &part,
                            list<TracePart> &splits) {
  uint8_t *pt_buffer = nullptr;
  struct pt_config config;
  int errcode;

  pt_config_init(&config);
  config.cpu = attr.cpu;
  config.mtc_freq = attr.mtc_freq;
  config.nom_freq = attr.nom_freq;
  config.cpuid_0x15_eax = attr.cpuid_0x15_eax;
  config.cpuid_0x15_ebx = attr.cpuid_0x15_ebx;
  config.addr_filter.config.addr_cfg = pt_addr_cfg_filter;
  config.addr_filter.addr0_a = attr.addr0_a;
  config.addr_filter.addr0_b = attr.addr0_b;

  pt_buffer = (uint8_t *)malloc(part.pt_size);
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

  if (config.cpu.vendor) {
    errcode = pt_cpu_errata(&config.errata, &config.cpu);
    if (errcode < 0) {
      fprintf(stderr, "Ptjvm split: [0, 0: config error: %s]\n",
              pt_errstr(pt_errcode(errcode)));
      return errcode;
    }
  }
  config.begin = pt_buffer;
  config.end = pt_buffer + part.pt_size;

  pt_packet_decoder *decoder = pt_pkt_alloc_decoder(&config);
  if (!decoder) {
    fprintf(stderr, "Ptjvm split: fail to allocate decoder.\n");
    free(pt_buffer);
    return -pte_nomem;
  }
  int cnt = 0;
  size_t begin_offset = 0;
  size_t end_offset;
  size_t offset;
  for (;;) {
    int errcode = pt_pkt_sync_forward(decoder);
    if (errcode < 0) {
      if (errcode != -pte_eos) {
        fprintf(stderr, "Ptjvm split: packet decoder split error.\n");
        free(pt_buffer);
        return -1;
      }
      end_offset = part.pt_size;
      size_t split_size = end_offset - begin_offset;
      uint8_t *split_buffer = (uint8_t *)malloc(split_size);
      if (!split_buffer) {
        fprintf(stderr, "Ptjvm split: packet decoder split error.\n");
        return -1;
      }
      memcpy(split_buffer, pt_buffer + begin_offset, split_size);
      splits.push_back(TracePart());
      splits.back().pt_buffer = split_buffer;
      splits.back().pt_size = split_size;
      break;
    }
    errcode = pt_pkt_get_sync_offset(decoder, &offset);
    if (errcode < 0) {
      fprintf(stderr, "Ptjvm split: packet decoder split error.\n");
      return -1;
    }
    if (cnt == 0)
      begin_offset = offset;
    end_offset = offset;
    if (cnt == SYNC_SPLIT_NUMBER) {
      size_t split_size = end_offset - begin_offset;
      uint8_t *split_buffer = (uint8_t *)malloc(split_size);
      if (!split_buffer) {
        fprintf(stderr, "Ptjvm split: packet decoder split error.\n");
        return -1;
      }
      memcpy(split_buffer, pt_buffer + begin_offset, split_size);
      splits.push_back(TracePart());
      splits.back().pt_buffer = split_buffer;
      splits.back().pt_size = split_size;
      cnt = 0;
      continue;
    }
    cnt++;
  }
  return 0;
}

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
            {begin, end});
        coarse_traceparts[cpu_id].first.back().pt_size += (end - begin);
      }
    } else {
      coarse_traceparts[cpu_id].second.sb_offsets.push_back(
          {begin, end});
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
      ptjvm_fine_split(trace, part, split_parts);
      if (part.loss && !split_parts.empty())
        split_parts.front().loss = true;
      for (auto iter = split_parts.begin(); iter != split_parts.end(); iter++) {
        uint8_t *sb_buffer_p = (uint8_t *)malloc(cpupart.second.second.sb_size);
        if (!sb_buffer_p) {
          fprintf(stderr, "Ptjvm split: Unable to alloc sb buffer.\n");
          fclose(trace);
          return -pte_nomem;
        }
        memcpy(sb_buffer_p, sb_buffer, cpupart.second.second.sb_size);
        iter->sb_buffer = sb_buffer_p;
        iter->sb_size = cpupart.second.second.sb_size;
      }
      splits[cpupart.first].insert(splits[cpupart.first].end(),
                                   split_parts.begin(), split_parts.end());
    }
    free(sb_buffer);
  }
  fclose(trace);
  return 0;
}

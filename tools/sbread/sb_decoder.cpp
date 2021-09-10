#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <intel-pt.h>
#include <errno.h>

#include "pevent.h"

#define PERF_RECORD_AUXTRACE 71
#define PERF_RECORD_AUX_ADVANCE 72

struct auxtrace_event {
    __u64 size;
    __u64 offset;
    __u64 reference;
    __u32 idx;
    __u32 tid;
    __u32 cpu;
    __u32 reservered__; /* for alignment */
};

struct aux_advance_event {
    __u32 cpu;
    __u32 tid;
  __u64 advance_size;
};

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
    uint64_t low;
    uint64_t high;
};

static size_t aux_size = 0;
static size_t advance_size = 0;
bool loss = false;

#define pev_config_has(config, field) \
    (config->size >= (offsetof(struct pev_config, field) + \
              sizeof(config->field)))

static int pev_read_samples(struct pev_event *event, const uint8_t *begin,
                const uint8_t *end, const struct pev_config *config)
{
    const uint8_t *pos;
    uint64_t sample_type;

    if (!event || !begin || !config)
        return -pte_internal;

    if (!pev_config_has(config, sample_type))
        return -pte_bad_config;

    sample_type = config->sample_type;
    pos = begin;

    if (sample_type & PERF_SAMPLE_TID) {
        event->sample.pid = (const uint32_t *) &pos[0];
        event->sample.tid = (const uint32_t *) &pos[4];
        pos += 8;
    }

    if (sample_type & PERF_SAMPLE_TIME) {
        int errcode;

        event->sample.time = (const uint64_t *) pos;
        pos += 8;

        /* We're reading the time.  Let's make sure the pointer lies
         * inside the buffer.
         */
        if (end < pos)
            return -pte_nosync;

        errcode = pev_time_to_tsc(&event->sample.tsc,
                      *event->sample.time, config);
        if (errcode < 0)
            return errcode;
    }

    if (sample_type & PERF_SAMPLE_ID) {
        event->sample.id = (const uint64_t *) pos;
        pos += 8;
    }

    if (sample_type & PERF_SAMPLE_STREAM_ID) {
        event->sample.stream_id = (const uint64_t *) pos;
        pos += 8;
    }

    if (sample_type & PERF_SAMPLE_CPU) {
        event->sample.cpu = (const uint32_t *) pos;
        pos += 8;
    }

    if (sample_type & PERF_SAMPLE_IDENTIFIER) {
        event->sample.identifier = (const uint64_t *) pos;
        pos += 8;
    }

    return (int) (pos - begin);
}

int trace_file_handle(FILE *trace, pev_config &config)
{
  struct perf_event_header header;
  int errcode;

  if (!trace)
    return -pte_internal;

  errcode = fread(&header, sizeof(header), 1, trace);
  if (errcode <= 0)
    return errcode;

  if (header.type == PERF_RECORD_AUXTRACE) {
    struct auxtrace_event aux;
    errcode = fread(&aux, sizeof(aux), 1, trace);
    if (errcode <= 0)
      return errcode;
    errcode = fseek(trace, aux.size, SEEK_CUR);
    aux_size += aux.size;
    printf("<AUXTRACE> cpu:%d size:%lld acc:%ld", aux.cpu, aux.size, aux_size);
    if (errcode)
      return -1;
    return 1;
  } else if (header.type == PERF_RECORD_AUX_ADVANCE) {
    struct aux_advance_event aux;
    errcode = fread(&aux, sizeof(aux), 1, trace);
    if (errcode < 0)
      return errcode;
    printf("<AUX ADVANCE> cpu:%d advance: acc:%ld\n", aux.cpu, aux_size);
    return 1;
  }

  errcode = fseek(trace, -sizeof(header), SEEK_CUR);
  uint8_t *buffer = (uint8_t *)malloc(header.size);
  if (!buffer) {
    fprintf(stderr, "unable to allocate memory.\n");
    return -1;
  }

  errcode = fread(buffer, header.size, 1, trace);
  if (errcode)
    return -1;

  pev_event event;
  int size = pev_read(&event, buffer, buffer+header.size, &config);
  if (size != header.size)
    return -1;

  switch(event.type) {
    default:
      printf("<Unknown>\n");
    break;
    case PERF_RECORD_MMAP:
      printf("<MMAP> filename:%s addr:%lx len:%lx pgoff:%lx pid%d tid:%d; \n", 
              event.record.mmap->filename, event.record.mmap->addr, 
              event.record.mmap->len, event.record.mmap->pgoff,
              event.record.mmap->pid, event.record.mmap->tid);
      break;

    case PERF_RECORD_LOST:
      printf("<LOST> id:%ld lost:%lx\n", event.record.lost->id, event.record.lost->lost);
      break;

    case PERF_RECORD_COMM:
      printf("<COMM> comm:%s pid:%d tid:%d\n", event.record.comm->comm,
              event.record.comm->pid, event.record.comm->tid);
      break;

    case PERF_RECORD_EXIT:
      printf("<EXIT> pid:%d ppid:%d ptid:%d tid:%d time:%lx\n", event.record.exit->pid,
            event.record.exit->ppid, event.record.exit->ptid,
            event.record.exit->tid, event.record.exit->time);
      break;

    case PERF_RECORD_THROTTLE:
    case PERF_RECORD_UNTHROTTLE:
      printf("<THROTTLE> id:%ld stream_id:%ld time:%lx\n", event.record.throttle->id,
            event.record.throttle->stream_id, event.record.throttle->time);
    break;

    case PERF_RECORD_FORK:
      printf("<FORK> pid:%d ppid:%d ptid:%d tid:%d time:%lx\n", event.record.fork->pid,
            event.record.fork->ppid, event.record.fork->ptid,
            event.record.fork->tid, event.record.fork->time);
    break;

    case PERF_RECORD_MMAP2: 
      printf("<MMAP2> filename:%s addr:%lx len:%lx pgoff:%lx flags:%x ino:%lx ino_gen:%lx maj:%d min:%d pid:%d prot:%d tid:%d\n",
        event.record.mmap2->filename, event.record.mmap2->addr, event.record.mmap2->len,
        event.record.mmap2->pgoff, event.record.mmap2->flags, event.record.mmap2->ino,
        event.record.mmap2->ino_generation, event.record.mmap2->maj, event.record.mmap2->min,
        event.record.mmap2->pid, event.record.mmap2->prot, event.record.mmap2->tid);
      break;

    case PERF_RECORD_AUX:
      if (event.record.aux->flags & PERF_AUX_FLAG_TRUNCATED)
        loss = true;
      printf("<AUX> offset:%lx size:%lx flags:%lx\n", event.record.aux->aux_offset,
              event.record.aux->aux_size, event.record.aux->flags);
      break;

    case PERF_RECORD_ITRACE_START:
      printf("<ITRACE_START> pid:%d tid:%d\n",
        event.record.itrace_start->pid, event.record.itrace_start->tid);
      break;

    case PERF_RECORD_LOST_SAMPLES:
      printf("<LOST_SAMPLES> lost:%lx\n", event.record.lost_samples->lost);
      break;

    case PERF_RECORD_SWITCH:
      printf("<SWITCH>\n");
      break;

    case PERF_RECORD_SWITCH_CPU_WIDE:
      printf("<SWITCH_CPU_WIDE> next_prev_pid:%d next_prev_tid:%d\n",
            event.record.switch_cpu_wide->next_prev_pid,
            event.record.switch_cpu_wide->next_prev_tid);
      break;
  }
  if (event.sample.cpu) printf("    cpu: %d", *event.sample.cpu);
  if (event.sample.id) printf("    id: %ld", *event.sample.id);
  if (event.sample.identifier) printf("    identifier: %ld", *event.sample.identifier);
  if (event.sample.pid) printf("    pid: %d", *event.sample.pid);
  if (event.sample.stream_id) printf("stream_id: %ld", *event.sample.stream_id);
  if (event.sample.tid) printf("tid: %d", *event.sample.tid);
  if (event.sample.time) printf("id: %lx", event.sample.tsc);
  return 1;
}

int main(int argc, char **argv) {
  if (argc <= 1) {
    fprintf(stderr, "no data.\n");
    return -1;
  }
  char *filename = argv[1];
  FILE *trace = fopen(filename, "rb");
  if (!trace) {
    fprintf(stderr, "%s cannot be opened.\n", filename);
    return -1;
  }
  pev_config config;
  attr_config attr;
  int errcode = fread(&attr, sizeof(attr), 1, trace);
  if (errcode <= 0) {
    fprintf(stderr, "Trace data invalid format.\n");
    return -1;
  }
  pev_config_init(&config);
  config.sample_type = attr.sample_type;
  config.time_mult = attr.time_mult;
  config.time_shift = attr.time_shift;
  config.time_zero = attr.time_zero;

  while (true) {
    size_t begin, end;
    bool pt_trace;
    int cpu_id;
    errcode = trace_file_handle(trace, config);
    if (errcode < 0) {
       fprintf(stderr, "Illegal trace data format.\n");
       return -1;
    } else if (errcode == 0) {
       break;
    }
  }
  fclose(trace);

  if (loss) {
    printf("JPortalTrace collects %ld and advances %ld without aux loss\n", aux_size, advance_size);
  } else {
    printf("JPortalTrace collects %ld and advances %ld with aux loss\n", aux_size, advance_size);
  }
  return 0;
}

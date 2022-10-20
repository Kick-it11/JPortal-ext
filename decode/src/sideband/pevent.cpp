#include "pt/pt.hpp"
#include "sideband/pevent.hpp"

#define pev_config_has(config, field)                      \
    (config->size >= (offsetof(struct pev_config, field) + \
                      sizeof(config->field)))

int pev_time_to_tsc(uint64_t *tsc, uint64_t time,
                    const struct pev_config *config)
{
    uint64_t quot, rem, time_zero;
    uint16_t time_shift;
    uint32_t time_mult;

    if (!tsc || !config)
        return -pte_internal;

    if (!pev_config_has(config, time_zero))
        return -pte_bad_config;

    time_shift = config->time_shift;
    time_mult = config->time_mult;
    time_zero = config->time_zero;

    if (!time_mult)
        return -pte_bad_config;

    time -= time_zero;

    quot = time / time_mult;
    rem = time % time_mult;

    quot <<= time_shift;
    rem <<= time_shift;
    rem /= time_mult;

    *tsc = quot + rem;

    return 0;
}

int pev_time_from_tsc(uint64_t *time, uint64_t tsc,
                      const struct pev_config *config)
{
    uint64_t quot, rem, time_zero;
    uint16_t time_shift;
    uint32_t time_mult;

    if (!time || !config)
        return -pte_internal;

    if (!pev_config_has(config, time_zero))
        return -pte_bad_config;

    time_shift = config->time_shift;
    time_mult = config->time_mult;
    time_zero = config->time_zero;

    if (!time_mult)
        return -pte_bad_config;

    quot = tsc >> time_shift;
    rem = tsc & ((1ull << time_shift) - 1);

    quot *= time_mult;
    rem *= time_mult;
    rem >>= time_shift;

    *time = time_zero + quot + rem;

    return 0;
}

static int pev_strlen(const char *begin, const void *end_arg)
{
    const char *pos, *end;

    if (!begin || !end_arg)
        return -pte_internal;

    end = (const char *)end_arg;
    if (end < begin)
        return -pte_internal;

    for (pos = begin; pos < end; ++pos)
    {
        if (!pos[0])
            return (int)(pos - begin) + 1;
    }

    return -pte_bad_packet;
}

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

    if (sample_type & PERF_SAMPLE_TID)
    {
        event->sample.pid = (const uint32_t *)&pos[0];
        event->sample.tid = (const uint32_t *)&pos[4];
        pos += 8;
    }

    if (sample_type & PERF_SAMPLE_TIME)
    {
        int errcode;

        event->sample.time = (const uint64_t *)pos;
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

    if (sample_type & PERF_SAMPLE_ID)
    {
        event->sample.id = (const uint64_t *)pos;
        pos += 8;
    }

    if (sample_type & PERF_SAMPLE_STREAM_ID)
    {
        event->sample.stream_id = (const uint64_t *)pos;
        pos += 8;
    }

    if (sample_type & PERF_SAMPLE_CPU)
    {
        event->sample.cpu = (const uint32_t *)pos;
        pos += 8;
    }

    if (sample_type & PERF_SAMPLE_IDENTIFIER)
    {
        event->sample.identifier = (const uint64_t *)pos;
        pos += 8;
    }

    return (int)(pos - begin);
}

int pev_read(struct pev_event *event, const uint8_t *begin, const uint8_t *end,
             const struct pev_config *config)
{
    const struct perf_event_header *header;
    const uint8_t *pos;
    int size;

    if (!event || !begin || end < begin)
        return -pte_internal;

    pos = begin;
    if (end < (pos + sizeof(*header)))
        return -pte_eos;

    header = (const struct perf_event_header *)pos;
    pos += sizeof(*header);

    if (!header->type || (end < (begin + header->size)))
        return -pte_eos;

    /* Stay within the packet. */
    end = begin + header->size;

    memset(event, 0, sizeof(*event));

    event->type = header->type;
    event->misc = header->misc;

    switch (event->type)
    {
    default:
        /* We don't provide samples.
         *
         * It would be possible since we know the event's total size
         * as well as the sample size.  But why?
         */
        return (int)header->size;

    case PERF_RECORD_MMAP:
    {
        int slen;

        event->record.mmap = (const struct pev_record_mmap *)pos;

        slen = pev_strlen(event->record.mmap->filename, end);
        if (slen < 0)
            return slen;

        slen = (slen + 7) & ~7;

        pos += sizeof(*event->record.mmap);
        pos += slen;
    }
    break;

    case PERF_RECORD_LOST:
        event->record.lost = (const struct pev_record_lost *)pos;
        pos += sizeof(*event->record.lost);
        break;

    case PERF_RECORD_COMM:
    {
        int slen;

        event->record.comm = (const struct pev_record_comm *)pos;

        slen = pev_strlen(event->record.comm->comm, end);
        if (slen < 0)
            return slen;

        slen = (slen + 7) & ~7;

        pos += sizeof(*event->record.comm);
        pos += slen;
    }
    break;

    case PERF_RECORD_EXIT:
        event->record.exit = (const struct pev_record_exit *)pos;
        pos += sizeof(*event->record.exit);
        break;

    case PERF_RECORD_THROTTLE:
    case PERF_RECORD_UNTHROTTLE:
        event->record.throttle =
            (const struct pev_record_throttle *)pos;
        pos += sizeof(*event->record.throttle);
        break;

    case PERF_RECORD_FORK:
        event->record.fork = (const struct pev_record_fork *)pos;
        pos += sizeof(*event->record.fork);
        break;

    case PERF_RECORD_MMAP2:
    {
        int slen;

        event->record.mmap2 = (const struct pev_record_mmap2 *)pos;

        slen = pev_strlen(event->record.mmap2->filename, end);
        if (slen < 0)
            return slen;

        slen = (slen + 7) & ~7;

        pos += sizeof(*event->record.mmap2);
        pos += slen;
    }
    break;

    case PERF_RECORD_AUX:
        event->record.aux = (const struct pev_record_aux *)pos;
        pos += sizeof(*event->record.aux);
        break;

    case PERF_RECORD_ITRACE_START:
        event->record.itrace_start =
            (const struct pev_record_itrace_start *)pos;
        pos += sizeof(*event->record.itrace_start);
        break;

    case PERF_RECORD_LOST_SAMPLES:
        event->record.lost_samples =
            (const struct pev_record_lost_samples *)pos;
        pos += sizeof(*event->record.lost_samples);
        break;

    case PERF_RECORD_SWITCH:
        break;

    case PERF_RECORD_SWITCH_CPU_WIDE:
        event->record.switch_cpu_wide =
            (const struct pev_record_switch_cpu_wide *)pos;
        pos += sizeof(*event->record.switch_cpu_wide);
        break;
    }

    size = pev_read_samples(event, pos, end, config);
    if (size < 0)
        return size;

    pos += size;
    if (pos < begin)
        return -pte_internal;

    size = (int)(pos - begin);
    if ((uint16_t)size != header->size)
        return -pte_nosync;

    return size;
}

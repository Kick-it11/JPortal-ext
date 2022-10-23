#ifndef SIDEBAND_HPP
#define SIDEBAND_HPP

#include "sideband/pevent.hpp"

#include <map>

/* Sideband decoder */
class Sideband
{
private:
    /** Sideband data for all cpus */
    static std::map<uint32_t, std::pair<uint8_t *, uint64_t>> _data;

    /** config */
    static struct pev_config _config;

    /** is initialized or not */
    static bool _initialized;

    /** sideband data begin */
    const uint8_t *_begin;

    /** sideband data end */
    const uint8_t *_end;

    /** sideband data current */
    const uint8_t *_current;

    /** event of last event() call*/
    struct pev_event _event;

public:
    /** sideband(perf event) constructor */
    Sideband(uint32_t cpu);

    /** sideband(perf event) destructor */
    ~Sideband();

    /* iterate sideband events until time */
    bool event(uint64_t time);

    /* tid of current event */
    uint32_t tid()
    {
        return _event.sample.tid ? (*_event.sample.tid) : -1;
    }

    /** current event has a data loss flag */
    bool loss()
    {
        return (_event.type != PERF_RECORD_AUX) ? false : (_event.record.aux->flags & PERF_AUX_FLAG_TRUNCATED == PERF_AUX_FLAG_TRUNCATED);
    }

    static void initialize(std::map<uint32_t, std::pair<uint8_t *, uint64_t>> &data,
                           uint64_t sample_type, uint32_t time_mult,
                           uint16_t time_shift, uint64_t time_zero);
    static void destroy();

    static void print();
};

#endif /* SIDEBAND_HPP */

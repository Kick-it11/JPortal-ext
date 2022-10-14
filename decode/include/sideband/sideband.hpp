#ifndef SIDEBAND_DECODER
#define SIDEBAND_DECODER 

#include <inttypes.h>
#include "pevent.hpp"
#include <cassert>

class Sideband {
private:
    /* trace data begin */
    const uint8_t *_begin;
    /* trace data end */
    const uint8_t *_end;
    /* trace data current */
    const uint8_t *_current;

    /* config */
    struct pev_config _config;

    /* 
     * indicates a loss event
     * will be reset before every event call
     */
    bool _loss;

    /* current thread id */
    long _tid;

public:
    /* 
     * sideband(perf event) constructor
     * @buffer indicates data begin, @size represents data size
     * perf event recorded data buffer
     */
    Sideband(const uint8_t * buffer, const size_t size,
             uint64_t sample_type, uint16_t time_shift,
             uint32_t time_mult, uint64_t time_zero);

    /* 
     * sideband(perf event) destructor
     * set _begin, _end, _current nullptr;
     */
    ~Sideband();

    /* 
     * iterate sideband(perf event) util @time
     * 
     * return: true indicates a sideband to handle, else nothing.
     */
    bool event(uint64_t time);

    long tid() { return _tid; }

    bool loss() { return _loss; }
};

#endif
#include "sideband/sideband.hpp"

Sideband::Sideband(const uint8_t * buffer, const size_t size,
                   uint64_t sample_type, uint16_t time_shift,
                   uint32_t time_mult, uint64_t time_zero) :
    _begin(buffer), _end(buffer + size), _current(buffer) {
    pev_config_init(&_config);
    _config.sample_type = sample_type;
    _config.time_mult = time_mult;
    _config.time_shift = time_shift;
    _config.time_zero = time_zero;
}

Sideband::~Sideband() {
    _begin = nullptr;
    _end = nullptr;
    _current = nullptr;
}

bool Sideband::event(uint64_t time) {
    int size;
    struct pev_event event;
    pev_event_init(&event);
    size = pev_read(&event, _current, _end, &_config);
    if (size < 0 || time < event.sample.tsc) {
        return false;
    }
    _current += size;

    switch (event.type) {
        case PERF_RECORD_AUX: {
            _loss = (event.record.aux->flags && PERF_AUX_FLAG_TRUNCATED) == PERF_AUX_FLAG_TRUNCATED;
            break;
        }
        default: {
            break;
        }
    }
    _tid = event.sample.tid ? (*(event.sample.tid)) : -1;
    return true;
}

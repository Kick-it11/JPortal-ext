#include "sideband/sideband.hpp"

#include <cassert>

std::map<uint32_t, std::pair<uint8_t*, uint64_t>> Sideband::_data;
struct pev_config Sideband::_config;
bool Sideband::_initialized;

void Sideband::initialize(std::map<uint32_t, std::pair<uint8_t*, uint64_t>> &data,
                          uint64_t sample_type, uint32_t time_mult,
                          uint16_t time_shift, uint16_t time_zero) {
    pev_config_init(&_config);
    _config.sample_type = sample_type;
    _config.time_mult = time_mult;
    _config.time_shift = time_shift;
    _config.time_zero = time_zero;
    _data = data;

    _initialized = true;
}

void Sideband::destroy() {
    for (auto data : _data) {
        delete[] data.second.first;
    }
    _data.clear();
    _initialized = false;
}

Sideband::Sideband(uint32_t cpu) {
    assert(_initialized);
    if (!_data.count(cpu)) {
        _begin = nullptr;
        _end = nullptr;
        _current = nullptr;
    } else {
        _begin = _data[cpu].first;
        _end = _begin + _data[cpu].second;
        _current = _begin;
    }
}

Sideband::~Sideband() {
    _begin = nullptr;
    _end = nullptr;
    _current = nullptr;
}

bool Sideband::event(uint64_t time) {
    int size;
    pev_event_init(&_event);
    size = pev_read(&_event, _current, _end, &_config);
    if (size < 0 || time < _event.sample.tsc) {
        return false;
    }
    _current += size;
    return true;
}

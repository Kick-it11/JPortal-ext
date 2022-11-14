#include "pt/pt.hpp"
#include "sideband/sideband.hpp"

#include <cassert>
#include <iostream>

std::map<uint32_t, std::pair<uint8_t *, uint64_t>> Sideband::_data;
struct pev_config Sideband::_config;
bool Sideband::_initialized;

void Sideband::initialize(std::map<uint32_t, std::pair<uint8_t *, uint64_t>> &data,
                          uint64_t sample_type, uint32_t time_mult,
                          uint16_t time_shift, uint64_t time_zero)
{
    pev_config_init(&_config);
    _config.sample_type = sample_type;
    _config.time_mult = time_mult;
    _config.time_shift = time_shift;
    _config.time_zero = time_zero;
    _data = data;

    _initialized = true;
}

void Sideband::destroy()
{
    for (auto data : _data)
    {
        delete[] data.second.first;
    }
    _data.clear();
    _initialized = false;
}

void Sideband::print()
{
    assert(_initialized);
    int loss_num = 0;
    for (auto &&data : _data)
    {
        std::cout << "Sideband data for cpu" << data.first << std::endl;
        uint8_t *buffer_beg = data.second.first;
        uint8_t *buffer_end = data.second.first + data.second.second;
        uint8_t *buffer = buffer_beg;
        while (buffer < buffer_end)
        {
            struct pev_event ev;
            int size = pev_read(&ev, buffer, buffer_end, &_config);
            if (size <= 0)
            {
                std::cerr << "Sdieband error: pev_read " << buffer - buffer_beg << std::endl;
                break;
            }
            switch (ev.type)
            {
            case PERF_RECORD_MMAP:
                std::cout << "PERF_RECORD_MMAP ";
                break;
            case PERF_RECORD_LOST:
                std::cout << "PERF_RECORD_LOST ";
                break;
            case PERF_RECORD_COMM:
                std::cout << "PERF_RECORD_COMM ";
                break;
            case PERF_RECORD_EXIT:
                std::cout << "PERF_RECORD_EXIT ";
                break;
            case PERF_RECORD_THROTTLE:
                std::cout << "PERF_RECORD_THROTTLE ";
                break;
            case PERF_RECORD_UNTHROTTLE:
                std::cout << "PERF_RECORD_UNTHROTTLE ";
                break;
            case PERF_RECORD_FORK:
                std::cout << "PERF_RECORD_FORK ";
                break;
            case PERF_RECORD_READ:
                std::cout << "PERF_RECORD_READ ";
                break;
            case PERF_RECORD_SAMPLE:
                std::cout << "PERF_RECORD_SAMPLE ";
                break;
            case PERF_RECORD_MMAP2:
                std::cout << "PERF_RECORD_MMAP2 ";
                break;
            case PERF_RECORD_AUX:
                std::cout << "PERF_RECORD_AUX " << ev.record.aux->flags << " ";
                if (ev.record.aux->flags && PERF_AUX_FLAG_TRUNCATED)
                    ++loss_num;
                break;
            case PERF_RECORD_ITRACE_START:
                std::cout << "PERF_RECORD_ITRACE_START " << ev.record.itrace_start->tid << " ";
                break;
            case PERF_RECORD_LOST_SAMPLES:
                std::cout << "PERF_RECORD_LOST_SAMPLES ";
                break;
            case PERF_RECORD_SWITCH:
                std::cout << "PERF_RECORD_SWITCH ";
                break;
            case PERF_RECORD_SWITCH_CPU_WIDE:
                std::cout << "PERF_RECORD_SWITCH_CPU_WIDE ";
                break;
            case PERF_RECORD_MAX:
                std::cout << "PERF_RECORD_MAX ";
                break;
            default:
                std::cout << "PERF_RECORD UNKNOWN ";
                break;
            }
            if (ev.sample.tid)
                std::cout << *ev.sample.tid << " ";
            std::cout << ev.sample.tsc << std::endl;
            buffer += size;
        }
    }
    printf("Sideband Loss: %d\n", loss_num);
}

Sideband::Sideband(uint32_t cpu)
{
    assert(_initialized);
    if (!_data.count(cpu))
    {
        _begin = nullptr;
        _end = nullptr;
        _current = nullptr;
    }
    else
    {
        _begin = _data[cpu].first;
        _end = _begin + _data[cpu].second;
        _current = _begin;
    }
}

Sideband::~Sideband()
{
    _begin = nullptr;
    _end = nullptr;
    _current = nullptr;
}

int Sideband::event(uint64_t time, struct pev_event *event)
{
    if (!event)
    {
        return -pte_internal;
    }

    pev_event_init(event);

    int size = pev_read(event, _current, _end, &_config);
    if (size < 0)
    {
        return size;
    }

    if (time < event->sample.tsc)
    {
        return 0;
    }
    _current += size;

    return pts_event_pending;
}

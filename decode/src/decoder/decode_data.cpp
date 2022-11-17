#include "decoder/decode_data.hpp"

#include <cstring>
#include <algorithm>
#include <iostream>

std::map<uint64_t, std::vector<DecodeData::ThreadSplit>> DecodeData::sort_all_by_time(const std::vector<DecodeData *> &data)
{
    std::map<uint64_t, std::vector<ThreadSplit>> results;
    for (auto &&d : data)
    {
        for (auto &&s : d->_splits)
        {
            results[s.tid].push_back(s);
        }
    }
    for (auto iter = results.begin(); iter != results.end(); ++iter)
    {
        std::sort(iter->second.begin(), iter->second.end(),
                  [](const ThreadSplit &x, const ThreadSplit &y) -> bool
                  { return x.start_time < y.start_time || x.start_time == y.start_time && x.end_time < y.end_time; });
    }

    return results;
}

void DecodeData::expand_data(uint64_t size)
{
    if (!_data_volume)
    {
        _data_begin = new uint8_t[(initial_data_volume)];
        _data_end = _data_begin;
        _data_volume = initial_data_volume;
        if (_data_volume > size)
            return;
    }
    uint64_t new_volume = _data_volume;
    while (new_volume - (_data_end - _data_begin) < size)
        new_volume *= 2;
    uint8_t *new_data = new uint8_t[new_volume];
    memcpy(new_data, _data_begin, _data_end - _data_begin);
    delete[] _data_begin;
    _data_end = new_data + (_data_end - _data_begin);
    _data_begin = new_data;
    _data_volume = new_volume;
}

void DecodeData::write(void *data, uint64_t size)
{
    if (_data_volume - (_data_end - _data_begin) < size)
        expand_data(size);
    memcpy(_data_end, data, size);
    _data_end += size;
}

void DecodeDataRecord::record_switch(uint64_t tid, uint64_t time)
{
    if (_cur_thread)
    {
        /* the same thread */
        if (_cur_thread->tid == tid)
        {
            return;
        }

        /* set end time and addr of previous thread */
        _cur_thread->end_addr = pos();
        _cur_thread->end_time = time;

        /* previous thread contains no data */
        if (_cur_thread->end_addr == _cur_thread->start_addr)
        {
            _data->_splits.pop_back();
        }
    }

    _data->_splits.push_back(DecodeData::ThreadSplit(tid, pos(), time, _data));
    _cur_thread = &_data->_splits.back();
}

void DecodeDataRecord::record_mark_end(uint64_t time)
{
    if (_cur_thread)
    {
        /* set end time and addr of previous thread */
        _cur_thread->end_addr = pos();
        _cur_thread->end_time = time;

        /* previous thread contains no data */
        if (_cur_thread->end_addr == _cur_thread->start_addr)
        {
            _data->_splits.pop_back();
        }

        _cur_thread = nullptr;
    }
}

void DecodeDataRecord::record_method_entry(const Method *method)
{
    assert(_cur_thread != nullptr && method != nullptr);
    _type = DecodeData::_method_entry;
    _data->write(&_type, 1);
    _data->write(&method, sizeof(method));
}

void DecodeDataRecord::record_method_exit()
{
    assert(_cur_thread != nullptr);
    _type = DecodeData::_method_exit;
    _data->write(&_type, 1);
}

void DecodeDataRecord::record_branch_taken()
{
    assert(_cur_thread != nullptr);
    _type = DecodeData::_taken;
    _data->write(&_type, 1);
}

void DecodeDataRecord::record_branch_not_taken()
{
    assert(_cur_thread != nullptr);
    _type = DecodeData::_not_taken;
    _data->write(&_type, 1);
}

void DecodeDataRecord::record_switch_case(int index)
{
    assert(_cur_thread != nullptr);
    _type = DecodeData::_switch_case;
    _data->write(&_type, 1);
    _data->write(&index, sizeof(index));
}

void DecodeDataRecord::record_switch_default()
{
    assert(_cur_thread != nullptr);
    _type = DecodeData::_switch_default;
    _data->write(&_type, 1);
}

void DecodeDataRecord::record_invoke_site()
{
    assert(_cur_thread != nullptr);
    _type = DecodeData::_invoke_site;
    _data->write(&_type, 1);
}

void DecodeDataRecord::record_exception_handling(const Method *method, int current_bci, int handler_bci)
{
    assert(_cur_thread != nullptr && method != nullptr);
    _type = DecodeData::_exception;
    _data->write(&_type, 1);
    _data->write(&method, sizeof(method));
    _data->write(&current_bci, sizeof(int));
    _data->write(&handler_bci, sizeof(int));
}

void DecodeDataRecord::record_deoptimization(const Method *method, int bci)
{
    assert(_cur_thread != nullptr && method != nullptr);
    _type = DecodeData::_deoptimization;
    _data->write(&_type, 1);
    _data->write(&method, sizeof(method));
    _data->write(&bci, sizeof(int));
}

void DecodeDataRecord::record_jit_entry(const JitSection *section)
{
    assert(_cur_thread != nullptr && section != nullptr);
    _type = DecodeData::_jit_entry;
    _data->write(&_type, 1);
    _cur_section = section;
    _data->write(&section, sizeof(section));
    _pc_size = 0;
    _pc_size_pos = pos();
    _data->write(&_pc_size, sizeof(_pc_size));

    /* for aligning */
    while (pos() % sizeof(const PCStackInfo *) != 0)
    {
        uint8_t padding = DecodeData::_padding;
        _data->write(&padding, 1);
    }
}

void DecodeDataRecord::record_jit_osr_entry(const JitSection *section)
{
    assert(_cur_thread != nullptr && section != nullptr);
    _type = DecodeData::_jit_osr_entry;
    _data->write(&_type, 1);
    _cur_section = section;
    _data->write(&section, sizeof(section));
    _pc_size = 0;
    _pc_size_pos = pos();
    _data->write(&_pc_size, sizeof(_pc_size));

    /* for aligning */
    while (pos() % sizeof(const PCStackInfo *) != 0)
    {
        uint8_t padding = DecodeData::_padding;
        _data->write(&padding, 1);
    }
}

void DecodeDataRecord::record_jit_code(const JitSection *section, const PCStackInfo *info)
{
    assert(_cur_thread != nullptr && section != nullptr && info != nullptr);
    if ((_type < DecodeData::_jit_entry || _type > DecodeData::_jit_code) || section != _cur_section)
    {
        _type = DecodeData::_jit_code;
        _data->write(&_type, 1);
        _cur_section = section;
        _data->write(&section, sizeof(section));
        _pc_size = 0;
        _pc_size_pos = pos();
        _data->write(&_pc_size, sizeof(_pc_size));

        /* for aligning */
        while (pos() % sizeof(const PCStackInfo *) != 0)
        {
            uint8_t padding = DecodeData::_padding;
            _data->write(&padding, 1);
        }
    }
    _data->write(&info, sizeof(info));
    ++_pc_size;
    memcpy(_data->_data_begin + _pc_size_pos, &_pc_size, sizeof(_pc_size));
}

void DecodeDataRecord::record_jit_exception()
{
    assert(_cur_thread != nullptr);
    assert(_cur_thread != nullptr);
    _type = DecodeData::_jit_exception;
    _data->write(&_type, 1);
}

void DecodeDataRecord::record_jit_deopt()
{
    assert(_cur_thread != nullptr);
    _type = DecodeData::_jit_deopt;
    _data->write(&_type, 1);
}

void DecodeDataRecord::record_jit_deopt_mh()
{
    assert(_cur_thread != nullptr);
    _type = DecodeData::_jit_deopt_mh;
    _data->write(&_type, 1);
}

void DecodeDataRecord::record_data_loss()
{
    assert(_cur_thread != nullptr);
    _type = DecodeData::_data_loss;
    _data->write(&_type, 1);
}

void DecodeDataRecord::record_decode_error()
{
    assert(_cur_thread != nullptr);
    _type = DecodeData::_decode_error;
    _data->write(&_type, 1);
}

bool DecodeDataAccess::next_trace(DecodeData::DecodeDataType &type, uint64_t &pos)
{
    pos = _current - _data->_data_begin;
    if (_current >= _terminal)
    {
        return false;
    }
    type = (DecodeData::DecodeDataType)*_current;
    switch (type)
    {
    case DecodeData::_method_entry:
        ++_current;
        _current += sizeof(const Method *);
        break;
    case DecodeData::_method_exit:
    case DecodeData::_taken:
    case DecodeData::_not_taken:
        ++_current;
        break;
    case DecodeData::_switch_case:
        ++_current;
        _current += sizeof(int);
        break;
    case DecodeData::_switch_default:
        ++_current;
        break;
    case DecodeData::_invoke_site:
        ++_current;
        break;
    case DecodeData::_exception:
        ++_current;
        _current += (sizeof(const Method *) + sizeof(int) + sizeof(int));
        break;
    case DecodeData::_deoptimization:
        ++_current;
        _current += (sizeof(const Method *) + sizeof(int));
        break;
    case DecodeData::_jit_entry:
    case DecodeData::_jit_osr_entry:
    case DecodeData::_jit_code:
    {
        ++_current;
        _current += (sizeof(JitSection *));
        uint64_t pc_size;
        memcpy(&pc_size, _current, sizeof(pc_size));

        /* for aligning */
        while ((_current - _data->_data_begin) % (sizeof(const PCStackInfo *)) != 0)
        {
            ++_current;
        }
        _current += (sizeof(pc_size) + sizeof(const PCStackInfo *) * pc_size);
        break;
    }
    case DecodeData::_jit_exception:
    case DecodeData::_jit_deopt:
    case DecodeData::_jit_deopt_mh:
        ++_current;
        break;
    default:
        std::cerr << "DecodeData eror: acess unknown type" << std::endl;
        exit(1);
    }
    return true;
}

bool DecodeDataAccess::get_method_entry(uint64_t pos, const Method *&method)
{
    if (pos > _data->_data_end - _data->_data_begin)
    {
        return false;
    }
    uint8_t *buf = _data->_data_begin + pos;
    DecodeData::DecodeDataType type = (DecodeData::DecodeDataType) * (buf);
    ++buf;
    if (type != DecodeData::_method_entry)
    {
        return false;
    }
    memcpy(&method, buf, sizeof(method));
    assert(method != nullptr);
    return true;
}

bool DecodeDataAccess::get_switch_case_index(uint64_t pos, int &index)
{
    if (pos > _data->_data_end - _data->_data_begin)
    {
        return false;
    }
    uint8_t *buf = _data->_data_begin + pos;
    DecodeData::DecodeDataType type = (DecodeData::DecodeDataType) * (buf);
    ++buf;
    if (type != DecodeData::_switch_case)
    {
        return false;
    }
    memcpy(&index, buf, sizeof(index));
    assert(index >= 0);
    return true;
}

bool DecodeDataAccess::get_exception_handling(uint64_t pos, const Method *&method,
                                              int &current_bci, int &handler_bci)
{
    if (pos > _data->_data_end - _data->_data_begin)
    {
        return false;
    }
    uint8_t *buf = _data->_data_begin + pos;
    DecodeData::DecodeDataType type = (DecodeData::DecodeDataType) * (buf);
    ++buf;
    if (type != DecodeData::_exception)
    {
        return false;
    }
    memcpy(&method, buf, sizeof(method));
    buf += sizeof(method);
    memcpy(&current_bci, buf, sizeof(current_bci));
    buf += sizeof(current_bci);
    memcpy(&handler_bci, buf, sizeof(handler_bci));
    assert(method != nullptr);
    return true;
}

bool DecodeDataAccess::get_deoptimization(uint64_t pos, const Method *&method, int &bci)
{
    if (pos > _data->_data_end - _data->_data_begin)
    {
        return false;
    }
    uint8_t *buf = _data->_data_begin + pos;
    DecodeData::DecodeDataType type = (DecodeData::DecodeDataType) * (buf);
    ++buf;
    if (type != DecodeData::_deoptimization)
    {
        return false;
    }
    memcpy(&method, buf, sizeof(method));
    buf += sizeof(method);
    memcpy(&bci, buf, sizeof(bci));
    assert(method != nullptr);
    return true;
}

bool DecodeDataAccess::get_jit_code(uint64_t pos, const JitSection *&section,
                                    const PCStackInfo **&pcs, uint64_t &size)
{
    if (pos > _data->_data_end - _data->_data_begin)
    {
        return false;
    }
    uint8_t *buf = _data->_data_begin + pos;
    DecodeData::DecodeDataType type = (DecodeData::DecodeDataType) * (buf);
    ++buf;
    if (type < DecodeData::_jit_entry || type > DecodeData::_jit_code)
    {
        return false;
    }
    memcpy(&section, buf, sizeof(section));
    buf += sizeof(section);
    memcpy(&size, buf, sizeof(size));
    buf += sizeof(size);

    /* for aligning */
    while ((buf - _data->_data_begin) % sizeof(const PCStackInfo *) != 0)
    {
        ++buf;
    }
    pcs = (const PCStackInfo **)buf;
    assert(section != nullptr && pcs != nullptr);
    return true;
}

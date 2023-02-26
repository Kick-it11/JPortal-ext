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

void DecodeDataRecord::switch_in(uint64_t tid, uint64_t time)
{
    if (_cur_thread)
    {
        if (_cur_thread->tid == tid)
        {
            return;
        }
        switch_out(time);
    }

    _data->_splits.push_back(DecodeData::ThreadSplit(tid, pos(), time, _data));
    _cur_thread = &_data->_splits.back();
}

/* must be called at the end of decoding */
void DecodeDataRecord::switch_out(uint64_t time)
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

bool DecodeDataRecord::record_method_entry(int method_id)
{
    if (!_cur_thread || !method_id < 0)
        return false;
    _type = DecodeData::_method_entry;
    _data->write(&_type, 1);
    _data->write(&method_id, sizeof(int));
    return true;
}

bool DecodeDataRecord::record_method_exit(int method_id)
{
    if (!_cur_thread || !method_id < 0)
        return false;
    _type = DecodeData::_method_exit;
    _data->write(&_type, 1);
    _data->write(&method_id, sizeof(int));
    return true;
}

bool DecodeDataRecord::record_method_point(int method_id)
{
    if (!_cur_thread || !method_id < 0)
        return false;
    _type = DecodeData::_method_point;
    _data->write(&_type, 1);
    _data->write(&method_id, sizeof(int));
    return true;
}

bool DecodeDataRecord::record_branch_taken()
{
    if (!_cur_thread)
        return false;
    _type = DecodeData::_taken;
    _data->write(&_type, 1);
    return true;
}

bool DecodeDataRecord::record_branch_not_taken()
{
    if (!_cur_thread)
        return false;
    _type = DecodeData::_not_taken;
    _data->write(&_type, 1);
    return true;
}

bool DecodeDataRecord::record_switch_case(int index)
{
    if (!_cur_thread)
        return false;
    _type = DecodeData::_switch_case;
    _data->write(&_type, 1);
    _data->write(&index, sizeof(index));
    return true;
}

bool DecodeDataRecord::record_switch_default()
{
    if (!_cur_thread)
        return false;
    _type = DecodeData::_switch_default;
    _data->write(&_type, 1);
    return true;
}

bool DecodeDataRecord::record_bci(int bci)
{
    if (!_cur_thread)
        return false;
    _type = DecodeData::_bci;
    _data->write(&_type, 1);
    _data->write(&bci, sizeof(int));
    return true;
}

bool DecodeDataRecord::record_ret_code()
{
    if (!_cur_thread)
        return false;
    _type = DecodeData::_ret_code;
    _data->write(&_type, 1);
    return true;
}

bool DecodeDataRecord::record_deoptimization()
{
    if (!_cur_thread)
        return false;
    _type = DecodeData::_deoptimization;
    _data->write(&_type, 1);
    return true;
}

bool DecodeDataRecord::record_throw_exception()
{
    if (!_cur_thread)
        return false;
    _type = DecodeData::_throw_exception;
    _data->write(&_type, 1);
    return true;
}

bool DecodeDataRecord::record_pop_frame()
{
    if (!_cur_thread)
        return false;
    _type = DecodeData::_pop_frame;
    _data->write(&_type, 1);
    return true;
}

bool DecodeDataRecord::record_earlyret()
{
    if (!_cur_thread)
        return false;
    _type = DecodeData::_earlyret;
    _data->write(&_type, 1);
    return true;
}

bool DecodeDataRecord::record_non_invoke_ret()
{
    if (!_cur_thread)
        return false;
    _type = DecodeData::_non_invoke_ret;
    _data->write(&_type, 1);
    return true;
}

bool DecodeDataRecord::record_java_call_begin()
{
    if (!_cur_thread)
        return false;
    _type = DecodeData::_java_call_begin;
    _data->write(&_type, 1);
    return true;
}

bool DecodeDataRecord::record_java_call_end()
{
    if (!_cur_thread)
        return false;
    _type = DecodeData::_java_call_end;
    _data->write(&_type, 1);
    return true;
}

bool DecodeDataRecord::record_jit_entry(int section_id)
{
    if (!_cur_thread || section_id < 0)
        return false;
    _type = DecodeData::_jit_entry;
    _data->write(&_type, 1);
    _section_id = section_id;
    _data->write(&_section_id, sizeof(_section_id));
    return true;
}

bool DecodeDataRecord::record_jit_osr_entry(int section_id)
{
    if (!_cur_thread || section_id < 0)
        return false;
    _type = DecodeData::_jit_osr_entry;
    _data->write(&_type, 1);
    _section_id = section_id;
    _data->write(&_section_id, sizeof(_section_id));
    return true;
}

bool DecodeDataRecord::record_jit_code(int section_id, int idx)
{
    if (!_cur_thread || section_id < 0)
        return false;
    if ((_type < DecodeData::_jit_entry || _type > DecodeData::_jit_code) || section_id != _section_id)
    {
        _type = DecodeData::_jit_code;
        _data->write(&_type, 1);
        _section_id = section_id;
        _data->write(&_section_id, sizeof(_section_id));
        _data->write(&idx, sizeof(idx));
    }

    uint8_t mid_type = DecodeData::_jit_pc_info;
    _data->write(&mid_type, sizeof(mid_type));
    _data->write(&idx, sizeof(idx));
    return true;
}

bool DecodeDataRecord::record_jit_return()
{
    if (!_cur_thread)
        return false;
    _type = DecodeData::_jit_return;
    _data->write(&_type, 1);
    return true;
}

bool DecodeDataRecord::record_data_loss()
{
    if (!_cur_thread)
        return false;
    _type = DecodeData::_data_loss;
    _data->write(&_type, 1);
    return true;
}

bool DecodeDataRecord::record_decode_error()
{
    if (!_cur_thread)
        return false;
    _type = DecodeData::_decode_error;
    _data->write(&_type, 1);
    return true;
}

bool DecodeDataAccess::next_trace(DecodeData::DecodeDataType &type, uint64_t &pos)
{
    pos = _current - _data->_data_begin;
    while (_current >= _terminal)
    {
        if (_it == _splits.end())
        {
            return false;
        }
        else
        {
            _data = _it->data;
            _current = _data->_data_begin + _it->start_addr;
            _terminal = _data->_data_begin + _it->end_addr;
            ++_it;
        }
    }
    type = (DecodeData::DecodeDataType)*_current;
    switch (type)
    {
    case DecodeData::_method_entry:
    case DecodeData::_method_exit:
    case DecodeData::_method_point:
        ++_current;
        _current += sizeof(int);
        break;
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
    case DecodeData::_bci:
        ++_current;
        _current += sizeof(int);
        break;
    case DecodeData::_ret_code:
    case DecodeData::_deoptimization:
    case DecodeData::_throw_exception:
    case DecodeData::_pop_frame:
    case DecodeData::_earlyret:
    case DecodeData::_non_invoke_ret:
    case DecodeData::_java_call_begin:
    case DecodeData::_java_call_end:
        ++_current;
        break;
    case DecodeData::_jit_entry:
    case DecodeData::_jit_osr_entry:
    case DecodeData::_jit_code:
    {
        ++_current;
        /* skip section id */
        _current += sizeof(int);
        /* skip pc info */
        while (_current < _terminal && (DecodeData::DecodeDataType)*_current == DecodeData::_jit_pc_info)
        {
            ++_current;
            _current += sizeof(int);
        }
        break;
    }
    case DecodeData::_jit_return:
    case DecodeData::_decode_error:
    case DecodeData::_data_loss:
        ++_current;
        break;
    default:
        std::cerr << "DecodeData error: access unknown type" << type << std::endl;
        exit(1);
    }
    return true;
}

bool DecodeDataAccess::current_trace(DecodeData::DecodeDataType &type)
{
    while (_current >= _terminal)
    {
        if (_it == _splits.end())
        {
            return false;
        }
        else
        {
            _data = _it->data;
            _current = _data->_data_begin + _it->start_addr;
            _terminal = _data->_data_begin + _it->end_addr;
            ++_it;
        }
    }
    type = (DecodeData::DecodeDataType)*_current;
    switch (type)
    {
    case DecodeData::_method_entry:
    case DecodeData::_method_exit:
    case DecodeData::_method_point:
    case DecodeData::_taken:
    case DecodeData::_not_taken:
    case DecodeData::_switch_case:
    case DecodeData::_switch_default:
    case DecodeData::_bci:
    case DecodeData::_ret_code:
    case DecodeData::_deoptimization:
    case DecodeData::_throw_exception:
    case DecodeData::_pop_frame:
    case DecodeData::_earlyret:
    case DecodeData::_non_invoke_ret:
    case DecodeData::_java_call_begin:
    case DecodeData::_java_call_end:
    case DecodeData::_jit_entry:
    case DecodeData::_jit_osr_entry:
    case DecodeData::_jit_code:
    case DecodeData::_jit_return:
    case DecodeData::_decode_error:
    case DecodeData::_data_loss:
        break;
    default:
        std::cerr << "DecodeData error: access unknown type" << type << std::endl;
        exit(1);
    }
    return true;
}

bool DecodeDataAccess::get_method_entry(uint64_t pos, int &method_id)
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
    memcpy(&method_id, buf, sizeof(int));
    assert(method_id >= 0);
    return true;
}

bool DecodeDataAccess::get_method_exit(uint64_t pos, int &method_id)
{
    if (pos > _data->_data_end - _data->_data_begin)
    {
        return false;
    }
    uint8_t *buf = _data->_data_begin + pos;
    DecodeData::DecodeDataType type = (DecodeData::DecodeDataType) * (buf);
    ++buf;
    if (type != DecodeData::_method_exit)
    {
        return false;
    }
    memcpy(&method_id, buf, sizeof(int));
    assert(method_id >= 0);
    return true;
}

bool DecodeDataAccess::get_method_point(uint64_t pos, int &method_id)
{
    if (pos > _data->_data_end - _data->_data_begin)
    {
        return false;
    }
    uint8_t *buf = _data->_data_begin + pos;
    DecodeData::DecodeDataType type = (DecodeData::DecodeDataType) * (buf);
    ++buf;
    if (type != DecodeData::_method_point)
    {
        return false;
    }
    memcpy(&method_id, buf, sizeof(int));
    assert(method_id >= 0);
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

bool DecodeDataAccess::get_bci(uint64_t pos, int &bci)
{
    if (pos > _data->_data_end - _data->_data_begin)
    {
        return false;
    }
    uint8_t *buf = _data->_data_begin + pos;
    DecodeData::DecodeDataType type = (DecodeData::DecodeDataType) * (buf);
    ++buf;
    if (type != DecodeData::_bci)
    {
        return false;
    }
    memcpy(&bci, buf, sizeof(bci));
    return true;
}

bool DecodeDataAccess::get_jit_code(uint64_t pos, int &section_id, std::vector<int> &pcs)
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
    memcpy(&section_id, buf, sizeof(int));
    buf += sizeof(int);
    while (buf < _terminal && (DecodeData::DecodeDataType) * (buf) == DecodeData::_jit_pc_info)
    {
        ++buf;
        int pc;
        memcpy(&pc, buf, sizeof(int));
        pcs.push_back(pc);
        buf += sizeof(int);
    }
    return true;
}

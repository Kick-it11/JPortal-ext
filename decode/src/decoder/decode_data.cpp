#include "decoder/decode_data.hpp"
#include "java/method.hpp"
#include "runtime/jit_section.hpp"
#include "runtime/jvm_runtime.hpp"
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

bool DecodeDataRecord::record_method_entry(const Method *method)
{
    if (!_cur_thread || !method)
        return false;
    int method_id = method->id();
    _type = DecodeData::_method_entry;
    _data->write(&_type, 1);
    _data->write(&method_id, sizeof(int));
    return true;
}

bool DecodeDataRecord::record_method_exit(const Method *method)
{
    if (!_cur_thread || !method)
        return false;
    int method_id = method->id();
    _type = DecodeData::_method_exit;
    _data->write(&_type, 1);
    _data->write(&method_id, sizeof(int));
    return true;
}

bool DecodeDataRecord::record_method_point(const Method *method)
{
    if (!_cur_thread || !method)
        return false;
    int method_id = method->id();
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

bool DecodeDataRecord::record_rethrow_exception()
{
    if (!_cur_thread)
        return false;
    _type = DecodeData::_rethrow_exception;
    _data->write(&_type, 1);
    return true;
}

bool DecodeDataRecord::record_handle_exception()
{
    if (!_cur_thread)
        return false;
    _type = DecodeData::_handle_exception;
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

bool DecodeDataRecord::record_osr()
{
    if (!_cur_thread)
        return false;
    _type = DecodeData::_osr;
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

bool DecodeDataRecord::record_jit_entry(const JitSection *section)
{
    if (!_cur_thread || !section)
        return false;
    if (_type != DecodeData::_jit_code || section->id() != _section_id)
    {
        _type = DecodeData::_jit_code;
        _data->write(&_type, 1);
        _section_id = section->id();
        _data->write(&_section_id, sizeof(_section_id));
    }

    uint8_t mid_type = DecodeData::_jit_entry;
    _data->write(&mid_type, sizeof(mid_type));
    return true;
}

bool DecodeDataRecord::record_jit_osr_entry(const JitSection *section)
{
    if (!_cur_thread || !section)
        return false;
    if (_type != DecodeData::_jit_code || section->id() != _section_id)
    {
        _type = DecodeData::_jit_code;
        _data->write(&_type, 1);
        _section_id = section->id();
        _data->write(&_section_id, sizeof(_section_id));
    }

    uint8_t mid_type = DecodeData::_jit_osr_entry;
    _data->write(&mid_type, sizeof(mid_type));
    return true;
}

bool DecodeDataRecord::record_jit_pc_info(const JitSection *section, int ind)
{
    if (!_cur_thread || !section)
        return false;
    if (_type != DecodeData::_jit_code || section->id() != _section_id)
    {
        _type = DecodeData::_jit_code;
        _data->write(&_type, 1);
        _section_id = section->id();
        _data->write(&_section_id, sizeof(_section_id));
    }

    uint8_t mid_type = DecodeData::_jit_pc_info;
    _data->write(&mid_type, sizeof(mid_type));
    _data->write(&ind, sizeof(ind));
    return true;
}

bool DecodeDataRecord::record_jit_return(const JitSection *section)
{
    if (!_cur_thread || !section)
        return false;
    if (_type != DecodeData::_jit_code || section->id() != _section_id)
    {
        _type = DecodeData::_jit_code;
        _data->write(&_type, 1);
        _section_id = section->id();
        _data->write(&_section_id, sizeof(_section_id));
    }

    uint8_t mid_type = DecodeData::_jit_return;
    _data->write(&mid_type, sizeof(mid_type));
    return true;
}

bool DecodeDataRecord::record_jit_exception(const JitSection *section)
{
    if (!_cur_thread || !section)
        return false;
    if (_type != DecodeData::_jit_code || section->id() != _section_id)
    {
        _type = DecodeData::_jit_code;
        _data->write(&_type, 1);
        _section_id = section->id();
        _data->write(&_section_id, sizeof(_section_id));
    }

    uint8_t mid_type = DecodeData::_jit_exception;
    _data->write(&mid_type, sizeof(mid_type));
    return true;
}

bool DecodeDataRecord::record_jit_unwind(const JitSection *section)
{
    if (!_cur_thread || !section)
        return false;
    if (_type != DecodeData::_jit_code || section->id() != _section_id)
    {
        _type = DecodeData::_jit_code;
        _data->write(&_type, 1);
        _section_id = section->id();
        _data->write(&_section_id, sizeof(_section_id));
    }

    uint8_t mid_type = DecodeData::_jit_deopt;
    _data->write(&mid_type, sizeof(mid_type));
    return true;
}

bool DecodeDataRecord::record_jit_deopt(const JitSection *section)
{
    if (!_cur_thread || !section)
        return false;
    if (_type != DecodeData::_jit_code || section->id() != _section_id)
    {
        _type = DecodeData::_jit_code;
        _data->write(&_type, 1);
        _section_id = section->id();
        _data->write(&_section_id, sizeof(_section_id));
    }

    uint8_t mid_type = DecodeData::_jit_deopt;
    _data->write(&mid_type, sizeof(mid_type));
    return true;
}

bool DecodeDataRecord::record_jit_deopt_mh(const JitSection *section)
{
    if (!_cur_thread || !section)
        return false;
    if (_type != DecodeData::_jit_code || section->id() != _section_id)
    {
        _type = DecodeData::_jit_code;
        _data->write(&_type, 1);
        _section_id = section->id();
        _data->write(&_section_id, sizeof(_section_id));
    }

    uint8_t mid_type = DecodeData::_jit_deopt_mh;
    _data->write(&mid_type, sizeof(mid_type));
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
    case DecodeData::_rethrow_exception:
    case DecodeData::_handle_exception:
    case DecodeData::_pop_frame:
    case DecodeData::_earlyret:
    case DecodeData::_non_invoke_ret:
    case DecodeData::_osr:
    case DecodeData::_java_call_begin:
    case DecodeData::_java_call_end:
        ++_current;
        break;
    case DecodeData::_jit_code:
    {
        ++_current;
        /* skip section id */
        _current += sizeof(int);
        /* skip pc info */
        while (_current < _terminal)
        {
            DecodeData::DecodeDataType mid_type = (DecodeData::DecodeDataType)*_current;
            if (mid_type == DecodeData::_jit_pc_info) {
                ++_current;
                _current += sizeof(int);
            }
            else if (mid_type >= DecodeData::_jit_entry && mid_type <= DecodeData::_jit_deopt_mh)
            {
                ++_current;
            }
            else
            {
                break;
            }
        }
        break;
    }
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
    case DecodeData::_rethrow_exception:
    case DecodeData::_handle_exception:
    case DecodeData::_pop_frame:
    case DecodeData::_earlyret:
    case DecodeData::_non_invoke_ret:
    case DecodeData::_osr:
    case DecodeData::_java_call_begin:
    case DecodeData::_java_call_end:
    case DecodeData::_jit_code:
    case DecodeData::_decode_error:
    case DecodeData::_data_loss:
        break;
    default:
        std::cerr << "DecodeData error: access unknown type" << type << std::endl;
        exit(1);
    }
    return true;
}

bool DecodeDataAccess::get_method(uint64_t pos, const Method *&method)
{
    if (pos > _data->_data_end - _data->_data_begin)
    {
        return false;
    }
    uint8_t *buf = _data->_data_begin + pos;
    DecodeData::DecodeDataType type = (DecodeData::DecodeDataType) * (buf);
    int method_id;
    ++buf;
    if (type != DecodeData::_method_entry && type != DecodeData::_method_exit
        && type != DecodeData::_method_point)
    {
        return false;
    }
    memcpy(&method_id, buf, sizeof(int));
    assert(method_id >= 0);
    method = JVMRuntime::method_by_id(method_id);
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

bool DecodeDataAccess::get_jit_code(uint64_t pos, const JitSection *&section, std::vector<int> &pcs)
{
    if (pos > _data->_data_end - _data->_data_begin)
    {
        return false;
    }
    uint8_t *buf = _data->_data_begin + pos;
    DecodeData::DecodeDataType type = (DecodeData::DecodeDataType) * (buf);
    int section_id;
    ++buf;
    if (type != DecodeData::_jit_code)
    {
        return false;
    }
    memcpy(&section_id, buf, sizeof(int));
    assert(section_id >= 0);
    section = JVMRuntime::jit_section_by_id(section_id);
    assert(section != nullptr);
    buf += sizeof(int);
    while (buf < _terminal)
    {
        DecodeData::DecodeDataType mid_type = (DecodeData::DecodeDataType) * (buf);
        if (mid_type == DecodeData::_jit_pc_info)
        {
            ++buf;
            int pc;
            memcpy(&pc, buf, sizeof(int));
            pcs.push_back(pc);
            buf += sizeof(int);
        }
        else if (mid_type >= DecodeData::_jit_entry && mid_type <= DecodeData::_jit_deopt_mh)
        {
            ++buf;
            pcs.push_back(-mid_type);
        }
        else
        {
            break;
        }
    }
    return true;
}

bool DecodeDataEvent::method_entry_event()
{
    DecodeData::DecodeDataType cur_type;
    uint64_t loc;
    if (!_access.next_trace(cur_type, loc) || cur_type != DecodeData::_method_entry
        || !_access.get_method(loc, _method))
    {
        _type = DecodeData::_illegal;
        std::cerr << "DecodeDataEvent error: Fail to get method entry event at "
                  << loc << std::endl;
        exit(1);
    }
    return true;
}

bool DecodeDataEvent::method_exit_event()
{
    DecodeData::DecodeDataType cur_type;
    uint64_t loc;
    if (!_access.next_trace(cur_type, loc) || cur_type != DecodeData::_method_exit
        || !_access.get_method(loc, _method))
    {
        std::cerr << "DecodeDataEvent error: Fail to get method exit event at "
                  << loc << std::endl;
        exit(1);
    }
    return true;
}

bool DecodeDataEvent::method_point_event(std::string fevent)
{
    DecodeData::DecodeDataType cur_type;
    uint64_t loc;
    if (!_access.next_trace(cur_type, loc) || cur_type != DecodeData::_method_point
        || !_access.get_method(loc, _method))
    {
        std::cerr << "DecodeDataEvent error: Fail to get method point event from "
                  << fevent << " at " << loc << std::endl;
        exit(1);
    }
    if (!_access.current_trace(cur_type) || cur_type != DecodeData::_bci)
    {
        _type = DecodeData::_illegal;
        _method = nullptr;
        std::cerr << "DecodeDataEvent error: Fail to get method point bci from "
                  << fevent << " at " << loc << std::endl;
        return false;
    }
    if (!_access.next_trace(cur_type, loc) || cur_type != DecodeData::_bci
        || !_access.get_bci(loc, _bci_or_ind))
    {
        std::cerr << "DecodeDataEvent error: Fail to get method point bci from "
                  << fevent << " at " << loc << std::endl;
        exit(1);
    }
    return true;
}

bool DecodeDataEvent::taken_event()
{
    DecodeData::DecodeDataType cur_type;
    uint64_t loc;
    if (!_access.next_trace(cur_type, loc) || cur_type != DecodeData::_taken)
    {
        std::cerr << "DecodeDataEvent error: Fail to get taken event at "
                  << loc << std::endl;
        exit(1);
    }
    return true;
}

bool DecodeDataEvent::not_taken_event()
{
    DecodeData::DecodeDataType cur_type;
    uint64_t loc;
    if (!_access.next_trace(cur_type, loc) || cur_type != DecodeData::_not_taken)
    {
        std::cerr << "DecodeDataEvent error: Fail to get not taken event at "
                  << loc << std::endl;
        exit(1);
    }
    return true;
}

bool DecodeDataEvent::switch_case_event()
{
    DecodeData::DecodeDataType cur_type;
    uint64_t loc;
    if (!_access.next_trace(cur_type, loc) || cur_type != DecodeData::_switch_case
        || !_access.get_switch_case_index(loc, _bci_or_ind))
    {
        std::cerr << "DecodeDataEvent error: Fail to switch case index event at "
                  << loc << std::endl;
        exit(1);
    }
    return true;
}

bool DecodeDataEvent::switch_default_event()
{
    DecodeData::DecodeDataType cur_type;
    uint64_t loc;
    if (!_access.next_trace(cur_type, loc) || cur_type != DecodeData::_switch_default)
    {
        std::cerr << "DecodeDataEvent error: Fail to get switch default event at "
                  << loc << std::endl;
        exit(1);
    }
    return true;
}

bool DecodeDataEvent::ret_code_event()
{
    DecodeData::DecodeDataType cur_type;
    uint64_t loc;
    if (!_access.next_trace(cur_type, loc) || cur_type != DecodeData::_ret_code)
    {
        std::cerr << "DecodeDataEvent error: Fail to get ret code event at "
                  << loc << std::endl;
        exit(1);
    }
    if (!_access.current_trace(cur_type) || cur_type != DecodeData::_method_point)
    {
        _type = DecodeData::_illegal;
        std::cerr << "DecodeDataEvent error: Fail to get method point of ret code event at "
                  << loc << std::endl;
        return false;
    }
    return method_point_event("ret code");
}

bool DecodeDataEvent::deoptimization_event()
{
    DecodeData::DecodeDataType cur_type;
    uint64_t loc;
    if (!_access.next_trace(cur_type, loc) || cur_type != DecodeData::_deoptimization)
    {
        std::cerr << "DecodeDataEvent error: Fail to get deoptimization event at "
                  << loc << std::endl;
        exit(1);
    }
    if (!_access.current_trace(cur_type) || cur_type != DecodeData::_method_point)
    {
        _type = DecodeData::_illegal;
        std::cerr << "DecodeDataEvent error: Fail to get method point of deoptimization event at "
                  << loc << std::endl;
        return false;
    }
    return method_point_event("deoptimization");
}

bool DecodeDataEvent::throw_exception_event()
{
    DecodeData::DecodeDataType cur_type;
    uint64_t loc;
    if (!_access.next_trace(cur_type, loc) || cur_type != DecodeData::_throw_exception)
    {
        std::cerr << "DecodeDataEvent error: Fail to get throw exception event at "
                  << loc << std::endl;
        exit(1);
    }
    if (!_access.current_trace(cur_type) || cur_type != DecodeData::_method_point)
    {
        _type = DecodeData::_illegal;
        std::cerr << "DecodeDataEvent error: Fail to get method point of throw exception event at "
                  << loc << std::endl;
        return false;
    }
    return method_point_event("throw exception");
}

bool DecodeDataEvent::rethrow_exception_event()
{
    DecodeData::DecodeDataType cur_type;
    uint64_t loc;
    if (!_access.next_trace(cur_type, loc) || cur_type != DecodeData::_rethrow_exception)
    {
        std::cerr << "DecodeDataEvent error: Fail to get rethrow exception event at "
                  << loc << std::endl;
        exit(1);
    }
}
bool DecodeDataEvent::handle_exception_event()
{
    DecodeData::DecodeDataType cur_type;
    uint64_t loc;
    if (!_access.next_trace(cur_type, loc) || cur_type != DecodeData::_handle_exception)
    {
        std::cerr << "DecodeDataEvent error: Fail to get throw exception event at "
                  << loc << std::endl;
        exit(1);
    }
    if (!_access.current_trace(cur_type) || cur_type != DecodeData::_method_point)
    {
        _type = DecodeData::_illegal;
        std::cerr << "DecodeDataEvent error: Fail to get method point of throw exception event at "
                  << loc << std::endl;
        return false;
    }
    if (!method_point_event("throw exception"))
        return false;
}

bool DecodeDataEvent::pop_frame_event()
{
    DecodeData::DecodeDataType cur_type;
    uint64_t loc;
    if (!_access.next_trace(cur_type, loc) || cur_type != DecodeData::_pop_frame)
    {
        std::cerr << "DecodeDataEvent error: Fail to get pop frame event at "
                  << loc << std::endl;
        exit(1);
    }
    if (!_access.current_trace(cur_type) || cur_type != DecodeData::_method_point)
    {
        std::cerr << "DecodeDataEvent error: Fail to get method point of pop frame event at "
                  << loc << std::endl;
        return false;
    }
    return method_point_event("pop frame");
}

bool DecodeDataEvent::earlyret_event()
{
    DecodeData::DecodeDataType cur_type;
    uint64_t loc;
    if (!_access.next_trace(cur_type, loc) || cur_type != DecodeData::_earlyret)
    {
        std::cerr << "DecodeDataEvent error: Fail to get earlyret event at "
                  << loc << std::endl;
        exit(1);
    }
    if (!_access.current_trace(cur_type) || cur_type != DecodeData::_method_point)
    {
        std::cerr << "DecodeDataEvent error: Fail to get method point of earlyret code event at "
                  << loc << std::endl;
        return false;
    }
    return method_point_event("earlyret");
}

bool DecodeDataEvent::non_invoke_event()
{
    DecodeData::DecodeDataType cur_type;
    uint64_t loc;
    if (!_access.next_trace(cur_type, loc) || cur_type != DecodeData::_non_invoke_ret)
    {
        std::cerr << "DecodeDataEvent error: Fail to get non invoke ret event at "
                  << loc << std::endl;
        exit(1);
    }
    if (!_access.current_trace(cur_type) || cur_type != DecodeData::_method_point)
    {
        std::cerr << "DecodeDataEvent error: Fail to get method point of non invoke ret code event at "
                  << loc << std::endl;
        return false;
    }
    return method_point_event("non invoke ret");
}

bool DecodeDataEvent::osr_event()
{
    DecodeData::DecodeDataType cur_type;
    uint64_t loc;
    if (!_access.next_trace(cur_type, loc) || cur_type != DecodeData::_osr)
    {
        std::cerr << "DecodeDataEvent error: Fail to get osr event at "
                  << loc << std::endl;
        exit(1);
    }
    if (!_access.current_trace(cur_type) || cur_type != DecodeData::_method_point)
    {
        std::cerr << "DecodeDataEvent error: Fail to get method point of osr event at "
                  << loc << std::endl;
        return false;
    }
    return method_point_event("osr");
}

bool DecodeDataEvent::java_call_begin_event()
{
    DecodeData::DecodeDataType cur_type;
    uint64_t loc;
    if (!_access.next_trace(cur_type, loc) || cur_type != DecodeData::_java_call_begin)
    {
        std::cerr << "DecodeDataEvent error: Fail to get java call begin event at "
                  << loc << std::endl;
        exit(1);
    }
    return true;
}

bool DecodeDataEvent::java_call_end_event()
{
    DecodeData::DecodeDataType cur_type;
    uint64_t loc;
    if (!_access.next_trace(cur_type, loc) || cur_type != DecodeData::_java_call_end)
    {
        std::cerr << "DecodeDataEvent error: Fail to get java call end event at "
                  << loc << std::endl;
        exit(1);
    }
    return true;
}

bool DecodeDataEvent::jit_code_event()
{
    DecodeData::DecodeDataType cur_type;
    uint64_t loc;
    if (!_access.next_trace(cur_type, loc) || cur_type != DecodeData::_jit_code
        || _access.get_jit_code(loc, _section, _pcs))
    {
        std::cerr << "DecodeDataEvent error: Fail to get jit code event at "
                  << loc << std::endl;
        exit(1);
    }
    return true;
}

bool DecodeDataEvent::data_loss_event()
{
    DecodeData::DecodeDataType cur_type;
    uint64_t loc;
    if (!_access.next_trace(cur_type, loc) || cur_type != DecodeData::_data_loss)
    {
        std::cerr << "DecodeDataEvent error: Fail to get data loss event at "
                  << loc << std::endl;
        exit(1);
    }
    return true;
}

bool DecodeDataEvent::decode_error_event()
{
    DecodeData::DecodeDataType cur_type;
    uint64_t loc;
    if (!_access.next_trace(cur_type, loc) || cur_type != DecodeData::_decode_error)
    {
        std::cerr << "DecodeDataEvent error: Fail to get decode error event at "
                  << loc << std::endl;
        exit(1);
    }
    return true;
}

bool DecodeDataEvent::remaining()
{
    DecodeData::DecodeDataType temp;
    return _access.current_trace(temp);
}

bool DecodeDataEvent::current_event()
{
    DecodeData::DecodeDataType cur_type;
    if (_pending)
        return true;

    if (!_access.current_trace(cur_type))
        return false;

    if (_type != DecodeData::_bci)
        _pending = true;

    switch(_type)
    {
        case DecodeData::_java_call_begin:
            return java_call_begin_event();
        case DecodeData::_java_call_end:
            return java_call_end_event();
        case DecodeData::_method_entry:
            return method_entry_event();
        case DecodeData::_method_exit:
            return method_exit_event();
        case DecodeData::_method_point:
            return method_point_event("method point");
        case DecodeData::_taken:
            return taken_event();
        case DecodeData::_not_taken:
            return not_taken_event();
        case DecodeData::_switch_case:
            return switch_case_event();
        case DecodeData::_switch_default:
            return switch_default_event();
        case DecodeData::_bci:
        {
            /* A Single bci does not refer to any event, decode error? */
            uint64_t loc;
            _access.next_trace(_type, loc);
            return false;
        }
        case DecodeData::_osr:
            return osr_event();
        case DecodeData::_throw_exception:
            return throw_exception_event();
        case DecodeData::_rethrow_exception:
            return rethrow_exception_event();
        case DecodeData::_handle_exception:
            return handle_exception_event();
        case DecodeData::_ret_code:
            return ret_code_event();
        case DecodeData::_deoptimization:
            return deoptimization_event();
        case DecodeData::_non_invoke_ret:
            return non_invoke_event();
        case DecodeData::_pop_frame:
            return pop_frame_event();
        case DecodeData::_earlyret:
            return earlyret_event();
        case DecodeData::_jit_code:
            return jit_code_event();
        case DecodeData::_data_loss:
            return data_loss_event();
        case DecodeData::_decode_error:
            return decode_error_event();
        default:
            std::cerr << "DecodeDataEvent error: False event" << std::endl;
            exit(1);
    }
}

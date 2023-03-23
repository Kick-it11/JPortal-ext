#ifndef DECODE_DATA_HPP
#define DECODE_DATA_HPP

#include <cassert>
#include <vector>
#include <map>
#include <string>

#include <stdint.h>

class Method;
class JitSection;
/* TraceDataParser will parse bianry data of JPortalTrace.data
 * and extract the PT data, sideband data, JVM runtime data.
 *     It will also split PT data into segments for parallel decoding.
 *
 * PTJVMDecoder will decode splitted PT data
 * and record decoded information in a Java bytecode level
 * Recording results stored in DecodeData
 *     Since PT data is splitted, a single decode result might be incomplete
 *     Following parsing should notice this and do thread-level combination
 *               to get a full trace information
 *
 *
 * Since currenlty PT is supporetd only on x86_64 platform,
 *  Now we ignore aligning data
 */
class DecodeData
{
    friend class DecodeDataRecord;
    friend class DecodeDataAccess;

public:
    struct ThreadSplit
    {
        uint64_t tid;
        uint64_t start_addr;
        uint64_t end_addr;
        uint64_t start_time;
        uint64_t end_time;
        DecodeData *data;

        ThreadSplit(uint64_t _tid, uint64_t _start_addr, uint64_t _start_time, DecodeData *_data) : tid(_tid), start_addr(_start_addr), end_addr(_start_addr),
                                                                                                    start_time(_start_time), end_time(_start_time), data(_data)
        {
            assert(_data != NULL);
        }
    };

    enum DecodeDataType
    {
        _illegal = -1,

        /* java call begin */
        _java_call_begin,

        /* java call end*/
        _java_call_end,

        /* method entry in a java method level, follows a method pointer -> for inter */
        _method_entry,

        /* method exit */
        _method_exit,

        /* method point: in the middle of method: invoke/exception/... */
        _method_point,

        /* taken branch in a bytecode level -> for inter */
        _taken,

        /* untaken branch in a bytecode level -> for inter */
        _not_taken,

        /* switch case */
        _switch_case,

        /* switch default */
        _switch_default,

        /* exception handling or unwwind -> for inter, mostly a pair */
        _bci,

        /* osr */
        _osr,

        /* exception */
        _throw_exception,

        /* rethrow exception */
        _rethrow_exception,

        /* handle exception */
        _handle_exception,

        /* ret or wide ret [jsr] */
        _ret_code,

        /* deoptimization indication */
        _deoptimization,

        /* non invoke ret*/
        _non_invoke_ret,

        /* pop frame */
        _pop_frame,

        /* early ret*/
        _earlyret,

        /* following jit code, JIT description and pcs */
        _jit_code,

        /* pc info */
        _jit_pc_info,

        /* following jit code, with a jit entry */
        _jit_entry,

        /* following jit code, with an osr entry */
        _jit_osr_entry,

        /* jit return */
        _jit_return,

        /* jit exception */
        _jit_exception,

        /* jit unwind */
        _jit_unwind,

        /* jit deopt */
        _jit_deopt,

        /* jit mh deopt */
        _jit_deopt_mh,

        /* indicate a dataloss might happening */
        _data_loss,

        /* indicate a decode error */
        _decode_error,
    };

private:
    const static int initial_data_volume = 1024 * 4;
    uint64_t _id;
    uint8_t *_data_begin;
    uint8_t *_data_end;
    uint64_t _data_volume;

    std::vector<ThreadSplit> _splits;

    void expand_data(uint64_t size);
    void write(void *data, uint64_t size);

    /* disabled functions */
    DecodeData(const DecodeData &data) = delete;
    DecodeData(const DecodeData &&data) = delete;
    DecodeData operator=(DecodeData &data) = delete;

public:
    DecodeData(uint64_t id) : _id(id), _data_begin(nullptr), _data_end(nullptr), _data_volume(0) {}

    ~DecodeData() { delete[] _data_begin; }

    static std::map<uint64_t, std::vector<ThreadSplit>> sort_all_by_time(const std::vector<DecodeData *> &data);
};

class DecodeDataRecord
{
private:
    DecodeData *const _data;
    DecodeData::ThreadSplit *_cur_thread;
    uint8_t _type;
    int _section_id;

public:
    DecodeDataRecord(DecodeData *data) : _data(data),
                                         _cur_thread(nullptr),
                                         _type(DecodeData::_illegal),
                                         _section_id(-1)
    {
        assert(data != nullptr);
    }
    ~DecodeDataRecord()
    {
        /* mark end has been set */
        assert(_cur_thread == nullptr);
        _type = DecodeData::_illegal;
    }

    uint64_t pos() { return _data->_data_end - _data->_data_begin; }

    /* must be called before record other information, to set thread info */
    void switch_in(uint64_t tid, uint64_t time);

    void switch_out(uint64_t time);

    bool record_method_entry(const Method *method);

    bool record_method_exit(const Method *method);

    bool record_method_point(const Method *method);

    bool record_branch_taken();

    bool record_branch_not_taken();

    bool record_switch_case(int index);

    bool record_switch_default();

    bool record_bci(int bci);

    bool record_ret_code();

    bool record_deoptimization();

    bool record_throw_exception();

    bool record_rethrow_exception();

    bool record_handle_exception();

    bool record_pop_frame();

    bool record_earlyret();

    bool record_non_invoke_ret();

    bool record_osr();

    bool record_java_call_begin();

    bool record_java_call_end();

    bool record_jit_entry(const JitSection *section);

    bool record_jit_osr_entry(const JitSection *section);

    bool record_jit_pc_info(const JitSection *section, int ind);

    bool record_jit_return(const JitSection *section);

    bool record_jit_exception(const JitSection *section);

    bool record_jit_unwind(const JitSection *section);

    bool record_jit_deopt(const JitSection *section);

    bool record_jit_deopt_mh(const JitSection *section);

    bool record_data_loss();

    bool record_decode_error();

    uint64_t id() const { return _data->_id; }
};

class DecodeDataAccess
{
private:
    DecodeData *_data;
    const uint8_t *_current;
    const uint8_t *_terminal;

    int _idx;
    std::vector<DecodeData::ThreadSplit> _splits;

    DecodeDataAccess(const DecodeDataAccess &) = delete;
public:
    DecodeDataAccess(DecodeData *data)
    {
        assert(data != NULL);
        _data = data;
        _current = _data->_data_begin;
        _terminal = _data->_data_end;
        _idx = 0;
    }

    DecodeDataAccess(const DecodeData::ThreadSplit &split)
    {
        assert(split.data != NULL);
        _data = split.data;
        _current = _data->_data_begin + split.start_addr;
        _terminal = _data->_data_begin + split.end_addr;
        _idx = 0;
    }

    DecodeDataAccess(const std::vector<DecodeData::ThreadSplit> &splits)
    {
        _splits = splits;
        _idx = 0;
        if (_idx != splits.size())
        {
            _data = _splits[_idx].data;
            _current = _data->_data_begin + _splits[_idx].start_addr;
            _terminal = _data->_data_begin + _splits[_idx].end_addr;
            ++_idx;
        }
        else
        {
            _data = nullptr;
            _current = nullptr;
            _terminal = nullptr;
        }
    }

    /* get trace and move on */
    bool next_trace(DecodeData::DecodeDataType &type, uint64_t &pos);

    /* get current trace */
    bool current_trace(DecodeData::DecodeDataType &type);

    bool get_method(uint64_t pos, const Method *&method);

    bool get_switch_case_index(uint64_t pos, int &index);

    bool get_bci(uint64_t pos, int &bci);

    /* Note that pcs might < 0, if so: it is not a index in pc, but entry/osrentry/....*/
    bool get_jit_code(uint64_t pos, const JitSection *&section, std::vector<int> &pcs);
};

class DecodeDataEvent {
private:
    DecodeDataAccess _access;

    /* if pending, current_event will not continue to get next, after set_handled it will */
    bool _pending;

    DecodeData::DecodeDataType _type;
    const Method *_method;
    int _bci_or_ind;
    const JitSection *_section;
    std::vector<int> _pcs;

    /* DecodeData::DecodeDataType can be used for event
     *   _java_call_begin
     *   _java_call_end
     *   _method_entry
     *   _method_exit
     *   _method_point
     *   _taken
     *   _not_taken
     *   _switch_case
     *   _switch_default
     *   _osr
     *   _throw_exception
     *   _rethrow_exception
     *   _handle_exception
     *   _ret_code
     *   _deoptimization
     *   _non_invoke_ret
     *   _pop_frame
     *   _earlyret
     *   _jit_code
     *   _data_loss
     *   _decode_error
     */

    bool method_entry_event();
    bool method_exit_event();
    bool method_point_event(std::string fevent);
    bool taken_event();
    bool not_taken_event();
    bool switch_case_event();
    bool switch_default_event();
    bool ret_code_event();
    bool deoptimization_event();
    bool throw_exception_event();
    bool rethrow_exception_event();
    bool handle_exception_event();
    bool pop_frame_event();
    bool earlyret_event();
    bool non_invoke_event();
    bool osr_event();
    bool java_call_begin_event();
    bool java_call_end_event();
    bool jit_code_event();
    bool data_loss_event();
    bool decode_error_event();

    DecodeDataEvent(const DecodeDataEvent &) = delete;

public:
    DecodeDataEvent(DecodeData *data) :
        _access(data), _pending(false), _type(DecodeData::_illegal),
        _method(nullptr), _bci_or_ind(-1), _section(nullptr)
    {
    }

    DecodeDataEvent(const DecodeData::ThreadSplit &split) :
        _access(split), _pending(false), _type(DecodeData::_illegal),
        _method(nullptr), _bci_or_ind(-1), _section(nullptr)
    {
    }

    DecodeDataEvent(const std::vector<DecodeData::ThreadSplit> &splits) :
        _access(splits), _pending(false), _type(DecodeData::_illegal),
        _method(nullptr), _bci_or_ind(-1), _section(nullptr)
    {
    }

    bool remaining();

    bool pending() { return _pending; }
    /* query curent event, if not pending, query next;
     * Since bci etc event is not into event, so even if it returns false
     *       There might still have decode data remaining to be processed
     */
    bool current_event();

    void set_unpending() { _pending = false; }

    DecodeData::DecodeDataType type() { return _pending ? _type : DecodeData::_illegal; }

    const Method *method() { return _pending ? _method: nullptr; }

    int bci_or_ind() { return _pending ? _bci_or_ind : -1; }

    const JitSection *section() { return _pending ? _section : nullptr; }

    std::vector<int> &pcs() { if (!_pending) _pcs.clear();  return _pcs; }
};

#endif /* DECODE_DATA_HPP */

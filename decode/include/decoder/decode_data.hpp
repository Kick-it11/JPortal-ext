#ifndef DECODE_DATA_HPP
#define DECODE_DATA_HPP

#include "java/bytecodes.hpp"
#include "runtime/jit_section.hpp"
#include "runtime/jvm_runtime.hpp"

#include <cassert>
#include <vector>

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

        /* method entry in a java method level, follows a method pointer -> for inter */
        _method_entry = 0,

        /* method exit */
        _method_exit = 1,

        /* method point: in the middle of method: invoke/exception/... */
        _method_point = 2,

        /* taken branch in a bytecode level -> for inter */
        _taken = 3,

        /* untaken branch in a bytecode level -> for inter */
        _not_taken = 4,

        /* switch case */
        _switch_case = 5,

        /* switch default */
        _switch_default = 6,

        /* exception handling or unwwind -> for inter, mostly a pair */
        _bci = 7,

        /* ret or wide ret [jsr] */
        _ret_code = 8,

        /* deoptimization indication */
        _deoptimization = 9,

        /* exception */
        _throw_exception = 10,

        /* pop frame */
        _pop_frame = 11,

        /* early ret*/
        _earlyret = 12,

        /* non invoke ret*/
        _non_invoke_ret = 13,

        /* java call begin */
        _java_call_begin = 14,

        /* java call end*/
        _java_call_end = 15,

        /* following jit code, with a jit entry */
        _jit_entry = 16,

        /* following jit code, with an osr entry */
        _jit_osr_entry = 17,

        /* following jit code, JIT description and pcs */
        _jit_code = 18,

        /* pc info */
        _jit_pc_info = 19,

        /* jit return */
        _jit_return = 20,

        /* java_*/
        /* indicate a dataloss might happening */
        _data_loss = 21,

        /* indicate a decode error */
        _decode_error = 22,
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

    bool record_method_entry(int method_id);

    bool record_method_exit(int method_id);

    bool record_method_point(int method_id);

    bool record_branch_taken();

    bool record_branch_not_taken();

    bool record_switch_case(int index);

    bool record_switch_default();

    bool record_bci(int bci);

    bool record_ret_code();

    bool record_deoptimization();

    bool record_throw_exception();

    bool record_pop_frame();

    bool record_earlyret();

    bool record_non_invoke_ret();

    bool record_java_call_begin();

    bool record_java_call_end();

    bool record_jit_entry(int section_id);

    bool record_jit_osr_entry(int section_id);

    bool record_jit_code(int section_id, int idx);

    bool record_jit_return();

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

    std::vector<DecodeData::ThreadSplit>::iterator _it;
    std::vector<DecodeData::ThreadSplit> _splits;

public:
    DecodeDataAccess(DecodeData *data)
    {
        assert(data != NULL);
        _data = data;
        _current = _data->_data_begin;
        _terminal = _data->_data_end;
        _it = _splits.end();
    }

    DecodeDataAccess(const DecodeData::ThreadSplit &split)
    {
        assert(split.data != NULL);
        _data = split.data;
        _current = _data->_data_begin + split.start_addr;
        _terminal = _data->_data_begin + split.end_addr;
        _it = _splits.end();
    }

    DecodeDataAccess(const std::vector<DecodeData::ThreadSplit> &splits)
    {
        _splits = splits;
        _it = _splits.begin();
        if (_it != _splits.end())
        {
            _data = _it->data;
            _current = _data->_data_begin + _it->start_addr;
            _terminal = _data->_data_begin + _it->end_addr;
            ++_it;
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

    bool get_method_entry(uint64_t pos, int &method_id);
    bool get_method_exit(uint64_t pos, int &method_id);
    bool get_method_point(uint64_t pos, int &method_id);

    bool get_switch_case_index(uint64_t pos, int &index);

    bool get_bci(uint64_t pos, int &bci);

    bool get_jit_code(uint64_t pos, int &section_id, std::vector<int> &pcs);

    uint64_t pos() const { return _current - _data->_data_begin; }

    uint64_t id() const { return _data->_id; }
};

#endif /* DECODE_DATA_HPP */

#ifndef DECODE_DATA_HPP
#define DECODE_DATA_HPP

#include "java/bytecodes.hpp"
#include "runtime/jit_section.hpp"
#include "runtime/jvm_runtime.hpp"

#include <cassert>
#include <vector>

class JitSection;
class Method;
struct PCStackInfo;

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

        /* 0, for aligning */
        _padding = 0,

        /* method entry in a java method level, follows a method pointer -> for inter */
        _method_entry = 1,

        /* method exit: return/ exception/ pop frame */
        _method_exit = 2,

        /* taken branch in a bytecode level -> for inter */
        _taken = 3,

        /* untaken branch in a bytecode level -> for inter*/
        _not_taken = 4,

        /* switch case */
        _switch_case = 5,

        /* switch default */
        _switch_default = 6,

        /* invoke site */
        _invoke_site = 7,

        /* exception handling or unwwind -> for inter, mostly a pair */
        _bci = 8,

        /* deoptimization -> for inter */
        _deoptimization = 9,

        /* following jit code, with a jit entry */
        _jit_entry = 10,

        /* following jit code, with an osr entry */
        _jit_osr_entry = 11,

        /* following jit code, JIT description and pcs */
        _jit_code = 12,

        /* jit return */
        _jit_return = 13,

        /* indicate a dataloss might happening */
        _data_loss = 14,

        /* indicate a decode error */
        _decode_error = 15,
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

    const JitSection *_cur_section;
    uint64_t _pc_size;
    uint64_t _pc_size_pos;

public:
    DecodeDataRecord(DecodeData *data) : _data(data),
                                         _cur_thread(nullptr),
                                         _type(DecodeData::_illegal),
                                         _cur_section(nullptr),
                                         _pc_size(0),
                                         _pc_size_pos(0)
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

    bool record_method_exit();

    bool record_branch_taken();

    bool record_branch_not_taken();

    bool record_switch_case(int index);

    bool record_switch_default();

    bool record_invoke_site();

    bool record_bci(int bci);

    bool record_deoptimization(const Method *method, int bci, uint8_t use_next_bci, uint8_t is_bottom_frame);

    bool record_jit_entry(const JitSection *section);

    bool record_jit_osr_entry(const JitSection *section);

    bool record_jit_code(const JitSection *section, const PCStackInfo *info);

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

public:
    DecodeDataAccess(DecodeData *data)
    {
        assert(data != NULL);
        _data = data;
        _current = _data->_data_begin;
        _terminal = _data->_data_end;
    }

    DecodeDataAccess(const DecodeData::ThreadSplit &split)
    {
        assert(split.data != NULL);
        _data = split.data;
        _current = _data->_data_begin + split.start_addr;
        _terminal = _data->_data_begin + split.end_addr;
    }

    bool next_trace(DecodeData::DecodeDataType &type, uint64_t &pos);

    bool get_method_entry(uint64_t pos, const Method *&method);

    bool get_switch_case_index(uint64_t pos, int &index);

    bool get_bci(uint64_t pos, int &bci);

    bool get_deoptimization(uint64_t pos, const Method *&method, int &bci, uint8_t &use_next_bci, uint8_t &is_bottom_frame);

    bool get_jit_code(uint64_t pos, const JitSection *&section, const PCStackInfo **&pcs, uint64_t &size);

    uint64_t pos() const { return _current - _data->_data_begin; }

    uint64_t id() const { return _data->_id; }
};

#endif /* DECODE_DATA_HPP */

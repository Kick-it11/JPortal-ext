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

        /* taken branch in a bytecode level -> for inter */
        _taken = 2,

        /* untaken branch in a bytecode level -> for inter*/
        _not_taken = 1,

        /* method entry in a java method level, follows a method pointer -> for inter */
        _method_entry = 3,

        /* exception handling or unwwind -> for inter */
        _exception = 4,

        /* deoptimization -> for inter */
        _deoptimization = 5,

        /* following jit code, with a jit entry */
        _jit_entry = 6,

        /* following jit code, with an osr entry */
        _jit_osr_entry = 7,

        /* following jit code, JIT description and pcs */
        _jit_code = 8,

        /* jit exception begin */
        _jit_exception = 9,

        /* jit deopt */
        _jit_deopt = 10,

        /* jit deopt mh */
        _jit_deopt_mh = 11,

        /* indicate a dataloss might happening */
        _data_loss = 12,

        /* indicate a decode error */
        _decode_error = 13,
    };

private:
    const static int initial_data_volume = 1024 * 4;
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
    DecodeData() : _data_begin(nullptr), _data_end(nullptr), _data_volume(0) {}

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
        assert(_cur_thread != nullptr);
        _type = DecodeData::_illegal;
    }

    uint64_t pos() { return _data->_data_end - _data->_data_begin; }
    /* must be called before record other information, to set thread info */
    void record_switch(uint64_t tid, uint64_t time);

    /* end of record */
    void record_mark_end(uint64_t time);

    void record_method_entry(const Method *method);

    void record_branch_taken();

    void record_branch_not_taken();

    void record_exception_handling(const Method *method, int current_bci, int handler_bci);

    void record_deoptimization(const Method *method, int bci);

    void record_jit_entry(const JitSection *section);

    void record_jit_osr_entry(const JitSection *section);

    void record_jit_code(const JitSection *section, const PCStackInfo *info);

    void record_jit_exception();

    void record_jit_deopt();

    void record_jit_deopt_mh();

    void record_data_loss();

    void record_decode_error();
};

class DecodeDataAccess
{
private:
    DecodeData *_data;
    const uint8_t *_current;
    const uint8_t *_terminal;

public:
    DecodeDataAccess(DecodeData *data) : _data(data)
    {
        assert(data != NULL);
        _current = _data->_data_begin;
        _terminal = _data->_data_end;
    }

    bool next_trace(DecodeData::DecodeDataType &type, uint64_t &pos);

    bool get_method_entry(uint64_t pos, const Method *&method);

    bool get_exception_handling(uint64_t pos, const Method *&method, int &current_bci, int &handler_bci);

    bool get_deoptimization(uint64_t pos, const Method *&method, int &bci);

    bool get_jit_code(uint64_t pos, const JitSection *&section, const PCStackInfo **&pcs, uint64_t &size);
};

#endif /* DECODE_DATA_HPP */

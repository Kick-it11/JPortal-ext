#ifndef DECODE_RESULT_HPP
#define DECODE_RESULT_HPP

#include "java/bytecodes.hpp"
#include "runtime/jit_section.hpp"
#include "runtime/jvm_runtime.hpp"

#include <list>
#include <unordered_map>

class Method;

struct InterRecord
{
    uint64_t size;
    InterRecord() : size(0) {}
};

struct JitRecord
{
    uint64_t size;
    const JitSection *section;
    JitRecord(const JitSection *_section) : size(0), section(_section) {}
};

struct ThreadSplit
{
    uint16_t head_loss; /* 0 for not loss, 1 for head loss */
    uint16_t tail_loss; /* 0 for not loss, 1 for head loss */
    uint32_t tid;
    uint64_t start_addr;
    uint64_t end_addr;
    uint64_t start_time;
    uint64_t end_time;
    ThreadSplit(uint32_t _tid, uint64_t _start_addr, uint64_t _start_time) : head_loss(0), tail_loss(0), tid(_tid), start_addr(_start_addr),
                                                                             end_addr(_start_addr), start_time(_start_time), end_time(_start_time) {}
};

class TraceData
{
    friend class TraceDataRecord;
    friend class TraceDataAccess;

public:
private:
    const static int initial_data_volume = 1024 * 4;
    uint8_t *data_begin;
    uint8_t *data_end;
    uint64_t data_volume;

    /** indicate a method entry here; */
    std::unordered_map<uint64_t, const Method *> method_info;

    /** thread split */
    std::unordered_map<uint32_t, std::list<ThreadSplit>> thread_map;

    void expand_data(uint64_t size);
    void write(void *data, uint64_t size);

public:
    TraceData() : data_begin(nullptr), data_end(nullptr), data_volume(0) {}

    ~TraceData() { delete[] data_begin; }

    std::unordered_map<uint32_t, std::list<ThreadSplit>> &get_thread_map() { return thread_map; }

    const Method *get_method_info(uint64_t loc);

    bool get_inter(uint64_t loc, const uint8_t *&codes, uint64_t &size);

    bool get_jit(uint64_t loc, const PCStackInfo **&codes, uint64_t &size, const JitSection *&section);
};

class TraceDataRecord
{
private:
    TraceData &trace;
    JVMRuntime::Codelet codelet_type = JVMRuntime::_illegal;
    uint64_t loc;
    uint64_t current_time = 0;
    ThreadSplit *thread = nullptr;
    const JitSection *last_section = nullptr;
    Bytecodes::Code last_bytecode = Bytecodes::_illegal;

public:
    TraceDataRecord(TraceData &_trace) : trace(_trace) {}

    void add_bytecode(uint64_t time, Bytecodes::Code bytecode);

    void add_jitcode(uint64_t time, const JitSection *section, PCStackInfo *pc, uint64_t entry);

    void add_codelet(uint64_t time, JVMRuntime::Codelet codelet);

    void add_method_info(const Method *method);

    /** Should always be called at leave */
    void switch_out(bool loss);

    /** Should always be called at start */
    void switch_in(uint32_t tid, uint64_t time, bool loss);

    uint64_t get_loc() { return loc; }
};

class TraceDataAccess
{
private:
    TraceData &trace;
    const uint8_t *current;
    const uint8_t *terminal;

public:
    TraceDataAccess(TraceData &_trace) : trace(_trace)
    {
        current = trace.data_begin;
        terminal = trace.data_end;
    }
    TraceDataAccess(TraceData &_trace, uint64_t begin) : trace(_trace)
    {
        current = trace.data_begin + begin;
        if (current < trace.data_begin)
            current = trace.data_end;
        terminal = trace.data_end;
    }
    TraceDataAccess(TraceData &_trace, uint64_t begin, uint64_t end) : trace(_trace)
    {
        current = trace.data_begin + begin;
        terminal = trace.data_begin + end;
        if (current < trace.data_begin)
            current = terminal;
        if (terminal > trace.data_end)
            terminal = trace.data_end;
    }
    bool next_trace(JVMRuntime::Codelet &codelet, uint64_t &loc);

    void set_current(uint64_t addr)
    {
        current = trace.data_begin + addr;
        if (current < trace.data_begin)
            current = trace.data_end;
    }

    uint64_t get_current()
    {
        return current - trace.data_begin;
    }

    bool end()
    {
        return current >= terminal;
    }

    uint64_t get_end()
    {
        return terminal - trace.data_begin;
    }
};

#endif /* DECODE_RESULT_HPP */

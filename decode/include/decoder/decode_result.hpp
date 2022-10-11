#ifndef DECODE_RESULT
#define DECODE_RESULT

#include "utilities/definitions.hpp"
#include "java/bytecodes.hpp"
#include "runtime/codelets_entry.hpp"
#include "runtime/jit_section.hpp"

#include <unordered_map>
#include <stack>
#include <list>
#include <vector>

using std::unordered_map;
using std::list;

class Method;

struct InterRecord {
    u8 size = 0;
};

struct JitRecord {
    u8 size = 0;
    const JitSection *section = nullptr;
    JitRecord(const JitSection *_section) :
      section(_section) {}
};

struct ThreadSplit {
    long tid;
    size_t start_addr;
    size_t end_addr;
    u8 start_time;
    u8 end_time;
    bool head_loss = false;
    bool tail_loss = false;
    ThreadSplit(long _tid, size_t _start_addr, u8 _start_time):
        tid(_tid), start_addr(_start_addr), end_addr(-1l),
        start_time(_start_time), end_time(_start_time) {}
};

class TraceData {
  friend class TraceDataRecord;
  friend class TraceDataAccess;
  public:
  private:
    const int initial_data_volume = 1024 * 1024;
    u1 *data_begin = nullptr;
    u1 *data_end = nullptr;
    size_t data_volume = 0;
    unordered_map<size_t, const Method*> method_info;

    unordered_map<long, list<ThreadSplit>> thread_map;

    int expand_data(size_t size);
    int write(void *data, size_t size);

  public:
    ~TraceData() {
      free(data_begin);
    }

    unordered_map<long, list<ThreadSplit>> &get_thread_map() { return thread_map; }

    const Method* get_method_info(size_t loc);

    bool get_inter(size_t loc, const u1* &codes, size_t &size);

    bool get_jit(size_t loc, const PCStackInfo**&codes, size_t &size, const JitSection *&section);

    void output();
};

class TraceDataRecord {
  private:
    TraceData &trace;
    CodeletsEntry::Codelet codelet_type = CodeletsEntry::_illegal;
    size_t loc;
    u8 current_time = 0;
    ThreadSplit *thread= nullptr;
    const JitSection *last_section = nullptr;
    Bytecodes::Code last_bytecode = Bytecodes::_illegal;
  public:
    TraceDataRecord(TraceData &_trace) :
        trace(_trace) {}

    int add_bytecode(u8 time, Bytecodes::Code bytecode);

    int add_jitcode(u8 time, const JitSection *section, PCStackInfo *pc, u8 entry);

    int add_codelet(CodeletsEntry::Codelet codelet);

    void add_method_info(const Method* method);

    void switch_out(bool loss);

    void switch_in(long tid, u8 time, bool loss);

    size_t get_loc() { return loc; }
};

class TraceDataAccess {
  private:
    TraceData &trace;
    const u1* current;
    const u1* terminal;
  public:
    TraceDataAccess(TraceData &_trace) :
        trace(_trace) {
        current = trace.data_begin;
        terminal = trace.data_end;
    }
    TraceDataAccess(TraceData &_trace, size_t begin) :
        trace(_trace) {
        current = trace.data_begin + begin;
        if (current < trace.data_begin)
            current = trace.data_end;
        terminal = trace.data_end;
    }
    TraceDataAccess(TraceData &_trace, size_t begin, size_t end) :
        trace(_trace) {
        current = trace.data_begin + begin;
        terminal = trace.data_begin + end;
        if (current < trace.data_begin)
            current = terminal;
        if (terminal > trace.data_end)
            terminal = trace.data_end;
    }
    bool next_trace(CodeletsEntry::Codelet &codelet, size_t &loc);

    void set_current(size_t addr) {
        current = trace.data_begin + addr;
        if (current < trace.data_begin)
            current = trace.data_end;
    }

    size_t get_current() {
        return current - trace.data_begin;
    }

    bool end() {
        return current >= terminal;
    }

    size_t get_end() {
        return terminal - trace.data_begin;
    }
};

#endif

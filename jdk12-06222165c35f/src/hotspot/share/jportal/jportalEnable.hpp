#ifndef SHARE_JPORTAL_JPORTALENABLE_HPP
#define SHARE_JPORTAL_JPORTALENABLE_HPP

#include "code/nmethod.hpp"
#include "interpreter/templateInterpreter.hpp"

#ifdef JPORTAL_ENABLE
class JavaThread;
class JPortalEnable {
  private:

    static bool   _initialized;     // indicate JPortal is initialized
    static bool   _tracing;         // indicate JPortal is initialized

    // shared memory related info
    static int             _shm_id;          // shared memory identifier
    static address         _shm_addr;        // start address of shared memory

  public:
    struct ShmHeader {
      volatile u8 data_head;
      volatile u8 data_tail;
      u8          data_size;
    };

    // JVM runtime dump type
    enum DumpType {
      _java_call_begin_info,          // JavaCalls::call() begin
      _java_call_end_info,            // JavaCalls::call() end
      _method_info,                   // method info
      _branch_taken_info,             // branch taken
      _branch_not_taken_info,         // branch not taken
      _switch_table_stub_info,        // jportal table stub
      _switch_default_info,           // switch default
      _bci_table_stub_info,           // jportal table stub,
      _osr_info,                      // online stack replacement
      _throw_exception_info,          // indicate throwing a exception
      _rethrow_exception_info,        // indicate rethrowing a exception
      _handle_exception_info,         // to exception handler
      _ret_code_info,                 // indicate a ret or wide ret [jsr/ret]
      _deoptimization_info,           // indicate a deoptimization
      _non_invoke_ret_info,           // non invoke ret, such as deoptimization
      _pop_frame_info,                // indicate a pop frame
      _earlyret_info,                 // indicate an early ret
      _compiled_method_load_info,     // after loading a compiled method: entry, codes, scopes data etc included
      _compiled_method_unload_info,   // after unloading a compiled method
      _thread_start_info,             // a thread begins, map between system tid and java tid
      _inline_cache_add_info,         // inline cache: a map between a source ip to a destination ip
      _inline_cache_clear_info,       // inline cache clear: delete the map
    };

    struct DumpInfo {
      u4 type;
      u4 size;
      u8 time;
    };

    struct JavaCallBeginInfo {
      struct DumpInfo info;
      u8 addr;
      JavaCallBeginInfo(u8 _addr, u4 _size) : addr(_addr) {
        info.type = _java_call_begin_info;
        info.size = _size;
        info.time = get_timestamp();
      }
    };

    struct JavaCallEndInfo {
      struct DumpInfo info;
      u8 addr;
      JavaCallEndInfo(u8 _addr, u4 _size) : addr(_addr) {
        info.type = _java_call_end_info;
        info.size = _size;
        info.time = get_timestamp();
      }
    };

    struct MethodInfo {
      struct DumpInfo info;
      u4 klass_name_length;
      u4 method_name_length;
      u4 method_signature_length;
      u4 _pending;
      u8 addr1;
      u8 addr2; // JPortalMethod: exit
      u8 addr3; // JPortal: method point

      MethodInfo(u4 _klass_name_length,
                 u4 _method_name_length,
                 u4 _method_signature_length,
                 u8 _addr1, u8 _addr2, u8 _addr3, u4 _size) :
        klass_name_length(_klass_name_length),
        method_name_length(_method_name_length),
        method_signature_length(_method_signature_length),
        addr1(_addr1), addr2(_addr2), addr3(_addr3) {
        info.type = _method_info;
        info.size = _size;
        info.time = get_timestamp();
      }
    };

    struct BranchTakenInfo {
      struct DumpInfo info;
      u8 addr;
      BranchTakenInfo(u8 _addr, u4 _size) : addr(_addr) {
        info.type = _branch_taken_info;
        info.size = _size;
        info.time = get_timestamp();
      }
    };

    struct BranchNotTakenInfo {
      struct DumpInfo info;
      u8 addr;
      BranchNotTakenInfo(u8 _addr, u4 _size) : addr(_addr) {
        info.type = _branch_not_taken_info;
        info.size = _size;
        info.time = get_timestamp();
      }
    };

    struct SwitchTableStubInfo {
      struct DumpInfo info;
      u8 addr;
      u4 num;
      u4 ssize;
      SwitchTableStubInfo(u8 _addr, u8 _num, u4 _ssize, u4 _size)
        : addr(_addr), num(_num), ssize(_ssize) {
        info.type = _switch_table_stub_info;
        info.size = _size;
        info.time = get_timestamp();
      }
    };

    struct SwitchDefaultInfo {
      struct DumpInfo info;
      u8 addr;

      SwitchDefaultInfo(u8 _addr, u4 _size) : addr(_addr) {
        info.type = _switch_default_info;
        info.size = _size;
        info.time = get_timestamp();
      }
    };

    struct BciTableStubInfo {
      struct DumpInfo info;
      u8 addr;
      u4 num;
      u4 ssize;
      BciTableStubInfo(u8 _addr, u8 _num, u4 _ssize, u4 _size)
        : addr(_addr), num(_num), ssize(_ssize) {
        info.type = _bci_table_stub_info;
        info.size = _size;
        info.time = get_timestamp();
      }
    };

    struct OSRInfo {
      struct DumpInfo info;
      u8 addr;
      OSRInfo(u8 _addr, u4 _size) : addr(_addr) {
        info.type = _osr_info;
        info.size = _size;
        info.time = get_timestamp();
      }
    };

    struct ThrowExceptionInfo {
      struct DumpInfo info;
      u8 addr;
      ThrowExceptionInfo(u8 _addr, u4 _size) : addr(_addr) {
        info.type = _throw_exception_info;
        info.size = _size;
        info.time = get_timestamp();
      }
    };

    struct RethrowExceptionInfo {
      struct DumpInfo info;
      u8 addr;
      RethrowExceptionInfo(u8 _addr, u4 _size) : addr(_addr) {
        info.type = _rethrow_exception_info;
        info.size = _size;
        info.time = get_timestamp();
      }
    };

    struct HandleExceptionInfo {
      struct DumpInfo info;
      u8 addr;
      HandleExceptionInfo(u8 _addr, u4 _size) : addr(_addr) {
        info.type = _handle_exception_info;
        info.size = _size;
        info.time = get_timestamp();
      }
    };

    struct RetCodeInfo {
      struct DumpInfo info;
      u8 addr;
      RetCodeInfo(u8 _addr, u4 _size) : addr(_addr) {
        info.type = _ret_code_info;
        info.size = _size;
        info.time = get_timestamp();
      }
    };

    struct DeoptimizationInfo {
      struct DumpInfo info;
      u8 addr;
      DeoptimizationInfo(u8 _addr, u4 _size) : addr(_addr) {
        info.type = _deoptimization_info;
        info.size = _size;
        info.time = get_timestamp();
      }
    };

    struct NonInvokeRetInfo {
      struct DumpInfo info;
      u8 addr;
      NonInvokeRetInfo(u8 _addr, u4 _size) : addr(_addr) {
        info.type = _non_invoke_ret_info;
        info.size = _size;
        info.time = get_timestamp();
      }
    };

    struct PopFrameInfo {
      struct DumpInfo info;
      u8 addr;
      PopFrameInfo(u8 _addr, u4 _size) : addr(_addr) {
        info.type = _pop_frame_info;
        info.size = _size;
        info.time = get_timestamp();
      }
    };

    struct EarlyretInfo {
      struct DumpInfo info;
      u8 addr;
      EarlyretInfo(u8 _addr, u4 _size) : addr(_addr) {
        info.type = _earlyret_info;
        info.size = _size;
        info.time = get_timestamp();
      }
    };

    struct CompiledMethodLoadInfo {
      struct DumpInfo info;
      u8 code_begin;
      u8 stub_begin;
      u8 entry_point;
      u8 verified_entry_point;
      u8 osr_entry_point;
      u8 exception_begin;
      u8 unwind_begin;
      u8 deopt_begin;
      u8 deopt_mh_begin;
      u4 inline_method_cnt;
      u4 code_size;
      u4 scopes_pc_size;
      u4 scopes_data_size;

      CompiledMethodLoadInfo(u8 _code_begin, u8 _stub_begin, u8 _entry_point,
                             u8 _verified_entry_point, u8 _osr_entry_point,
                             u8 _exception_begin, u8 _unwind_begin,
                             u8 _deopt_begin, u8 _deopt_mh_begin,
                             u4 _inline_method_cnt, u4 _code_size,
                             u4 _scopes_pc_size, u4 _scopes_data_size, u4 _size)
                     : code_begin(_code_begin),
                       stub_begin(_stub_begin),
                       entry_point(_entry_point),
                       verified_entry_point(_verified_entry_point),
                       osr_entry_point(_osr_entry_point),
                       exception_begin(_exception_begin),
                       unwind_begin(_unwind_begin),
                       deopt_begin(_deopt_begin),
                       deopt_mh_begin(_deopt_mh_begin),
                       inline_method_cnt(_inline_method_cnt),
                       code_size(_code_size),
                       scopes_pc_size(_scopes_pc_size),
                       scopes_data_size(_scopes_data_size) {
        info.type = _compiled_method_load_info;
        info.size = _size;
        info.time = get_timestamp();
      }
    };

    struct CompiledMethodUnloadInfo {
      struct DumpInfo info;
      u8 code_begin;

      CompiledMethodUnloadInfo(u8 _code_begin, u8 _size) : code_begin(_code_begin) {
        info.type = _compiled_method_unload_info;
        info.size = _size;
        info.time = get_timestamp();
      }
    };

    struct InlineMethodInfo {
      u4 klass_name_length;
      u4 method_name_length;
      u4 method_signature_length;
      u4 method_index;

      InlineMethodInfo(u4 _klass_name_length,
                       u4 _method_name_length,
                       u4 _method_signature_length,
                       u4 _method_index) :
                 klass_name_length(_klass_name_length),
                 method_name_length(_method_name_length),
                 method_signature_length(_method_signature_length),
                 method_index(_method_index) {}
    };

    struct ThreadStartInfo {
      struct DumpInfo info;
      u8 java_tid;
      u8 sys_tid;

      ThreadStartInfo(u8 _java_tid, u8 _sys_tid, u4 _size) : java_tid(_java_tid), sys_tid(_sys_tid) {
        info.type = _thread_start_info;
        info.size = _size;
        info.time = get_timestamp();
      }
    };

    struct InlineCacheAddInfo {
      struct DumpInfo info;
      u8 src;
      u8 dest;

      InlineCacheAddInfo(u8 _src, u8 _dest, u4 size) : src(_src), dest(_dest) {
        info.type = _inline_cache_add_info;
        info.size = size;
        info.time = get_timestamp();
      }
    };

    struct InlineCacheClearInfo {
      struct DumpInfo info;
      u8 src;

      InlineCacheClearInfo(u8 _src, u4 size) : src(_src) {
        info.type = _inline_cache_clear_info;
        info.size = size;
        info.time = get_timestamp();
      }
    };

    inline static u8 get_timestamp() {
      unsigned int low, high;
      asm volatile("rdtsc" : "=a" (low), "=d" (high));
      return low | ((u8)high) << 32;
    }

    inline static u8 get_java_tid(JavaThread* thread);

    // check data size
    inline static bool check_data(u4 size);

    // write data to shared memory, ensure has sapce before
    inline static void dump_data(address src, u4 size);

    // initialize shared memory, methoda array, etc & begin to trace...
    static void init();

    // destroy shared memory, method array, etc...
    static void destroy();

    static void trace();

    static void dump_java_call_begin(address src);

    static void dump_java_call_end(address src);

    static void dump_method(Method *moop);

    static void dump_branch_taken(address addr);

    static void dump_branch_not_taken(address addr);

    static void dump_switch_table_stub(address addr, u4 num, u4 ssize);

    static void dump_switch_default(address addr);

    static void dump_bci_table_stub(address addr, u4 num, u4 ssize);

    static void dump_osr(address);

    static void dump_throw_exception(address src);

    static void dump_rethrow_exception(address src);

    static void dump_handle_exception(address src);

    static void dump_ret_code(address addr);

    static void dump_deoptimization(address src);

    static void dump_non_invoke_ret(address src);

    static void dump_pop_frame(address src);

    static void dump_earlyret(address src);

    static void dump_compiled_method_load(Method *moop, nmethod *nm);

    static void dump_compiled_method_unload(Method *moop, nmethod *nm);

    static void dump_thread_start(JavaThread *thread);

    static void dump_inline_cache_add(address src, address dest);

    static void dump_inline_cache_clear(address src);
};
#endif

#endif

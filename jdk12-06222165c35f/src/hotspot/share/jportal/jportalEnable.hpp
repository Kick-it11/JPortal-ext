#ifndef SHARE_JPORTAL_JPORTALENABLE_HPP
#define SHARE_JPORTAL_JPORTALENABLE_HPP

#include "code/nmethod.hpp"
#include "interpreter/templateInterpreter.hpp"

class JavaThread;
class JPortalEnable {
  private:
    // const static int number_of_states = 10;
    // const static int number_of_return_entries = 6;
    // const static int number_of_return_addrs = 10;
    // const static int number_of_method_entries = 34;
    // const static int number_of_result_handlers = 10;
    // const static int number_of_deopt_entries = 7;
    // const static int dispatch_length = 256;
    // const static int number_of_codes = 239;

    static bool      _initialized;     // indicate JPortal is initialized

    // shared memory related info
    static int       _shm_id;          // shared memory identifier
    static address   _shm_addr;        // start address of shared memory

    static GrowableArray<Method *> *_method_array;  // method array

  public:
    struct ShmHeader {
      volatile u8 data_head;
      volatile u8 data_tail;
      u8          data_size;
    };

    // JVM runtime dump type
    enum DumpType {
      _illegal = -1,
      _method_entry_initial,     // first method entry: map between idx to method signature
      _method_entry,             // method entry: when call
      _method_exit,              // method exit: after return
      _compiled_method_load,     // after loading a compiled method: entry, codes, scopes data etc included
      _compiled_method_unload,   // after unloading a compiled method
      _thread_start,             // a thread begins, map between system tid and java tid
      _codelet_info,             // templates info etc...
      _inline_cache_add,         // inline cache: a map between a source ip to a destination ip
      _inline_cache_clear,       // inline cache clear: delete the map
    };

    struct DumpInfo {
      u4 type;
      u4 size;
      u8 time;
    };

    struct MethodEntryInitial {
      struct DumpInfo info;
      u4 idx;
      u4 klass_name_length;
      u4 method_name_length;
      u4 method_signature_length;
      u4 tid;
      u4 _pending;

      MethodEntryInitial(u4 _idx, u4 _klass_name_length,
                         u4 _method_name_length,
                         u4 _method_signature_length,
                         u4 _tid, u4 _size) :
        idx(_idx),
        klass_name_length(_klass_name_length),
        method_name_length(_method_name_length),
        method_signature_length(_method_signature_length),
        tid(_tid) {
        info.type = _method_entry_initial;
        info.size = _size;
        info.time = get_timestamp();
      }
    };

    struct MethodEntryInfo {
      struct DumpInfo info;
      u4 idx;
      u4 tid;
      MethodEntryInfo(u4 _idx, u4 _tid, u4 _size) : idx(_idx), tid(_tid) {
        info.type = _method_entry;
        info.size = _size;
        info.time = get_timestamp();
      }
    };

    struct MethodExitInfo {
      struct DumpInfo info;
      u4 idx;
      u4 tid;

      MethodExitInfo(u4 _idx, u4 _tid, u4 _size) : idx(_idx), tid(_tid) {
        info.type = _method_entry;
        info.size = _size;
        info.time = get_timestamp();
      }
    };

    struct CompiledMethodLoadInfo {
      struct DumpInfo info;
      u8 code_begin;
      u8 entry_point;
      u8 verified_entry_point;
      u8 osr_entry_point;
      u4 inline_method_cnt;
      u4 code_size;
      u4 scopes_pc_size;
      u4 scopes_data_size;

      CompiledMethodLoadInfo(u8 _code_begin, u8 _entry_point,
                             u8 _verified_entry_point, u8 _osr_entry_point,
                             u4 _inline_method_cnt,
                             u4 _code_size, u4 _scopes_pc_size,
                             u4 _scopes_data_size,
                             u4 _size)
                     : code_begin(_code_begin),
                       entry_point(_entry_point),
                       verified_entry_point(_verified_entry_point),
                       osr_entry_point(_osr_entry_point),
                       inline_method_cnt(_inline_method_cnt),
                       code_size(_code_size),
                       scopes_pc_size(_scopes_pc_size),
                       scopes_data_size(_scopes_data_size) {
        info.type = _compiled_method_load;
        info.size = _size;
        info.time = get_timestamp();
      }
    };

    struct CompiledMethodUnloadInfo {
      struct DumpInfo info;
      u8 code_begin;

      CompiledMethodUnloadInfo(u8 _code_begin, u8 _size) : code_begin(_code_begin) {
        info.type = _compiled_method_unload;
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
      u4 java_tid;
      u4 sys_tid;

      ThreadStartInfo(u4 _java_tid, u4 _sys_tid, u4 _size) : java_tid(_java_tid), sys_tid(_sys_tid) {
        info.type = _thread_start;
        info.size = _size;
        info.time = get_timestamp();
      }
    };

    struct InlineCacheAdd {
      struct DumpInfo info;
      u8 src;
      u8 dest;

      InlineCacheAdd(u8 _src, u8 _dest, u4 size) : src(_src), dest(_dest) {
        info.type = _inline_cache_add;
        info.size = size;
        info.time = get_timestamp();
      }
    };

    struct InlineCacheClear {
      struct DumpInfo info;
      u8 src;

      InlineCacheClear(u8 _src, u4 size) : src(_src) {
        info.type = _inline_cache_clear;
        info.size = size;
        info.time = get_timestamp();
      }
    };

    struct CodeletsInfo {
      struct DumpInfo info;
      u8 low_bound;
      u8 high_bound;

      u8 slow_signature_handler;

      u8 unimplemented_bytecode_entry;
      u8 illegal_bytecode_sequence_entry;

      u8 return_entry[TemplateInterpreter::number_of_return_entries][number_of_states];
      u8 invoke_return_entry[TemplateInterpreter::number_of_return_addrs];
      u8 invokeinterface_return_entry[TemplateInterpreter::number_of_return_addrs];
      u8 invokedynamic_return_entry[TemplateInterpreter::number_of_return_addrs];

      u8 earlyret_entry[number_of_states];

      u8 native_abi_to_tosca[TemplateInterpreter::number_of_result_handlers];

      u8 rethrow_exception_entry;
      u8 throw_exception_entry;
      u8 remove_activation_preserving_args_entry;
      u8 remove_activation_entry;
      u8 throw_ArrayIndexOutOfBoundsException_entry;
      u8 throw_ArrayStoreException_entry;
      u8 throw_ArithmeticException_entry;
      u8 throw_ClassCastException_entry;
      u8 throw_NullPointerException_entry;
      u8 throw_StackOverflowError_entry;

      u8 entry_table[TemplateInterpreter::number_of_method_entries];

      u8 normal_table[DispatchTable::length][number_of_states];
      u8 wentry_point[DispatchTable::length];

      u8 deopt_entry[TemplateInterpreter::number_of_deopt_entries][number_of_states];
      u8 deopt_reexecute_return_entry;

      CodeletsInfo(u4 size) {
        info.type = _codelet_info;
        info.size = size;
        info.time = get_timestamp();
      }
    };

    inline static u8 get_timestamp() {
	    unsigned int low, high;
	    asm volatile("rdtsc" : "=a" (low), "=d" (high));
	    return low | ((u8)high) << 32;
    }

    inline static u4 get_java_tid(JavaThread* thread);

    inline static void dump_data(address src, size_t size);

    // initialize shared memory, methoda array, etc & begin to trace...
    static void init();

    // destroy shared memory, method array, etc...
    static void destroy();

    static void dump_codelets();

    static void dump_method_entry(JavaThread *thread, Method *moop);

    static void dump_method_exit(JavaThread *thread, Method *moop);

    static void dump_compiled_method_load(Method *moop, nmethod *nm);

    static void dump_compiled_method_unload(Method *moop, nmethod *nm);

    static void dump_thread_start(JavaThread *thread);

    static void dump_inline_cache_add(address src, address dest);

    static void dump_inline_cache_clear(address src);

};

#endif

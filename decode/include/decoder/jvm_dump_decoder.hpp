#ifndef JVM_DUMP_DECODER_HPP
#define JVM_DUMP_DECODER_HPP

#include <map>

using namespace std;

struct jit_section;
class Analyser;
class Method;

class JvmDumpDecoder {
  public:
    enum DumpInfoType {
      _illegal = -1,
      _method_entry_initial,
      _method_entry,
      _method_exit,
      _compiled_method_load,
      _compiled_method_unload,
      _thread_start,
      _interpreter_info,
      _inline_cache_add,
      _inline_cache_clear,
      _no_thing
    };

    struct DumpInfo {
        DumpInfoType type;
        /* total size: following MethodEntry/CompiledMethod size included*/
        uint64_t size;
        uint64_t time;
    };

    struct InterpreterInfo {
      uint64_t low_bound;
      uint64_t high_bound;
      uint64_t unimplemented_bytecode;
      uint64_t illegal_bytecode_sequence;
      uint64_t return_entry[6][10];
      uint64_t invoke_return_entry[10];
      uint64_t invokeinterface_return_entry[10];
      uint64_t invokedynamic_return_entry[10];
      uint64_t native_abi_to_tosca[10];
      uint64_t rethrow_exception_entry;
      uint64_t throw_exception_entry;
      uint64_t remove_activation_preserving_args_entry;
      uint64_t remove_activation_entry;
      uint64_t throw_ArrayIndexOutOfBoundsException_entry;
      uint64_t throw_ArrayStoreException_entry;
      uint64_t throw_ArithmeticException_entry;
      uint64_t throw_ClassCastException_entry;
      uint64_t throw_NullPointerException_entry;
      uint64_t throw_StackOverflowError_entry;
      uint64_t entry_table[34];
      uint64_t normal_table[256][10];
      uint64_t wentry_point[256];
      uint64_t deopt_entry[7][10];
      uint64_t deopt_reexecute_return_entry;
    };

    struct MethodEntryInitial {
      int idx;
      uint64_t tid;
      int klass_name_length;
      int method_name_length;
      int method_signature_length;
    };

    struct MethodEntryInfo {
      int idx;
      uint64_t tid;
    };

    struct MethodExitInfo {
      int idx;
      uint64_t tid;
    };

    struct InlineMethodInfo {
        int klass_name_length;
        int name_length;
        int signature_length;
        int method_index;
    };

    struct ThreadStartInfo {
        long java_tid;
        long sys_tid;
    };

    struct CompiledMethodLoadInfo {
        uint64_t code_begin;
        uint64_t code_size;
        uint64_t scopes_pc_size;
        uint64_t scopes_data_size;
        uint64_t entry_point;
        uint64_t verified_entry_point;
        uint64_t osr_entry_point;
        int inline_method_cnt;
    };

    struct CompiledMethodUnloadInfo {
      uint64_t code_begin;
    };

    struct DynamicCodeGenerated {
      int name_length;
      uint64_t code_begin;
      uint64_t code_size;
    };

    struct InlineCacheAdd {
      uint64_t src;
      uint64_t dest;
    };

    struct InlineCacheClear {
      uint64_t src;
    };

    JvmDumpDecoder() {
        current = begin;
    }

    DumpInfoType dumper_event(uint64_t time, long tid, const void *&data);
    long get_java_tid(long tid);

    static void initialize(char *dump_data, Analyser* anslyser);
    static void destroy();

  private:
    const uint8_t *current;

    static uint8_t *begin;
    static uint8_t *end;
    /* map between system thread id & java thread id */
    static map<long, long> thread_map;
    static map<int, const Method*> md_map;
    static map<const uint8_t *, jit_section *> section_map;
};

#endif
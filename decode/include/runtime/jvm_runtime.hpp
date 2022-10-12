#ifndef JVM_RUNTIME_HPP
#define JVM_RUNTIME_HPP

#include "utilities/definitions.hpp"

#include <map>

using std::map;
using std::pair;

class Method;
class JitSection;
class JitImage;
class Analyser;

/* This class records JVM runtime information.
 * It is initialized by analysing dump file: usually named JPortalDump
 * 
 * DumpType defines different types of information
 */

class JVMRuntime {
public:
    const static int number_of_states = 10;
    const static int number_of_return_entries = 6;
    const static int number_of_return_addrs = 10;
    const static int number_of_method_entries = 34;
    const static int number_of_result_handlers = 10;
    const static int number_of_deopt_entries = 7;
    const static int dispatch_length = 256;
    const static int number_of_codes = 239;

    /* JPortalDump files */
    enum DumpType {
        _illegal = -1,
        
        /* First method entry: map between idx to method signature */
        _method_entry_initial,

        /* method entry: when call */
        _method_entry,

        /* method exit: after return */
        _method_exit,

        /* After loading a compiled method: entry, codes, scopes data etc included*/
        _compiled_method_load,

        /* After unloading a compiled method */
        _compiled_method_unload,

        /* A thread begins*/
        _thread_start,

        /* Templates info etc... */
        _codelet_info,

        /* A inline cache: a map between a source ip to a destination ip */
        _inline_cache_add,

        /* Inline cache clear: delete the map */
        _inline_cache_clear,
    };

    struct DumpInfo {
        DumpType type;
        uint64_t size;
        uint64_t time;
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

    struct CodeletsInfo {
        // [low, high]
        address _low_bound;
        address _high_bound;

        // [error]
        address _unimplemented_bytecode_entry;
        address _illegal_bytecode_sequence_entry;

        // return
        address _return_entry[number_of_return_entries][number_of_states];
        address _invoke_return_entry[number_of_return_addrs];
        address _invokeinterface_return_entry[number_of_return_addrs];
        address _invokedynamic_return_entry[number_of_return_addrs];

        address _native_abi_to_tosca[number_of_result_handlers];

        // exception
        address _rethrow_exception_entry;
        address _throw_exception_entry;
        address _remove_activation_preserving_args_entry;
        address _remove_activation_entry;
        address _throw_ArrayIndexOutOfBoundsException_entry;
        address _throw_ArrayStoreException_entry;
        address _throw_ArithmeticException_entry;
        address _throw_ClassCastException_entry;
        address _throw_NullPointerException_entry;
        address _throw_StackOverflowError_entry;

        // method entry
        address _entry_table[number_of_method_entries];

        // bytecode template
        address _normal_table[dispatch_length][number_of_states];
        address _wentry_point[dispatch_length];

        // deoptimization
        address _deopt_entry[number_of_deopt_entries][number_of_states];
        address _deopt_reexecute_return_entry;

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

    JVMRuntime();

    void event(uint64_t time, long tid);
    long get_java_tid(long tid);
    JitImage* image() { return _image; }
    bool get_ic(uint64_t &ip, JitSection* section) {
      if (_ics.count({ip, section})) {
        ip = _ics[{ip, section}];
        return true;
      }
      return false;
    }

    static void initialize(char *dump_data, Analyser* analyser);
    static void destroy();

  private:
    const uint8_t *_current;
    JitImage *_image;
    map<pair<uint64_t, JitSection *>, uint64_t> _ics;

    static uint8_t *begin;
    static uint8_t *end;
    /* map between system thread id & java thread id */
    static map<long, long> thread_map;
    static map<int, const Method*> md_map;
    static map<const uint8_t *, JitSection *> section_map;
};

#endif

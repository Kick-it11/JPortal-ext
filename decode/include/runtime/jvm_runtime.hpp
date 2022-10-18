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
        u4 type;
        u4 size;
        u8 time;
    };

    struct MethodEntryInitial {
        u4 idx;
        u4 klass_name_length;
        u4 method_name_length;
        u4 method_signature_length;
        u4 tid;
        u4 _pending;
    };

    struct MethodEntryInfo {
        u4 idx;
        u4 tid;
    };

    struct MethodExitInfo {
        u4 idx;
        u4 tid;
    };

    struct CompiledMethodLoadInfo {
        u8 code_begin;
        u8 entry_point;
        u8 verified_entry_point;
        u8 osr_entry_point;
        u4 inline_method_cnt;
        u4 code_size;
        u4 scopes_pc_size;
        u4 scopes_data_size;
    };

    struct CompiledMethodUnloadInfo {
        u8 code_begin;
    };

    struct InlineMethodInfo {
        u4 klass_name_length;
        u4 method_name_length;
        u4 method_signature_length;
        u4 method_index;
    };

    struct ThreadStartInfo {
        u4 java_tid;
        u4 sys_tid;
    };

    struct InlineCacheAdd {
        u8 src;
        u8 dest;
    };

    struct InlineCacheClear {
        u8 src;
    };

    struct CodeletsInfo {
        address _low_bound;
        address _high_bound;

        address _slow_signature_handler;

        address _unimplemented_bytecode_entry;
        address _illegal_bytecode_sequence_entry;

        address _return_entry[number_of_return_entries][number_of_states];
        address _invoke_return_entry[number_of_return_addrs];
        address _invokeinterface_return_entry[number_of_return_addrs];
        address _invokedynamic_return_entry[number_of_return_addrs];

        address _earlyret_entry[number_of_states];

        address _native_abi_to_tosca[number_of_result_handlers];

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

        address _entry_table[number_of_method_entries];

        address _normal_table[dispatch_length][number_of_states];
        address _wentry_point[dispatch_length];

        address _deopt_entry[number_of_deopt_entries][number_of_states];
        address _deopt_reexecute_return_entry;

    };

    JVMRuntime();

    void move_on(uint64_t time);
    long get_java_tid(long tid);
    JitImage* image() { return _image; }
    bool get_ic(uint64_t &ip, JitSection* section) {
        if (_ics.count({ip, section})) {
            ip = _ics[{ip, section}];
            return true;
        }
        return false;
    }

    static void initialize(uint8_t *buffer, uint64_t size, Analyser* analyser);
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
    static bool _initialized;
};

#endif

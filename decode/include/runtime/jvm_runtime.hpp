#ifndef JVM_RUNTIME_HPP
#define JVM_RUNTIME_HPP

#include "java/definitions.hpp"
#include "java/bytecodes.hpp"

#include <map>

class Method;
class JitSection;
class JitImage;
class Analyser;

/* This class records JVM runtime information.
 * It is initialized by analysing dump file: usually named JPortalDump
 *
 * DumpType defines different types of information
 */

class JVMRuntime
{
public:
    const static int number_of_states = 10;
    const static int number_of_return_entries = 6;
    const static int number_of_return_addrs = 10;
    const static int number_of_method_entries = 34;
    const static int number_of_result_handlers = 10;
    const static int number_of_deopt_entries = 7;
    const static int dispatch_length = 256;
    const static int number_of_codes = 239;

    /* Codelet type */
    enum Codelet
    {
        _illegal = -1,

        _slow_signature_handler = 0,
        _unimplemented_bytecode = 1,
        _illegal_bytecode_sequence = 2,

        _return = 3,
        _invoke_return = 4,
        _invokeinterface_return = 5,
        _invokedynamic_return = 6,

        _earlyret = 7,

        _result_handlers_for_native_calls = 8,
        _rethrow_exception = 9,
        _throw_exception = 10,
        _remove_activation_preserving_args = 11,
        _remove_activation = 12,
        _throw_ArrayIndexOutOfBoundsException = 13,
        _throw_ArrayStoreException = 14,
        _throw_ArithmeticException = 15,
        _throw_ClassCastException = 16,
        _throw_NullPointerException = 17,
        _throw_StackOverflowError = 18,

        _method_entry_point = 19,

        _deopt = 20,
        _deopt_reexecute_return = 21,

        _bytecode = 22,

        _jitcode_entry = 23,
        _jitcode_osr_entry = 24,
        _jitcode = 25,

    };

    /* JPortalDump files */
    enum DumpType
    {
        /* First method entry: map between idx to method signature */
        _method_initial_info,

        /* method entry: when call */
        _method_entry_info,

        /* method exit: when exit*/
        _method_exit_info,

        /* After loading a compiled method: entry, codes, scopes data etc included*/
        _compiled_method_load_info,

        /* After unloading a compiled method */
        _compiled_method_unload_info,

        /* A thread begins*/
        _thread_start_info,

        /* Templates info etc... */
        _codelet_info,

        /* A inline cache: a map between a source ip to a destination ip */
        _inline_cache_add_info,

        /* Inline cache clear: delete the map */
        _inline_cache_clear_info,
    };

    struct DumpInfo
    {
        uint32_t type;
        uint32_t size;
        uint64_t time;
    };

    struct MethodEntryInitial
    {
        uint32_t klass_name_length;
        uint32_t method_name_length;
        uint32_t method_signature_length;
        uint32_t _pending;
        uint64_t method;
    };

    struct MethodEntryInfo
    {
        uint32_t tid;
        uint32_t _pending;
        uint64_t method;
    };

    struct MethodExitInfo
    {
        uint32_t tid;
        uint32_t _pending;
        uint64_t method;
    };

    struct CompiledMethodLoadInfo
    {
        uint64_t code_begin;
        uint64_t entry_point;
        uint64_t verified_entry_point;
        uint64_t osr_entry_point;
        uint32_t inline_method_cnt;
        uint32_t code_size;
        uint32_t scopes_pc_size;
        uint32_t scopes_data_size;
    };

    struct CompiledMethodUnloadInfo
    {
        uint64_t code_begin;
    };

    struct InlineMethodInfo
    {
        uint32_t klass_name_length;
        uint32_t method_name_length;
        uint32_t method_signature_length;
        uint32_t method_index;
    };

    struct ThreadStartInfo
    {
        uint32_t java_tid;
        uint32_t sys_tid;
    };

    struct InlineCacheAdd
    {
        uint64_t src;
        uint64_t dest;
    };

    struct InlineCacheClear
    {
        uint64_t src;
    };

    struct CodeletsInfo
    {
        uint64_t _low_bound;
        uint64_t _high_bound;

        uint64_t _slow_signature_handler;

        uint64_t _unimplemented_bytecode_entry;
        uint64_t _illegal_bytecode_sequence_entry;

        uint64_t _return_entry[number_of_return_entries][number_of_states];
        uint64_t _invoke_return_entry[number_of_return_addrs];
        uint64_t _invokeinterface_return_entry[number_of_return_addrs];
        uint64_t _invokedynamic_return_entry[number_of_return_addrs];

        uint64_t _earlyret_entry[number_of_states];

        uint64_t _native_abi_to_tosca[number_of_result_handlers];

        uint64_t _rethrow_exception_entry;
        uint64_t _throw_exception_entry;
        uint64_t _remove_activation_preserving_args_entry;
        uint64_t _remove_activation_entry;
        uint64_t _throw_ArrayIndexOutOfBoundsException_entry;
        uint64_t _throw_ArrayStoreException_entry;
        uint64_t _throw_ArithmeticException_entry;
        uint64_t _throw_ClassCastException_entry;
        uint64_t _throw_NullPointerException_entry;
        uint64_t _throw_StackOverflowError_entry;

        uint64_t _entry_table[number_of_method_entries];

        uint64_t _normal_table[dispatch_length][number_of_states];
        uint64_t _wentry_point[dispatch_length];

        uint64_t _deopt_entry[number_of_deopt_entries][number_of_states];
        uint64_t _deopt_reexecute_return_entry;
    };

    JVMRuntime();
    ~JVMRuntime();

    /* parse jvm runtime event to time */
    void move_on(uint64_t time);

    /* match ip to a codelet, return value refers to bytecode maybe
     * if none, return -1;
     */
    Codelet match(uint64_t ip, Bytecodes::Code &code, JitSection *&section);
    uint32_t get_java_tid(uint32_t tid);
    JitImage *image() { return _image; }
    bool get_ic(uint64_t &ip, JitSection *section)
    {
        if (_ics.count({ip, section}))
        {
            ip = _ics[{ip, section}];
            return true;
        }
        return false;
    }

    static void initialize(uint8_t *buffer, uint64_t size, Analyser *analyser);
    static void destroy();
    static void print();

private:
    const uint8_t *_current;
    JitImage *_image;
    std::map<std::pair<uint64_t, JitSection *>, uint64_t> _ics;

    static uint8_t *begin;
    static uint8_t *end;
    static const CodeletsInfo *entries;
    /* map between system thread id & java thread id */
    static std::map<uint32_t, uint32_t> thread_map;
    static std::map<uint64_t, const Method *> md_map;
    static std::map<const uint8_t *, JitSection *> section_map;
    static bool initialized;
};

#endif /* JVM_RUNTIME_HPP */

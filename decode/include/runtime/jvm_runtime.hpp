#ifndef JVM_RUNTIME_HPP
#define JVM_RUNTIME_HPP

#include "java/definitions.hpp"
#include "java/bytecodes.hpp"

#include <map>
#include <set>

class Method;
class JitSection;
class JitImage;
class Analyser;

/* This class records JVM runtime information.
 * It is initialized by analysing dump file: usually named JPortalDump
 *
 * DumpType defines different types of information
 * some dumped information are shared by all decoding thread
 * since they all not change,  so process them in advance in initialize
 *       such as:
 *               (will judge through ip)
 *               method entry,
 *               branch taken,
 *               branch not taken,
 *
 *               (process and new jit section in advance)
 *               compiled method
 *
 *               thread start
 *
 * Some are decoding threads exclusive info,
 *       such as:
 *               inline cache(src ip -> dest ip)(add or clear according to time)
 *               compiled method(jit_iamge will do load and unload according to time -> exclusive)
 *               deoptimization
 *               exception handling
 */

class JVMRuntime
{
public:
    /* JVMRuntime dump type from JPortalTrace.data file */
    enum DumpType
    {
        /* First method entry: map between ip to method */
        _method_entry_info,

        /* branch taken */
        _branch_taken_info,

        /* branch not taken */
        _branch_not_taken_info,

        /* exception handling or maybe unwind (throw out) -> runtime event */
        _exception_handling_info,

        /* deoptimization -> runtime event */
        _deoptimization_info,

        /* After loading a compiled method: entry, codes, scopes data etc included*/
        _compiled_method_load_info,

        /* After unloading a compiled method */
        _compiled_method_unload_info,

        /* A thread begins*/
        _thread_start_info,

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

    struct MethodEntryInfo
    {
        uint32_t klass_name_length;
        uint32_t method_name_length;
        uint32_t method_signature_length;
        uint32_t _pending;
        uint64_t addr;
    };

    struct BranchTakenInfo
    {
        uint64_t addr;
    };

    struct BranchNotTakenInfo
    {
        uint64_t addr;
    };

    struct ExceptionHandlingInfo
    {
        int32_t current_bci;
        int32_t handler_bci;
        uint64_t java_tid;
        uint64_t addr;
    };

    struct DeoptimizationInfo
    {
        int32_t bci;
        uint32_t _pending;
        uint64_t java_tid;
        uint64_t addr;
    };

    struct CompiledMethodLoadInfo
    {
        uint64_t code_begin;
        uint64_t stub_begin;
        uint64_t entry_point;
        uint64_t verified_entry_point;
        uint64_t osr_entry_point;
        uint64_t exception_begin;
        uint64_t deopt_begin;
        uint64_t deopt_mh_begin;
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
        uint64_t java_tid;
        uint64_t sys_tid;
    };

    struct InlineCacheAddInfo
    {
        uint64_t src;
        uint64_t dest;
    };

    struct InlineCacheClearInfo
    {
        uint64_t src;
    };

    JVMRuntime();
    ~JVMRuntime();

    /* object functions */

    /* parse jvm runtime event to time */
    int event(uint64_t time, const uint8_t **data);

    /* static functions */

    /* initialize, process all section in advance */
    static void initialize(uint8_t *buffer, uint64_t size, Analyser *analyser);

    /* destroy: deconstruct section */
    static void destroy();

    static bool not_taken_branch(uint64_t ip)
    {
        return not_takens.count(ip);
    }

    static bool taken_branch(uint64_t ip)
    {
        return takens.count(ip);
    }

    static const Method *method_entry(uint64_t ip)
    {
        return md_map.count(ip) ? md_map[ip] : nullptr;
    }

    static JitSection *jit_section(const uint8_t *pointer)
    {
        return section_map.count(pointer) ? section_map[pointer] : nullptr;
    }

    static uint64_t java_tid(uint64_t tid)
    {
        return tid_map.count(tid) ? tid_map[tid] : 0;
    }

    /* print all jvm runtime event */
    static void print();

private:
    /* current position */
    const uint8_t *_current;

    /* runtime buffer begin */
    static uint8_t *begin;

    /* runtime buffer end */
    static uint8_t *end;

    /* map between method entry address and method */
    static std::map<uint64_t, const Method *> md_map;

    static std::set<uint64_t> takens;
    static std::set<uint64_t> not_takens;

    /* map between system tid and java tid
     * A potential bug here: system tid might be reused for different java tid
     *                       for a long running java program
     *                       Fix it later.
     */
    static std::map<uint64_t, uint64_t> tid_map;

    /* map between data pointer to sectio, process all in advance */
    static std::map<const uint8_t *, JitSection *> section_map;

    static bool initialized;
};

#endif /* JVM_RUNTIME_HPP */

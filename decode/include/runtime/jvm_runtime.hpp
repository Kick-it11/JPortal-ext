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

        /* First Method exit */
        _method_exit_info,

        /* branch taken */
        _branch_taken_info,

        /* branch not taken */
        _branch_not_taken_info,

        /* switch case */
        _switch_case_info,

        /* switch default */
        _switch_default_info,

        /* invoke site */
        _invoke_site_info,

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

    struct MethodExitInfo {
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

    struct SwitchCaseInfo {
        uint64_t addr;
        uint32_t num;
        uint32_t ssize;
    };

    struct SwitchDefaultInfo {
        uint64_t addr;
    };

    struct InvokeSiteInfo {
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
        uint8_t use_next_bci;
        uint8_t is_bottom_frame;
        uint16_t __pending;
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
        return _not_takens.count(ip);
    }

    static bool taken_branch(uint64_t ip)
    {
        return _takens.count(ip);
    }

    static const Method *method_entry(uint64_t ip)
    {
        return _entry_map.count(ip) ? _entry_map[ip] : nullptr;
    }

    static bool method_exit(uint64_t ip)
    {
        return _exits.count(ip);
    }

    static bool switch_case(uint64_t ip)
    {
        for (auto && c : _switch_cases)
        {
            if (ip >= c.first && ip < c.first + c.second.first*c.second.second)
            {
                return true;
            }
        }
        return false;
    }

    static int switch_case_index(uint64_t ip)
    {
        for (auto && c : _switch_cases)
        {
            if (ip >= c.first && ip < c.first + c.second.first*c.second.second)
            {
                return (ip - c.first)/c.second.second;
            }
        }
        return -1;
    }

    static bool switch_default(uint64_t ip)
    {
        return _switch_defaults.count(ip);
    }

    static bool invoke_site(uint64_t ip)
    {
        return _invoke_sites.count(ip);
    }

    static JitSection *jit_section(const uint8_t *pointer)
    {
        return _section_map.count(pointer) ? _section_map[pointer] : nullptr;
    }

    static uint64_t java_tid(uint64_t tid)
    {
        return _tid_map.count(tid) ? _tid_map[tid] : 0;
    }

    /* print all jvm runtime event */
    static void print(uint8_t *buffer, uint64_t size);

private:
    /* current position */
    const uint8_t *_current;

    /* runtime buffer begin */
    static uint8_t *_begin;

    /* runtime buffer end */
    static uint8_t *_end;

    /* map between method entry address and method */
    static std::map<uint64_t, const Method *> _entry_map;

    static std::set<uint64_t> _takens;
    static std::set<uint64_t> _not_takens;

    /*{ code_entry, {num, ssize} }*/
    static std::set<std::pair<uint64_t, std::pair<int, int>>> _switch_cases;
    static std::set<uint64_t> _switch_defaults;
    static std::set<uint64_t> _invoke_sites;
    static std::set<uint64_t> _exits;

    /* map between system tid and java tid
     * A potential bug here: system tid might be reused for different java tid
     *                       for a long running java program
     *                       Fix it later. By using sys tid and java tid both
     *                                        and making it a runtime info
     */
    static std::map<uint64_t, uint64_t> _tid_map;

    /* map between data pointer to sectio, process all in advance */
    static std::map<const uint8_t *, JitSection *> _section_map;

    static bool _initialized;
};

#endif /* JVM_RUNTIME_HPP */

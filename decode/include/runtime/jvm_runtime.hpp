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
 */

class JVMRuntime
{
public:
    /* JVMRuntime dump type from JPortalTrace.data file */
    enum DumpType
    {
        /* First method entry: map between ip to method */
        _method_info,

        /* jportal table stub, exception handling */
        _bci_table_stub_info,

        /* jportal table stub, exception handling */
        _switch_table_stub_info,

        /* switch default */
        _switch_default_info,

        /* branch taken */
        _branch_taken_info,

        /* branch taken */
        _branch_not_taken_info,

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

    struct MethodInfo
    {
        uint32_t klass_name_length;
        uint32_t method_name_length;
        uint32_t method_signature_length;
        uint32_t _pending;
        uint64_t addr1;
        uint64_t addr2; // only work for JPortalMethod
    };

    struct BciTableStubInfo {
      u8 addr;
      u4 num;
      u4 ssize;
    };

    struct SwitchTableStubInfo {
      u8 addr;
      u4 num;
      u4 ssize;
    };

    struct SwitchDefaultInfo {
      u8 addr;
    };

    struct BranchTakenInfo
    {
        uint64_t addr;
    };

    struct BranchNotTakenInfo
    {
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

    static const Method *method(uint64_t ip)
    {
        return _methods.count(ip) ? _methods[ip] : nullptr;
    }

    static const Method *method_exit(uint64_t ip)
    {
        return _exits.count(ip) ? _exits[ip] : nullptr;
    }

    static bool in_bci_table(uint64_t ip)
    {
        return ip >= _bci_tables.first && ip < _bci_tables.first +
                _bci_tables.second.first * _bci_tables.second.second;
    }

    static int bci(uint64_t ip)
    {
        if (in_bci_table(ip))
            return (ip - _bci_tables.first) / _bci_tables.second.second;
        return -1;
    }

    static bool in_switch_table(uint64_t ip)
    {
        return ip >= _switch_tables.first && ip < _switch_tables.first +
                _switch_tables.second.first * _switch_tables.second.second;
    }

    static int switch_case(uint64_t ip)
    {
        if (in_switch_table(ip))
            return (ip - _switch_tables.first) / _switch_tables.second.second;
        return -1;
    }

    static bool switch_default(uint64_t ip)
    {
        return _switch_defaults.count(ip);
    }

    static JitSection *jit_section(const uint8_t *pointer)
    {
        return _section_map.count(pointer) ? _section_map[pointer] : nullptr;
    }

    static uint64_t java_tid(uint64_t tid)
    {
        return _tid_map.count(tid) ? _tid_map[tid] : 0;
    }

    static const Method *method_by_id(int id)
    {
        return _id_to_methods.count(id) ? _id_to_methods[id] : nullptr;
    }

    static const JitSection *jit_section_by_id(int id)
    {
        return _id_to_sections.count(id) ? _id_to_sections[id] : nullptr;
    }

    /* print all jvm runtime event */
    static void print(uint8_t *buffer, uint64_t size);

    static void output(std::string prefix);

private:
    /* current position */
    const uint8_t *_current;

    /* runtime buffer begin */
    static uint8_t *_begin;

    /* runtime buffer end */
    static uint8_t *_end;

    /* map between method entry address and method */
    static std::map<uint64_t, const Method *> _methods;
    static std::map<uint64_t, const Method *> _exits;

    static std::set<uint64_t> _takens;
    static std::set<uint64_t> _not_takens;

    /*{ code_entry, {num, ssize} }*/
    static std::pair<uint64_t, std::pair<int, int>> _bci_tables;
    static std::pair<uint64_t, std::pair<int, int>> _switch_tables;
    static std::set<uint64_t> _switch_defaults;

    /* map between system tid and java tid
     * A potential bug here: system tid might be reused for different java tid
     *                       for a long running java program
     *                       Fix it later. By using sys tid and java tid both
     *                                        and making it a runtime info
     */
    static std::map<uint64_t, uint64_t> _tid_map;

    /* map between data pointer to sectio, process all in advance */
    static std::map<const uint8_t *, JitSection *> _section_map;

    /* map id to methods */
    static std::map<int, const Method *> _id_to_methods;
    /* map id to section */
    static std::map<int, JitSection *> _id_to_sections;

    static bool _initialized;
};

#endif /* JVM_RUNTIME_HPP */

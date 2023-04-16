#ifndef JVM_RUNTIME_HPP
#define JVM_RUNTIME_HPP

#include "java/definitions.hpp"
#include "java/bytecodes.hpp"

#include <map>
#include <set>

class Method;
class JitSection;
class JitImage;

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
    enum DumpType {
      _java_call_begin_info,          /*JavaCalls::call() begin*/
      _java_call_end_info,            /*JavaCalls::call() end*/
      _method_info,                   /*method info*/
      _branch_taken_info,             /*branch taken*/
      _branch_not_taken_info,         /*branch not taken*/
      _switch_table_stub_info,        /*jportal table stub*/
      _switch_default_info,           /*switch default*/
      _bci_table_stub_info,           /*jportal table stub,*/
      _osr_info,                      /*online stack replacement*/
      _throw_exception_info,          /*indicate throwing a exception*/
      _rethrow_exception_info,        /* rethrow exception */
      _handle_exception_info,         /*to exception handler*/
      _ret_code_info,                 /*indicate a ret or wide ret [jsr/ret]*/
      _deoptimization_info,           /*indicate a deoptimization*/
      _non_invoke_ret_info,           /*non invoke ret, such as deoptimization*/
      _pop_frame_info,                /*indicate a pop frame*/
      _earlyret_info,                 /*indicate an early ret*/
      _compiled_method_load_info,     /*after loading a compiled method: entry, codes, scopes data etc included*/
      _compiled_method_unload_info,   /*after unloading a compiled method*/
      _thread_start_info,             /*a thread begins, map between system tid and java tid*/
      _inline_cache_add_info,         /*inline cache: a map between a source ip to a destination ip*/
      _inline_cache_clear_info,       /*inline cache clear: delete the map*/
    };

    struct DumpInfo
    {
        uint32_t type;
        uint32_t size;
        uint64_t time;
    };

    struct JavaCallBeginInfo
    {
        uint64_t addr;
    };

    struct JavaCallEndInfo
    {
        uint64_t addr;
    };

    struct MethodInfo
    {
        uint32_t klass_name_length;
        uint32_t method_name_length;
        uint32_t method_signature_length;
        uint32_t _pending;
        uint64_t addr1;
        uint64_t addr2; /* JPortalMethod */
        uint64_t addr3; /* JPortal Only */
    };

    struct BranchTakenInfo
    {
        uint64_t addr;
    };

    struct BranchNotTakenInfo
    {
        uint64_t addr;
    };

    struct SwitchTableStubInfo {
        u8 addr;
        u4 num;
        u4 ssize;
    };

    struct SwitchDefaultInfo {
        u8 addr;
    };

    struct BciTableStubInfo {
        u8 addr;
        u4 num;
        u4 ssize;
    };

    struct OSRInfo
    {
        uint64_t addr;
    };

    struct ThrowExceptionInfo
    {
        uint64_t addr;
    };

    struct RethrowExceptionInfo
    {
        uint64_t addr;
    };

    struct HandleExceptionInfo
    {
        uint64_t addr;
    };

    struct RetCodeInfo
    {
        uint64_t addr;
    };

    struct DeoptimizationInfo
    {
        uint64_t addr;
    };

    struct NonInvokeRetInfo
    {
        uint64_t addr;
    };

    struct PopFrameInfo
    {
        uint64_t addr;
    };

    struct EarlyretInfo
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
        uint64_t exception_begin;
        uint64_t unwind_begin;
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

    /* for error */
    int forward(const uint8_t **data);

    /* static functions */

    /* initialize, process all section in advance */
    static void initialize(uint8_t *buffer, uint64_t size);

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
        return _entries.count(ip) ? _entries[ip] : nullptr;
    }

    static const Method *method_exit(uint64_t ip)
    {
        return _exits.count(ip) ? _exits[ip] : nullptr;
    }

    static const Method *method_point(uint64_t ip)
    {
        return _points.count(ip) ? _points[ip] : nullptr;
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

    static bool ret_code(uint64_t ip)
    {
        return _ret_codes.count(ip);
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

    static bool deoptimization(uint64_t ip)
    {
        return _deopts.count(ip);
    }

    static bool throw_exception(uint64_t ip)
    {
        return _throw_exceptions.count(ip);
    }

    static bool rethrow_exception(uint64_t ip)
    {
        return _rethrow_exceptions.count(ip);
    }

    static bool handle_exception(uint64_t ip)
    {
        return _handle_exceptions.count(ip);
    }

    static bool pop_frame(uint64_t ip)
    {
        return _pop_frames.count(ip);
    }

    static bool earlyret(uint64_t ip)
    {
        return _earlyrets.count(ip);
    }

    static bool non_invoke_ret(uint64_t ip)
    {
        return _non_invoke_rets.count(ip);
    }

    static bool osr(uint64_t ip)
    {
        return _osrs.count(ip);
    }

    static bool java_call_begin(uint64_t ip)
    {
        return _java_call_begins.count(ip);
    }

    static bool java_call_end(uint64_t ip)
    {
        return _java_call_ends.count(ip);
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

    /* map between method entry and method */
    static std::map<uint64_t, const Method *> _entries;
    /* map between method exit and method */
    static std::map<uint64_t, const Method *> _exits;
    /* map between method exit and method middle points */
    static std::map<uint64_t, const Method *> _points;

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

    /* indication to deoptimization */
    static std::set<uint64_t> _deopts;

    /* indication to ret or wide ret*/
    static std::set<uint64_t> _ret_codes;

    /* indication to throw exception */
    static std::set<uint64_t> _throw_exceptions;

    /* indication to rethrow exception */
    static std::set<uint64_t> _rethrow_exceptions;

    /* indication to handle exception */
    static std::set<uint64_t> _handle_exceptions;

    /* indication to pop frame */
    static std::set<uint64_t> _pop_frames;

    /* indication to early ret*/
    static std::set<uint64_t> _earlyrets;

    /* indicate non invoke ret */
    static std::set<uint64_t> _non_invoke_rets;

    /* online stack replacement */
    static std::set<uint64_t> _osrs;

    /* indication to java call begin*/
    static std::set<uint64_t> _java_call_begins;

    /* indication to java call end */
    static std::set<uint64_t> _java_call_ends;

    static bool _initialized;

    static bool check_duplicated_entry(uint64_t ip, std::string str);
};

#endif /* JVM_RUNTIME_HPP */

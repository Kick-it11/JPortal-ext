#include "java/analyser.hpp"
#include "pt/pt.hpp"
#include "runtime/jit_image.hpp"
#include "runtime/jit_section.hpp"
#include "runtime/jvm_runtime.hpp"

#include <cassert>
#include <iostream>
#include <fstream>

uint8_t *JVMRuntime::_begin = nullptr;
uint8_t *JVMRuntime::_end = nullptr;
std::map<uint64_t, const Method *> JVMRuntime::_entries;
std::map<uint64_t, const Method *> JVMRuntime::_exits;
std::map<uint64_t, const Method *> JVMRuntime::_points;
std::set<uint64_t> JVMRuntime::_takens;
std::set<uint64_t> JVMRuntime::_not_takens;
std::pair<uint64_t, std::pair<int, int>> JVMRuntime::_bci_tables = {0, {0, 0}};
std::pair<uint64_t, std::pair<int, int>> JVMRuntime::_switch_tables = {0, {0, 0}};
std::set<uint64_t> JVMRuntime::_switch_defaults;
std::map<uint64_t, uint64_t> JVMRuntime::_tid_map;
std::map<const uint8_t *, JitSection *> JVMRuntime::_section_map;
std::map<int, const Method *> JVMRuntime::_id_to_methods;
std::map<int, JitSection *> JVMRuntime::_id_to_sections;
std::set<uint64_t> JVMRuntime::_deopts;
std::set<uint64_t> JVMRuntime::_ret_codes;
std::set<uint64_t> JVMRuntime::_throw_exceptions;
std::set<uint64_t> JVMRuntime::_rethrow_exceptions;
std::set<uint64_t> JVMRuntime::_handle_exceptions;
std::set<uint64_t> JVMRuntime::_pop_frames;
std::set<uint64_t> JVMRuntime::_earlyrets;
std::set<uint64_t> JVMRuntime::_non_invoke_rets;
std::set<uint64_t> JVMRuntime::_osrs;
std::set<uint64_t> JVMRuntime::_java_call_begins;
std::set<uint64_t> JVMRuntime::_java_call_ends;
bool JVMRuntime::_initialized = false;

bool JVMRuntime::check_duplicated_entry(uint64_t ip, std::string str)
{
    if (_entries.count(ip))
    {
        std::cerr << "JVMRuntime error: " << ip << "both in entries & " << str << std::endl;
        return true;
    }
    if (_exits.count(ip))
    {
        std::cerr << "JVMRuntime error: " << ip << "both in exits & " << str << std::endl;
        return true;
    }
    if (_points.count(ip))
    {
        std::cerr << "JVMRuntime error: " << ip << "both in points & " << str << std::endl;
        return true;
    }
    if (_takens.count(ip))
    {
        std::cerr << "JVMRuntime error: " << ip << "both in takens & " << str << std::endl;
        return true;
    }
    if (_not_takens.count(ip))
    {
        std::cerr << "JVMRuntime error: " << ip << "both in not takens & " << str << std::endl;
        return true;
    }
    if (ip >= _bci_tables.first && ip < _bci_tables.first + _bci_tables.second.first*_bci_tables.second.second)
    {
        std::cerr << "JVMRuntime error: " << ip << "both in bci table & " << str << std::endl;
        return true;
    }
    if (ip >= _switch_tables.first && ip <= _switch_tables.second.first*_switch_tables.second.second)
    {
        std::cerr << "JVMRuntime error: " << ip << "both in switch table & " << str << std::endl;
        return true;
    }
    if (_switch_defaults.count(ip))
    {
        std::cerr << "JVMRuntime error: " << ip << "both in switch defaults & " << str << std::endl;
        return true;
    }
    if (_deopts.count(ip))
    {
        std::cerr << "JVMRuntime error: " << ip << "both in deopts & " << str << std::endl;
        return true;
    }
    if (_ret_codes.count(ip))
    {
        std::cerr << "JVMRuntime error: " << ip << "both in ret codes & " << str << std::endl;
        return true;
    }
    if (_throw_exceptions.count(ip))
    {
        std::cerr << "JVMRuntime error: " << ip << "both in throw exceptions & " << str << std::endl;
        return true;
    }
    if (_rethrow_exceptions.count(ip))
    {
        std::cerr << "JVMRuntime error: " << ip << "both in rethrow exceptions & " << str << std::endl;
        return true;
    }
    if (_handle_exceptions.count(ip))
    {
        std::cerr << "JVMRuntime error: " << ip << "both in handle exceptions & " << str << std::endl;
        return true;
    }
    if (_pop_frames.count(ip))
    {
        std::cerr << "JVMRuntime error: " << ip << "both in pop frames & " << str << std::endl;
        return true;
    }
    if (_earlyrets.count(ip))
    {
        std::cerr << "JVMRuntime error: " << ip << "both in earlyrets & " << str << std::endl;
        return true;
    }
    if (_non_invoke_rets.count(ip))
    {
        std::cerr << "JVMRuntime error: " << ip << "both in non invoke rets & " << str << std::endl;
        return true;
    }
    if (_osrs.count(ip))
    {
        std::cerr << "JVMRuntime error: " << ip << "both in osrs & " << str << std::endl;
        return true;
    }
    if (_java_call_begins.count(ip))
    {
        std::cerr << "JVMRuntime error: " << ip << "both in java call begins & " << str << std::endl;
        return true;
    }
    if (_java_call_ends.count(ip))
    {
        std::cerr << "JVMRuntime error: " << ip << "both in java call ends & " << str << std::endl;
        return true;
    }
    return false;
}

JVMRuntime::JVMRuntime()
{
    assert(_initialized);
    _current = _begin;
}

JVMRuntime::~JVMRuntime()
{
    _current = nullptr;
}

int JVMRuntime::forward(const uint8_t **data)
{
    if (!data)
    {
        return -pte_internal;
    }

    if (_current >= _end)
    {
        return -pte_eos;
    }

    const DumpInfo *info = (const struct DumpInfo *)_current;
    if (_current + info->size > _end)
    {
        _current = _end;
        return -pte_internal;
    }

    *data = _current;
    _current += info->size;

    return pts_event_pending;
}

int JVMRuntime::event(uint64_t time, const uint8_t **data)
{
    if (!data)
    {
        return -pte_internal;
    }

    if (_current >= _end)
    {
        return -pte_eos;
    }

    const DumpInfo *info = (const struct DumpInfo *)_current;
    if (_current + info->size > _end)
    {
        _current = _end;
        return -pte_internal;
    }

    if (info->time > time)
    {
        return 0;
    }

    *data = _current;
    _current += info->size;

    return pts_event_pending;
}

void JVMRuntime::initialize(uint8_t *buffer, uint64_t size)
{
    _begin = buffer;
    _end = buffer + size;
    const DumpInfo *info;
    while (buffer < _end)
    {
        uint8_t *event_start = buffer;
        info = (const struct DumpInfo *)buffer;
        if (buffer + info->size > _end)
        {
            std::cerr << "JVMRuntime error: Read JVMRuntime info" << std::endl;
            exit(1);
        }
        buffer += sizeof(DumpInfo);
        switch (info->type)
        {
        case _java_call_begin_info:
        {
            const JavaCallBeginInfo *jcbi;
            jcbi = (const JavaCallBeginInfo *)buffer;
            buffer += sizeof(JavaCallBeginInfo);
            if (check_duplicated_entry(jcbi->addr, "java call begins"))
            {
                break;
            }
            _java_call_begins.insert(jcbi->addr);
            break;
        }
        case _java_call_end_info:
        {
            const JavaCallEndInfo *jcei;
            jcei = (const JavaCallEndInfo *)buffer;
            buffer += sizeof(JavaCallEndInfo);
            if (check_duplicated_entry(jcei->addr, "java call ends"))
            {
                break;
            }
            _java_call_ends.insert(jcei->addr);
            break;
        }
        case _method_info:
        {
            const MethodInfo *mei;
            mei = (const MethodInfo *)buffer;
            buffer += sizeof(MethodInfo);
            std::string klass_name((const char *)buffer, mei->klass_name_length);
            buffer += mei->klass_name_length;
            std::string name((const char *)buffer, mei->method_name_length);
            buffer += mei->method_name_length;
            std::string sig((const char *)buffer, mei->method_signature_length);
            buffer += mei->method_signature_length;
            const Method *method = Analyser::get_method(klass_name, name + sig);
            if (!method || !method->is_jportal())
            {
                std::cerr << "JvmRuntime error: Unknown or un-jportal method" << std::endl;
                break;
            }
            if (check_duplicated_entry(mei->addr1, "entries") ||
                mei->addr2 && check_duplicated_entry(mei->addr2, "exits") ||
                mei->addr3 && check_duplicated_entry(mei->addr3, "points"))
            {
                break;
            }
            if (_id_to_methods.count(method->id()) && _id_to_methods[method->id()] != method)
            {
                std::cerr << "JVMRuntime error: Method with the same id" << std::endl;
                break;
            }
            else
            {
                _id_to_methods[method->id()] = method;
            }
            _entries[mei->addr1] = method;
            if (mei->addr2)
            {
                _exits[mei->addr2] = method;
            }
            if (mei->addr3)
            {
                _points[mei->addr3] = method;
            }
            break;
        }
        case _branch_taken_info:
        {
            const BranchTakenInfo *bti;
            bti = (const BranchTakenInfo *)buffer;
            buffer += sizeof(BranchTakenInfo);
            if (check_duplicated_entry(bti->addr, "takens"))
            {
                break;
            }
            _takens.insert(bti->addr);
            break;
        }
        case _branch_not_taken_info:
        {
            const BranchNotTakenInfo *bnti;
            bnti = (const BranchNotTakenInfo *)buffer;
            buffer += sizeof(BranchNotTakenInfo);
            if (check_duplicated_entry(bnti->addr, "not takens"))
            {
                break;
            }
            _not_takens.insert(bnti->addr);
            break;
        }
        case _switch_table_stub_info:
        {
            const SwitchTableStubInfo *stsi;
            stsi = (const SwitchTableStubInfo *)buffer;
            buffer += sizeof(SwitchTableStubInfo);
            if (_switch_tables.second.first != 0 || _switch_tables.second.second != 0)
            {
                std::cerr << "JVMRuntime error: switch table re inited " << std::endl;
                break;
            }
            _switch_tables = {stsi->addr, {stsi->num, stsi->ssize}};
            break;
        }
        case _switch_default_info:
        {
            const SwitchDefaultInfo *sdi;
            sdi = (const SwitchDefaultInfo *)buffer;
            buffer += sizeof(SwitchDefaultInfo);
            if (check_duplicated_entry(sdi->addr, "switch defaults"))
            {
                break;
            }
            _switch_defaults.insert({sdi->addr});
            break;
        }
        case _bci_table_stub_info:
        {
            const BciTableStubInfo *btsi;
            btsi = (const BciTableStubInfo *)buffer;
            buffer += sizeof(BciTableStubInfo);
            if (_bci_tables.second.first != 0 || _bci_tables.second.second != 0)
            {
                std::cerr << "JVMRuntime error: bci table re inited " << std::endl;
                break;
            }
            _bci_tables = {btsi->addr, {btsi->num, btsi->ssize}};
            break;
        }
        case _osr_info:
        {
            const OSRInfo *osri;
            osri = (const OSRInfo *)buffer;
            buffer += sizeof(OSRInfo);
            if (check_duplicated_entry(osri->addr, "osrs"))
            {
                break;
            }
            _osrs.insert(osri->addr);
            break;
        }
        case _throw_exception_info:
        {
            const ThrowExceptionInfo *tei;
            tei = (const ThrowExceptionInfo *)buffer;
            buffer += sizeof(ThrowExceptionInfo);
            if (check_duplicated_entry(tei->addr, "throw exceptions"))
            {
                break;
            }
            _throw_exceptions.insert(tei->addr);
            break;
        }
        case _rethrow_exception_info:
        {
            const RethrowExceptionInfo *rei;
            rei = (const RethrowExceptionInfo *)buffer;
            buffer += sizeof(RethrowExceptionInfo);
            if (check_duplicated_entry(rei->addr, "rethrow exceptions"))
            {
                break;
            }
            _rethrow_exceptions.insert(rei->addr);
            break;
        }
        case _handle_exception_info:
        {
            const HandleExceptionInfo *hei;
            hei = (const HandleExceptionInfo *)buffer;
            buffer += sizeof(HandleExceptionInfo);
            if (check_duplicated_entry(hei->addr, "handle exceptions"))
            {
                break;
            }
            _handle_exceptions.insert(hei->addr);
            break;
        }
        case _ret_code_info:
        {
            const RetCodeInfo *rci;
            rci = (const RetCodeInfo *)buffer;
            buffer += sizeof(RetCodeInfo);
            if (check_duplicated_entry(rci->addr, "rci takens"))
            {
                break;
            }
            _ret_codes.insert(rci->addr);
            break;
        }
        case _deoptimization_info:
        {
            const DeoptimizationInfo *di;
            di = (const DeoptimizationInfo *)buffer;
            buffer += sizeof(DeoptimizationInfo);
            if (check_duplicated_entry(di->addr, "deopts"))
            {
                break;
            }
            _deopts.insert(di->addr);
            break;
        }
        case _non_invoke_ret_info:
        {
            const NonInvokeRetInfo *niri;
            niri = (const NonInvokeRetInfo *)buffer;
            buffer += sizeof(NonInvokeRetInfo);
            if (check_duplicated_entry(niri->addr, "non invoke rets"))
            {
                break;
            }
            _earlyrets.insert(niri->addr);
            break;
        }
        case _pop_frame_info:
        {
            const PopFrameInfo *pfi;
            pfi = (const PopFrameInfo *)buffer;
            buffer += sizeof(PopFrameInfo);
            if (check_duplicated_entry(pfi->addr, "pop frames"))
            {
                break;
            }
            _pop_frames.insert(pfi->addr);
            break;
        }
        case _earlyret_info:
        {
            const EarlyretInfo *ei;
            ei = (const EarlyretInfo *)buffer;
            buffer += sizeof(EarlyretInfo);
            if (check_duplicated_entry(ei->addr, "earlyrets"))
            {
                break;
            }
            _earlyrets.insert(ei->addr);
            break;
        }
        case _compiled_method_load_info:
        {
            const CompiledMethodLoadInfo *cmi;
            cmi = (const CompiledMethodLoadInfo *)buffer;
            buffer += sizeof(CompiledMethodLoadInfo);
            const Method *mainm = nullptr;
            std::map<int, const Method *> methods;
            for (int i = 0; i < cmi->inline_method_cnt; i++)
            {
                const InlineMethodInfo *imi;
                imi = (const InlineMethodInfo *)buffer;
                buffer += sizeof(InlineMethodInfo);
                const char *klass_name = (const char *)buffer;
                buffer += imi->klass_name_length;
                const char *name = (const char *)buffer;
                buffer += imi->method_name_length;
                const char *sig = (const char *)buffer;
                buffer += imi->method_signature_length;
                std::string klassName = std::string(klass_name, imi->klass_name_length);
                std::string methodName = std::string(name, imi->method_name_length) + std::string(sig, imi->method_signature_length);
                const Method *method = Analyser::get_method(klassName, methodName);
                if (i == 0)
                    mainm = method;
                methods[imi->method_index] = method;
            }
            const uint8_t *insts, *scopes_pc, *scopes_data;
            insts = (const uint8_t *)buffer;
            buffer += cmi->code_size;
            scopes_pc = (const uint8_t *)buffer;
            buffer += cmi->scopes_pc_size;
            scopes_data = (const uint8_t *)buffer;
            buffer += cmi->scopes_data_size;
            if (!mainm || !mainm->is_jportal())
            {
                std::cerr << "JvmRuntime error: Unknown or un-jportal section" << std::endl;
                break;
            }
            JitSection *section = new JitSection(insts, cmi->code_begin, cmi->stub_begin,
                                                 cmi->code_size, scopes_pc, cmi->scopes_pc_size,
                                                 scopes_data, cmi->scopes_data_size,
                                                 cmi->entry_point, cmi->verified_entry_point,
                                                 cmi->osr_entry_point, cmi->exception_begin,
                                                 cmi->unwind_begin, cmi->deopt_begin,
                                                 cmi->deopt_mh_begin, cmi->inline_method_cnt,
                                                 methods, mainm, mainm->get_name());
            if (_id_to_sections.count(section->id()) && _id_to_sections[section->id()] != section)
            {
                std::cerr << "JVMRuntime error: Section with the same id" << std::endl;
                break;
            } else {
                _id_to_sections[section->id()] = section;
            }
            _section_map[buffer] = section;
            break;
        }
        case _compiled_method_unload_info:
        {
            buffer += sizeof(CompiledMethodUnloadInfo);
            break;
        }
        case _thread_start_info:
        {
            const ThreadStartInfo *thi;
            thi = (const ThreadStartInfo *)buffer;
            buffer += sizeof(ThreadStartInfo);

            /* A potential bug: system tid might get reused */
            assert(!_tid_map.count(thi->sys_tid));
            _tid_map[thi->sys_tid] = thi->java_tid;
            break;
        }
        case _inline_cache_add_info:
        {
            const InlineCacheAddInfo *icai;
            icai = (const InlineCacheAddInfo *)buffer;
            buffer += sizeof(InlineCacheAddInfo);
            break;
        }
        case _inline_cache_clear_info:
        {
            const InlineCacheClearInfo *icci;
            icci = (const InlineCacheClearInfo *)buffer;
            buffer += sizeof(InlineCacheClearInfo);
            break;
        }
        default:
        {
            buffer = _end;
            std::cerr << "JvmRuntime error: Unknown dump type" << std::endl;
            exit(1);
        }
        }
        assert(info->size == buffer - event_start);
    }

    _initialized = true;
    return;
}

void JVMRuntime::destroy()
{
    for (auto section : _section_map)
        delete section.second;
    _section_map.clear();

    _entries.clear();
    _exits.clear();
    _points.clear();
    _takens.clear();
    _not_takens.clear();
    _bci_tables = {0, {0, 0}};
    _switch_tables = {0, {0, 0}};
    _switch_defaults.clear();
    _tid_map.clear();
    _id_to_methods.clear();
    _id_to_sections.clear();
    _deopts.clear();
    _ret_codes.clear();
    _throw_exceptions.clear();
    _rethrow_exceptions.clear();
    _handle_exceptions.clear();
    _pop_frames.clear();
    _earlyrets.clear();
    _non_invoke_rets.clear();
    _osrs.clear();
    _java_call_begins.clear();
    _java_call_ends.clear();

    delete[] _begin;

    _begin = nullptr;
    _end = nullptr;

    _initialized = false;
}

void JVMRuntime::print(uint8_t *buffer, uint64_t size)
{
    uint8_t *print_begin = buffer;
    uint8_t *print_end = buffer + size;
    while (buffer < print_end)
    {
        const DumpInfo *info;
        info = (const struct DumpInfo *)buffer;
        if (buffer + info->size > print_end)
        {
            std::cerr << "JVMRuntime error: print out of bounds" << std::endl;
            exit(1);
        }
        buffer += sizeof(DumpInfo);
        switch (info->type)
        {
        case _java_call_begin_info:
        {
            const JavaCallBeginInfo *jcbi;
            jcbi = (const JavaCallBeginInfo *)buffer;
            buffer += sizeof(JavaCallBeginInfo);
            std::cout << std::hex << "JavaCallBeginInfo: " << jcbi->addr << std::endl;
            break;
        }
        case _java_call_end_info:
        {
            const JavaCallEndInfo *jcei;
            jcei = (const JavaCallEndInfo *)buffer;
            buffer += sizeof(JavaCallEndInfo);
            std::cout << std::hex << "JavaCallEndInfo: " << jcei->addr << std::endl;
            break;
        }
        case _method_info:
        {
            const MethodInfo *mei;
            mei = (const MethodInfo *)buffer;
            buffer += sizeof(MethodInfo);
            std::string klass_name((const char *)buffer, mei->klass_name_length);
            buffer += mei->klass_name_length;
            std::string name((const char *)buffer, mei->method_name_length);
            buffer += mei->method_name_length;
            std::string sig((const char *)buffer, mei->method_signature_length);
            buffer += mei->method_signature_length;
            std::cout << std::hex << "MethodEntryInfo: " << mei->addr1 << " " << mei->addr2 << " " << mei->addr3 << " " << klass_name
                      << " " << name << " " << sig << " " << info->time << std::endl;
            break;
        }
        case _branch_taken_info:
        {
            const BranchTakenInfo *bti;
            bti = (const BranchTakenInfo *)buffer;
            buffer += sizeof(BranchTakenInfo);
            std::cout << std::hex << "BranchTakenInfo: " << bti->addr << " " << info->time << std::endl;
            break;
        }
        case _branch_not_taken_info:
        {
            const BranchNotTakenInfo *bnti;
            bnti = (const BranchNotTakenInfo *)buffer;
            buffer += sizeof(BranchNotTakenInfo);
            std::cout << std::hex << "BranchNotTakenInfo: " << bnti->addr << " " << info->time << std::endl;
            break;
        }
        case _switch_table_stub_info:
        {
            const SwitchTableStubInfo *stsi;
            stsi = (const SwitchTableStubInfo *)buffer;
            buffer += sizeof(SwitchTableStubInfo);
            std::cout << std::hex << "SwitchTableStubInfo: " << stsi->addr << " " << stsi->num
                      << " " << stsi->ssize << " " << info->time << std::endl;
            break;
        }
        case _switch_default_info:
        {
            const SwitchDefaultInfo *sdi;
            sdi = (const SwitchDefaultInfo *)buffer;
            buffer += sizeof(SwitchDefaultInfo);
            std::cout << std::hex << "SwitchDefaultInfo: " << sdi->addr << " " << info->time << std::endl;
            break;
        }
        case _bci_table_stub_info:
        {
            const BciTableStubInfo *btsi;
            btsi = (const BciTableStubInfo *)buffer;
            buffer += sizeof(BciTableStubInfo);
            std::cout << std::hex << "BciTableStubInfo: " << btsi->addr << " " << btsi->num
                      << " " << btsi->ssize << " " << info->time << std::endl;
            break;
        }
        case _osr_info:
        {
            const OSRInfo *osri;
            osri = (const OSRInfo *)buffer;
            buffer += sizeof(OSRInfo);
            std::cout << std::hex << "OSRInfo: " << osri->addr << std::endl;
            break;
        }
        case _throw_exception_info:
        {
            const ThrowExceptionInfo *tei;
            tei = (const ThrowExceptionInfo *)buffer;
            buffer += sizeof(ThrowExceptionInfo);
            std::cout << std::hex << "ThrowExceptionInfo: " << tei->addr << std::endl;
            break;
        }
        case _rethrow_exception_info:
        {
            const RethrowExceptionInfo *rei;
            rei = (const RethrowExceptionInfo *)buffer;
            buffer += sizeof(RethrowExceptionInfo);
            std::cout << std::hex << "RethrowExceptionInfo: " << rei->addr << std::endl;
            break;
        }
        case _handle_exception_info:
        {
            const HandleExceptionInfo *hei;
            hei = (const HandleExceptionInfo *)buffer;
            buffer += sizeof(HandleExceptionInfo);
            std::cout << std::hex << "HandleExceptionInfo: " << hei->addr << std::endl;
            break;
        }
        case _ret_code_info:
        {
            const RetCodeInfo *rci;
            rci = (const RetCodeInfo *)buffer;
            buffer += sizeof(RetCodeInfo);
            std::cout << std::hex << "RetCodeInfo: " << rci->addr << std::endl;
            break;
        }
        case _deoptimization_info:
        {
            const DeoptimizationInfo *di;
            di = (const DeoptimizationInfo *)buffer;
            buffer += sizeof(DeoptimizationInfo);
            std::cout << std::hex << "DeoptimizationInfo: " << di->addr << " " << info->time << std::endl;
            break;
        }
        case _non_invoke_ret_info:
        {
            const NonInvokeRetInfo *niri;
            niri = (const NonInvokeRetInfo *)buffer;
            buffer += sizeof(NonInvokeRetInfo);
            std::cout << std::hex << "NonInvokeRetInfo: " << niri->addr << std::endl;
            break;
        }
        case _pop_frame_info:
        {
            const PopFrameInfo *pfi;
            pfi = (const PopFrameInfo *)buffer;
            buffer += sizeof(PopFrameInfo);
            std::cout << std::hex << "PopFrameInfo: " << pfi->addr << std::endl;
            break;
        }
        case _earlyret_info:
        {
            const EarlyretInfo *ei;
            ei = (const EarlyretInfo *)buffer;
            buffer += sizeof(EarlyretInfo);
            std::cout << std::hex << "EarlyretInfo: " << ei->addr << std::endl;
            break;
        }
        case _compiled_method_load_info:
        {
            const CompiledMethodLoadInfo *cmi;
            cmi = (const CompiledMethodLoadInfo *)buffer;
            buffer += sizeof(CompiledMethodLoadInfo);
            std::cout << std::hex << "CompiledMethodLoad: " << cmi->code_begin << " " << cmi->code_size
                      << " " << cmi->stub_begin << " " << cmi->entry_point
                      << " " << cmi->verified_entry_point << " " << cmi->osr_entry_point
                      << " " << info->time << std::endl;
            for (int i = 0; i < cmi->inline_method_cnt; i++)
            {
                const InlineMethodInfo *imi;
                imi = (const InlineMethodInfo *)buffer;
                buffer += sizeof(InlineMethodInfo);
                const char *klass_name = (const char *)buffer;
                buffer += imi->klass_name_length;
                const char *name = (const char *)buffer;
                buffer += imi->method_name_length;
                const char *sig = (const char *)buffer;
                buffer += imi->method_signature_length;
                std::string klassName = std::string(klass_name, imi->klass_name_length);
                std::string methodName = std::string(name, imi->method_name_length) + std::string(sig, imi->method_signature_length);
                std::cout << "    "
                          << "Method: " << imi->method_index << " "
                          << klassName << " " << methodName << std::endl;
            }
            const uint8_t *insts, *scopes_pc, *scopes_data;
            insts = (const uint8_t *)buffer;
            buffer += cmi->code_size;
            scopes_pc = (const uint8_t *)buffer;
            buffer += cmi->scopes_pc_size;
            scopes_data = (const uint8_t *)buffer;
            buffer += cmi->scopes_data_size;
            break;
        }
        case _compiled_method_unload_info:
        {
            const CompiledMethodUnloadInfo *cmui = (const CompiledMethodUnloadInfo *)buffer;
            buffer += sizeof(CompiledMethodUnloadInfo);
            std::cout << std::hex << "Compiled Method Unload " << cmui->code_begin << " " << info->time << std::endl;
            break;
        }
        case _thread_start_info:
        {
            const ThreadStartInfo *ths = (const ThreadStartInfo *)buffer;
            buffer += sizeof(ThreadStartInfo);
            std::cout << std::hex << "Thread Start " << ths->java_tid << " " << ths->sys_tid
                      << " " << info->time << std::endl;
            break;
        }
        case _inline_cache_add_info:
        {
            const InlineCacheAddInfo *icai = (const InlineCacheAddInfo *)buffer;
            std::cout << std::hex << "Inline cache Add " << icai->src << " " << icai->dest
                      << " " << info->time << std::endl;
            buffer += sizeof(InlineCacheAddInfo);
            break;
        }
        case _inline_cache_clear_info:
        {
            const InlineCacheClearInfo *icci = (const InlineCacheClearInfo *)buffer;
            std::cout << std::hex << "Inline cache Clear " << icci->src
                      << " " << info->time << std::endl;
            buffer += sizeof(InlineCacheClearInfo);
            break;
        }
        default:
        {
            /* error */
            std::cerr << "JVMRuntime error: Unknown tpye" << std::endl;
            buffer = print_end;
            exit(1);
        }
        }
    }
}

void JVMRuntime::output(std::string prefix)
{
    std::ofstream file(prefix+"-methods");
    for  (auto method : _id_to_methods)
    {
        file << method.first << " " << method.second->get_klass()->get_name()
             << " " << method.second->get_name() << std::endl;
    }
}

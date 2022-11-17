#include "java/analyser.hpp"
#include "pt/pt.hpp"
#include "runtime/jit_image.hpp"
#include "runtime/jit_section.hpp"
#include "runtime/jvm_runtime.hpp"

#include <cassert>
#include <iostream>

uint8_t *JVMRuntime::_begin = nullptr;
uint8_t *JVMRuntime::_end = nullptr;
std::map<uint64_t, const Method *> JVMRuntime::_entry_map;
std::set<uint64_t> JVMRuntime::_exits;
std::set<uint64_t> JVMRuntime::_takens;
std::set<uint64_t> JVMRuntime::_not_takens;
std::set<std::pair<uint64_t, std::pair<int, int>>> JVMRuntime::_switch_cases;
std::set<uint64_t> JVMRuntime::_switch_defaults;
std::set<uint64_t> JVMRuntime::_invoke_sites;
std::map<uint64_t, uint64_t> JVMRuntime::_tid_map;
std::map<const uint8_t *, JitSection *> JVMRuntime::_section_map;
bool JVMRuntime::_initialized = false;

JVMRuntime::JVMRuntime()
{
    assert(_initialized);
    _current = _begin;
}

JVMRuntime::~JVMRuntime()
{
    _current = nullptr;
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

void JVMRuntime::initialize(uint8_t *buffer, uint64_t size, Analyser *analyser)
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
        case _method_entry_info:
        {
            const MethodEntryInfo *mei;
            mei = (const MethodEntryInfo *)buffer;
            buffer += sizeof(MethodEntryInfo);
            std::string klass_name((const char *)buffer, mei->klass_name_length);
            buffer += mei->klass_name_length;
            std::string name((const char *)buffer, mei->method_name_length);
            buffer += mei->method_name_length;
            std::string sig((const char *)buffer, mei->method_signature_length);
            buffer += mei->method_signature_length;
            const Method *method = analyser->get_method(klass_name, name + sig);
            _entry_map[mei->addr] = method;
            break;
        }
        case _method_exit_info:
        {
            const MethodExitInfo *mei;
            mei = (const MethodExitInfo *)buffer;
            buffer += sizeof(MethodExitInfo);
            _exits.insert(mei->addr);
            break;
        }
        case _branch_taken_info:
        {
            const BranchTakenInfo *bti;
            bti = (const BranchTakenInfo *)buffer;
            buffer += sizeof(BranchTakenInfo);
            _takens.insert(bti->addr);
            break;
        }
        case _branch_not_taken_info:
        {
            const BranchNotTakenInfo *bnti;
            bnti = (const BranchNotTakenInfo *)buffer;
            buffer += sizeof(BranchNotTakenInfo);
            _not_takens.insert(bnti->addr);
            break;
        }
        case _switch_case_info:
        {
            const SwitchCaseInfo *sci;
            sci = (const SwitchCaseInfo *)buffer;
            buffer += sizeof(SwitchCaseInfo);
            _switch_cases.insert({sci->addr, {sci->num, sci->ssize}});
            break;
        }
        case _switch_default_info:
        {
            const SwitchDefaultInfo *sdi;
            sdi = (const SwitchDefaultInfo *)buffer;
            buffer += sizeof(SwitchDefaultInfo);
            _switch_defaults.insert({sdi->addr});
            break;
        }
        case _invoke_site_info:
        {
            const InvokeSiteInfo *isi;
            isi = (const InvokeSiteInfo *)buffer;
            buffer += sizeof(InvokeSiteInfo);
            _invoke_sites.insert(isi->addr);
            break;
        }
        case _exception_handling_info:
        {
            const ExceptionHandlingInfo *ehi;
            ehi = (const ExceptionHandlingInfo *)buffer;
            buffer += sizeof(ExceptionHandlingInfo);
            break;
        }
        case _deoptimization_info:
        {
            const DeoptimizationInfo *di;
            di = (const DeoptimizationInfo *)buffer;
            buffer += sizeof(DeoptimizationInfo);
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
                const Method *method = analyser->get_method(klassName, methodName);
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
                std::cerr << "JvmDumpDecoder error: Unknown or un-jportal section" << std::endl;
                break;
            }
            JitSection *section = new JitSection(insts, cmi->code_begin, cmi->stub_begin,
                                                 cmi->code_size, scopes_pc, cmi->scopes_pc_size,
                                                 scopes_data, cmi->scopes_data_size,
                                                 cmi->entry_point, cmi->verified_entry_point,
                                                 cmi->osr_entry_point, cmi->exception_begin,
                                                 cmi->deopt_begin, cmi->deopt_mh_begin,
                                                 cmi->inline_method_cnt,
                                                 methods, mainm, mainm->get_name());
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
            std::cerr << "JvmDumpDecoder error: Unknown dump type" << std::endl;
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

    _entry_map.clear();

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
        case _method_entry_info:
        {
            const MethodEntryInfo *mei;
            mei = (const MethodEntryInfo *)buffer;
            buffer += sizeof(MethodEntryInfo);
            std::string klass_name((const char *)buffer, mei->klass_name_length);
            buffer += mei->klass_name_length;
            std::string name((const char *)buffer, mei->method_name_length);
            buffer += mei->method_name_length;
            std::string sig((const char *)buffer, mei->method_signature_length);
            buffer += mei->method_signature_length;
            std::cout << "MethodEntryInfo: " << mei->addr << " " << klass_name
                      << " " << name << " " << sig << std::endl;
            break;
        }
        case _method_exit_info:
        {
            const MethodExitInfo *mei;
            mei = (const MethodExitInfo *)buffer;
            buffer += sizeof(MethodExitInfo);
            std::cout << "MethodExitInfo: " << mei->addr << std::endl;
            break;
        }
        case _branch_taken_info:
        {
            const BranchTakenInfo *bti;
            bti = (const BranchTakenInfo *)buffer;
            buffer += sizeof(BranchTakenInfo);
            std::cout << "BranchTakenInfo: " << bti->addr << std::endl;
            break;
        }
        case _branch_not_taken_info:
        {
            const BranchNotTakenInfo *bnti;
            bnti = (const BranchNotTakenInfo *)buffer;
            buffer += sizeof(BranchNotTakenInfo);
            std::cout << "BranchNotTakenInfo: " << bnti->addr << std::endl;
            break;
        }
        case _switch_case_info:
        {
            const SwitchCaseInfo *sci;
            sci = (const SwitchCaseInfo *)buffer;
            buffer += sizeof(SwitchCaseInfo);
            std::cout << "SwitchCaseInfo: " << sci->addr << " " << sci->num << " " << sci->ssize << std::endl;
            break;
        }
        case _switch_default_info:
        {
            const SwitchDefaultInfo *sdi;
            sdi = (const SwitchDefaultInfo *)buffer;
            buffer += sizeof(SwitchDefaultInfo);
            std::cout << "SwitchDefaultInfo: " << sdi->addr << std::endl;
            break;
        }
        case _invoke_site_info:
        {
            const InvokeSiteInfo *isi;
            isi = (const InvokeSiteInfo *)buffer;
            buffer += sizeof(InvokeSiteInfo);
            std::cout << "InvokeSiteInfo: " << isi->addr <<std::endl;
            break;
        }
        case _exception_handling_info:
        {
            const ExceptionHandlingInfo *ehi;
            ehi = (const ExceptionHandlingInfo *)buffer;
            buffer += sizeof(ExceptionHandlingInfo);
            std::cout << "ExceptionHandlingInfo: " << ehi->addr << " " << ehi->current_bci
                      << " " << ehi->handler_bci << " " << ehi->java_tid << std::endl;
            break;
        }
        case _deoptimization_info:
        {
            const DeoptimizationInfo *di;
            di = (const DeoptimizationInfo *)buffer;
            buffer += sizeof(DeoptimizationInfo);
            std::cout << "Deoptimization: " << di->addr << " " << di->bci
                      << " " << di->java_tid << std::endl;
            break;
        }
        case _compiled_method_load_info:
        {
            buffer += (info->size - sizeof(DumpInfo));
            const CompiledMethodLoadInfo *cmi;
            cmi = (const CompiledMethodLoadInfo *)buffer;
            buffer += sizeof(CompiledMethodLoadInfo);
            std::cout << "CompiledMethodLoad: " << cmi->code_begin << " " << cmi->code_size
                      << " " << cmi->stub_begin << " " << cmi->entry_point
                      << " " << cmi->verified_entry_point << " " << cmi->osr_entry_point
                      << " " << cmi->exception_begin << " " << cmi->deopt_begin << std::endl;
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
            std::cout << "Compiled Method Unload " << cmui->code_begin << std::endl;
            break;
        }
        case _thread_start_info:
        {
            const ThreadStartInfo *ths = (const ThreadStartInfo *)buffer;
            buffer += sizeof(ThreadStartInfo);
            std::cout << "Thread Start " << ths->java_tid << " " << ths->sys_tid << std::endl;
            break;
        }
        case _inline_cache_add_info:
        {
            const InlineCacheAddInfo *icai = (const InlineCacheAddInfo *)buffer;
            std::cout << "Inline cache Add " << icai->src << " " << icai->dest << std::endl;
            buffer += sizeof(InlineCacheAddInfo);
            break;
        }
        case _inline_cache_clear_info:
        {
            const InlineCacheClearInfo *icci = (const InlineCacheClearInfo *)buffer;
            std::cout << "Inline cache Clear " << icci->src << std::endl;
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

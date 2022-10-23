#include "java/analyser.hpp"
#include "runtime/jit_image.hpp"
#include "runtime/jit_section.hpp"
#include "runtime/jvm_runtime.hpp"

#include <cassert>
#include <iostream>

uint8_t *JVMRuntime::begin = nullptr;
uint8_t *JVMRuntime::end = nullptr;
const JVMRuntime::CodeletsInfo *JVMRuntime::entries = nullptr;
std::map<uint32_t, uint32_t> JVMRuntime::thread_map;
std::map<int, const Method *> JVMRuntime::md_map;
std::map<const uint8_t *, JitSection *> JVMRuntime::section_map;
bool JVMRuntime::initialized = false;

JVMRuntime::JVMRuntime()
{
    assert(initialized);
    _image = new JitImage("jitted-code");
    _current = begin;
}

JVMRuntime::~JVMRuntime() {
    delete _image;
    _image = nullptr;
}

void JVMRuntime::move_on(uint64_t time)
{
    const DumpInfo *info;
    while (_current < end)
    {
        info = (const struct DumpInfo *)_current;
        if (_current + info->size > end || info->time > time)
            return;
        _current += sizeof(DumpInfo);
        switch (info->type)
        {
        case _codelet_info:
        {
            _current += sizeof(CodeletsInfo);
            break;
        }
        case _method_entry_initial_info:
        {
            const MethodEntryInitial *me;
            me = (const MethodEntryInitial *)_current;
            _current += sizeof(MethodEntryInitial);
            const char *klass_name = (const char *)_current;
            _current += me->klass_name_length;
            const char *name = (const char *)_current;
            _current += me->method_name_length;
            const char *signature = (const char *)_current;
            _current += me->method_signature_length;
            break;
        }
        case _method_entry_info:
        {
            const MethodEntryInfo *me;
            me = (const MethodEntryInfo *)_current;
            _current += sizeof(MethodEntryInfo);
            break;
        }
        case _compiled_method_load_info:
        {
            _current += (info->size - sizeof(DumpInfo));
            if (section_map.count(_current))
                _image->add(section_map[_current]);
            break;
        }
        case _compiled_method_unload_info:
        {
            const CompiledMethodUnloadInfo *cmu = (const CompiledMethodUnloadInfo *)_current;
            _current += sizeof(CompiledMethodUnloadInfo);
            _image->remove(cmu->code_begin);
            break;
        }
        case _thread_start_info:
        {
            _current += sizeof(ThreadStartInfo);
            break;
        }
        case _inline_cache_add_info:
        {
            const InlineCacheAdd *ica = (const InlineCacheAdd *)_current;
            JitSection *section = _image->find(ica->src);
            if (section)
                _ics[{ica->src, section}] = ica->dest;
            else
                std::cerr << "JVMRuntime error: Add inline cache to unknown "
                          << ica->src << " " << ica->dest << std::endl;
            _current += sizeof(InlineCacheAdd);
            break;
        }
        case _inline_cache_clear_info:
        {
            const InlineCacheClear *icc = (const InlineCacheClear *)_current;
            JitSection *section = _image->find(icc->src);
            if (section)
                _ics.erase({icc->src, section});
            else
                std::cerr << "JVMRuntime error: Clear inline cache to unknown "
                          << icc->src << std::endl;
            _current += sizeof(InlineCacheClear);
            break;
        }
        default:
        {
            /* error */
            _current = end;
            return;
        }
        }
    }
}

uint32_t JVMRuntime::get_java_tid(uint32_t tid)
{
    auto iter = thread_map.find(tid);
    if (iter == thread_map.end())
        return -1;
    return iter->second;
}

JVMRuntime::Codelet JVMRuntime::match(uint64_t ip, Bytecodes::Code &code, JitSection *&section)
{
    if (ip >= entries->_normal_table[0][0] && ip < entries->_wentry_point[0])
    {
        int low = 0, high = JVMRuntime::dispatch_length * JVMRuntime::number_of_states - 1;
        while (low <= high)
        {
            int mid = (low + high) / 2;
            uint64_t addr = entries->_normal_table[mid / JVMRuntime::number_of_states][mid % JVMRuntime::number_of_states];
            if (addr == ip)
            {
                code = Bytecodes::cast(mid / JVMRuntime::number_of_states);
                return _bytecode;
            }
            else if (addr > ip)
            {
                high = mid - 1;
            }
            else
            {
                low = mid + 1;
            }
        }
        return _illegal;
    }

    if (ip >= entries->_wentry_point[0] && ip < entries->_deopt_entry[0][0])
    {
        int low = 0, high = JVMRuntime::dispatch_length - 1;
        while (low <= high)
        {
            int mid = (low + high) / 2;
            uint64_t addr = entries->_wentry_point[mid];
            if (addr == ip)
            {
                code = Bytecodes::cast(mid);
                return _bytecode;
            }
            else if (addr > ip)
            {
                high = mid - 1;
            }
            else
            {
                low = mid + 1;
            }
        }
        return _illegal;
    }

    if (ip < entries->_unimplemented_bytecode_entry)
        return _illegal;

    if (ip == entries->_slow_signature_handler)
        return _slow_signature_handler;

    if (ip == entries->_unimplemented_bytecode_entry)
        return _unimplemented_bytecode;

    if (ip == entries->_illegal_bytecode_sequence_entry)
        return _illegal_bytecode_sequence;

    if (ip >= entries->_return_entry[0][0] && ip < entries->_invoke_return_entry[0])
        return _return;

    if (ip >= entries->_invoke_return_entry[0] && ip < entries->_invokeinterface_return_entry[0])
        return _invoke_return;

    if (ip >= entries->_invokeinterface_return_entry[0] && ip < entries->_invokedynamic_return_entry[0])
        return _invokeinterface_return;

    if (ip >= entries->_invokedynamic_return_entry[0] && ip < entries->_earlyret_entry[0])
        return _invokedynamic_return;

    if (ip >= entries->_earlyret_entry[0] && ip < entries->_native_abi_to_tosca[0])
        return _earlyret;

    if (ip >= entries->_native_abi_to_tosca[0] && ip < entries->_rethrow_exception_entry)
        return _result_handlers_for_native_calls;

    if (ip >= entries->_entry_table[0] && ip < entries->_normal_table[0][0])
        return _method_entry_point;

    if (ip >= entries->_deopt_entry[0][0] && ip < entries->_deopt_reexecute_return_entry)
        return _deopt;

    if (ip == entries->_rethrow_exception_entry)
        return _rethrow_exception;

    if (ip == entries->_throw_exception_entry)
        return _throw_exception;

    if (ip == entries->_remove_activation_preserving_args_entry)
        return _remove_activation_preserving_args;

    if (ip == entries->_remove_activation_entry)
        return _remove_activation;

    if (ip == entries->_throw_ArrayIndexOutOfBoundsException_entry)
        return _throw_ArrayIndexOutOfBoundsException;

    if (ip == entries->_throw_ArrayStoreException_entry)
        return _throw_ArrayStoreException;

    if (ip == entries->_throw_ArithmeticException_entry)
        return _throw_ArithmeticException;

    if (ip == entries->_throw_ClassCastException_entry)
        return _throw_ClassCastException;

    if (ip == entries->_throw_NullPointerException_entry)
        return _throw_NullPointerException;

    if (ip == entries->_throw_StackOverflowError_entry)
        return _throw_StackOverflowError;

    if (ip == entries->_deopt_reexecute_return_entry)
        return _deopt_reexecute_return;

    section = _image->find(ip);
    if (!section)
        return _illegal;
    return _jitcode;
}

void JVMRuntime::initialize(uint8_t *buffer, uint64_t size, Analyser *analyser)
{
    begin = buffer;
    end = buffer + size;
    const DumpInfo *info;
    while (buffer < end)
    {
        uint8_t *event_start = buffer;
        info = (const struct DumpInfo *)buffer;
        if (buffer + info->size > end)
        {
            std::cerr << "JVMRuntime error: Read JVMRuntime info" << std::endl;
            break;
        }
        buffer += sizeof(DumpInfo);
        switch (info->type)
        {
        case _codelet_info:
        {
            entries = (CodeletsInfo *)buffer;
            buffer += sizeof(CodeletsInfo);
            break;
        }
        case _method_entry_initial_info:
        {
            const MethodEntryInitial *me;
            me = (const MethodEntryInitial *)buffer;
            buffer += sizeof(MethodEntryInitial);
            const char *klass_name = (const char *)buffer;
            buffer += me->klass_name_length;
            const char *name = (const char *)buffer;
            buffer += me->method_name_length;
            const char *sig = (const char *)buffer;
            buffer += me->method_signature_length;
            std::string klassName = std::string(klass_name, me->klass_name_length);
            std::string methodName = std::string(name, me->method_name_length) + std::string(sig, me->method_signature_length);
            const Method *method = analyser->get_method(klassName, methodName);
            md_map[me->idx] = method;
            break;
        }
        case _method_entry_info:
        {
            const MethodEntryInfo *me;
            me = (const MethodEntryInfo *)buffer;
            buffer += sizeof(MethodEntryInfo);
            break;
        }
        case _compiled_method_load_info:
        {
            const CompiledMethodLoadInfo *cm;
            cm = (const CompiledMethodLoadInfo *)buffer;
            buffer += sizeof(CompiledMethodLoadInfo);
            const Method *mainm = nullptr;
            std::map<int, const Method *> methods;
            for (int i = 0; i < cm->inline_method_cnt; i++)
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
            buffer += cm->code_size;
            scopes_pc = (const uint8_t *)buffer;
            buffer += cm->scopes_pc_size;
            scopes_data = (const uint8_t *)buffer;
            buffer += cm->scopes_data_size;
            if (!mainm || !mainm->is_jportal())
            {
                std::cerr << "JvmDumpDecoder error: Unknown or un-jportal section" << std::endl;
                break;
            }
            JitSection *section = new JitSection(insts, cm->code_begin, cm->code_size,
                                                 scopes_pc, cm->scopes_pc_size,
                                                 scopes_data, cm->scopes_data_size,
                                                 cm->entry_point, cm->verified_entry_point,
                                                 cm->osr_entry_point, cm->inline_method_cnt,
                                                 methods, mainm, mainm->get_name());
            section_map[buffer] = section;
            break;
        }
        case _compiled_method_unload_info:
        {
            buffer += sizeof(CompiledMethodUnloadInfo);
            break;
        }
        case _thread_start_info:
        {
            const ThreadStartInfo *th;
            th = (const ThreadStartInfo *)buffer;
            buffer += sizeof(ThreadStartInfo);
            thread_map[th->sys_tid] = th->java_tid;
            break;
        }
        case _inline_cache_add_info:
        {
            const InlineCacheAdd *ic;
            ic = (const InlineCacheAdd *)buffer;
            buffer += sizeof(InlineCacheAdd);
            break;
        }
        case _inline_cache_clear_info:
        {
            const InlineCacheClear *ic;
            ic = (const InlineCacheClear *)buffer;
            buffer += sizeof(InlineCacheClear);
            break;
        }
        default:
        {
            buffer = end;
            std::cerr << "JvmDumpDecoder error: Unknown dump type" << std::endl;
            return;
        }
        }
        assert(info->size == buffer - event_start);
    }

    initialized = true;
    return;
}

void JVMRuntime::destroy()
{
    for (auto section : section_map)
        delete section.second;
    section_map.clear();

    md_map.clear();

    delete[] begin;

    begin = nullptr;
    end = nullptr;
    entries = nullptr;

    initialized = false;
}

void JVMRuntime::print()
{
    assert(initialized);
    uint8_t *buffer = begin;
    while (buffer < end)
    {
        const DumpInfo *info;
        info = (const struct DumpInfo *)buffer;
        if (buffer + info->size > end) {
            std::cerr << "JVMRuntime error: print out of bounds" << std::endl;
            break;
        }
        buffer += sizeof(DumpInfo);
        switch (info->type)
        {
        case _codelet_info:
        {
            buffer += sizeof(CodeletsInfo);
            std::cout << "CodeletsInfo " << entries->_low_bound << " " << entries->_high_bound << std::endl;
            break;
        }
        case _method_entry_initial_info:
        {
            const MethodEntryInitial *me;
            me = (const MethodEntryInitial *)buffer;
            buffer += sizeof(MethodEntryInitial);
            std::string klass_name((const char *)buffer, me->klass_name_length);
            buffer += me->klass_name_length;
            std::string name((const char *)buffer, me->method_name_length);
            buffer += me->method_name_length;
            std::string sig((const char*)buffer, me->method_signature_length);
            buffer += me->method_signature_length;
            std::cout << "MethodEntryInitial: " << me->idx << " " << me->tid << " "
                      << klass_name << " " << name << " " << sig << std::endl;
            break;
        }
        case _method_entry_info:
        {
            const MethodEntryInfo *me;
            me = (const MethodEntryInfo *)buffer;
            buffer += sizeof(MethodEntryInfo);
            std::cout << "MethodEntryInitial: " << me->idx << " " << me->tid << std::endl;
            break;
        }
        case _compiled_method_load_info:
        {
            buffer += (info->size - sizeof(DumpInfo));
            if (section_map.count(buffer)) {
                JitSection* section = section_map[buffer];
                const Method *mainm = section->mainm();
                if (!mainm || !mainm->is_jportal())
                {
                    std::cerr << "JVMRuntime error: Non jportal method in compiled code" << std::endl;
                    break;
                }
                std::cout << "Compiled Method Load " << section->code_begin() << " "
                          << section->code_size() << " " << section->entry_point()
                          << " " << mainm->get_klass()->get_name() << " "
                          << mainm->get_name() << std::endl;
            } else {
                std::cerr << "JVMRuntime error: Print Compiled Method Error" << std::endl;
            }
            break;
        }
        case _compiled_method_unload_info:
        {
            const CompiledMethodUnloadInfo *cmu = (const CompiledMethodUnloadInfo *)buffer;
            buffer += sizeof(CompiledMethodUnloadInfo);
            std::cout << "Compiled Method Unload " << cmu->code_begin << std::endl;
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
            const InlineCacheAdd *ica = (const InlineCacheAdd *)buffer;
            std::cout << "Inline cache Add " << ica->src << " " << ica->dest << std::endl;
            buffer += sizeof(InlineCacheAdd);
            break;
        }
        case _inline_cache_clear_info:
        {
            const InlineCacheClear *icc = (const InlineCacheClear *)buffer;
            std::cout << "Inline cache Clear " << icc->src << std::endl;
            buffer += sizeof(InlineCacheClear);
            break;
        }
        default:
        {
            /* error */
            buffer = end;
            return;
        }
        }
    }
}

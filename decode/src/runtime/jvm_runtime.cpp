#include "java/analyser.hpp"
#include "runtime/codelets_entry.hpp"
#include "runtime/jit_image.hpp"
#include "runtime/jit_section.hpp"
#include "runtime/jvm_runtime.hpp"

#include <cassert>
#include <iostream>

uint8_t *JVMRuntime::begin = nullptr;
uint8_t *JVMRuntime::end = nullptr;
std::map<uint32_t, uint32_t> JVMRuntime::thread_map;
std::map<int, const Method*> JVMRuntime::md_map;
std::map<const uint8_t *, JitSection *> JVMRuntime::section_map;
bool JVMRuntime::_initialized = false;

JVMRuntime::JVMRuntime() {
    assert(_initialized);
    _image = new JitImage("jitted-code");
    _current = begin;
}

void JVMRuntime::move_on(uint64_t time) {
    const DumpInfo *info;
    while (_current < end) {
        info = (const struct DumpInfo *)_current;
        if (_current + info->size > end || info->time > time)
           return;
        _current += sizeof(DumpInfo);
        switch(info->type) {
            case _codelet_info: {
                _current += sizeof(CodeletsInfo);
                break;
            }
            case _method_entry_initial: {
                const MethodEntryInitial* me;
                me = (const MethodEntryInitial*)_current;
                _current += sizeof(MethodEntryInitial);
                const char *klass_name = (const char *)_current;
                _current += me->klass_name_length;
                const char *name = (const char *)_current;
                _current += me->method_name_length;
                const char *signature = (const char *)_current;
                _current += me->method_signature_length;
                break;
            }
            case _method_entry: {
                const MethodEntryInfo* me;
                me = (const MethodEntryInfo*)_current;
                _current += sizeof(MethodEntryInfo);
                break;
            }
            case _compiled_method_load: {
                _current += (info->size - sizeof(DumpInfo));
                if (section_map.count(_current))
                    _image->add(section_map[_current]);
                break;
            }
            case _compiled_method_unload: {
                const CompiledMethodUnloadInfo *cmu = (const CompiledMethodUnloadInfo*)_current;
                _current += sizeof(CompiledMethodUnloadInfo);
                _image->remove(cmu->code_begin);
                break;
            }
            case _thread_start: {
                _current += sizeof(ThreadStartInfo);
                break;
            }
            case _inline_cache_add: {
                const InlineCacheAdd* ica = (const InlineCacheAdd*)_current;
                JitSection* section = _image->find(ica->src);
                if (section)
                    _ics[{ica->src, section}] = ica->dest;
                _current += sizeof(InlineCacheAdd);
                break;
            }
            case _inline_cache_clear: {
                const InlineCacheClear* icc = (const InlineCacheClear*)_current;
                JitSection* section = _image->find(icc->src);
                if (section)
                    _ics.erase({icc->src, section});
                _current += sizeof(InlineCacheClear);
                break;
            }
            default: {                
                /* error */

                _current = end;
                return;
            }
        }
    }
}

uint32_t JVMRuntime::get_java_tid(uint32_t tid) {
    auto iter = thread_map.find(tid);
    if (iter == thread_map.end())
        return -1;
    return iter->second;
}

void JVMRuntime::initialize(uint8_t *buffer, uint64_t size, Analyser* analyser) {
    begin = buffer;
    end = buffer + size;
    const DumpInfo *info;
    while (buffer < end) {
        info = (const struct DumpInfo *)buffer;
        if (buffer + info->size > end)
           break;
        buffer += sizeof(DumpInfo);
        switch(info->type) {
            case _codelet_info: {
                CodeletsInfo* entries = (CodeletsInfo *)buffer;
                buffer += sizeof(CodeletsInfo);
                CodeletsEntry::initialize(entries);
                break;
            }
            case _method_entry_initial: {
                const MethodEntryInitial* me;
                me = (const MethodEntryInitial*)buffer;
                buffer += sizeof(MethodEntryInitial);
                const char *klass_name = (const char *)buffer;
                buffer += me->klass_name_length;
                const char *name = (const char *)buffer;
                buffer += me->method_name_length;
                const char *sig = (const char *)buffer;
                buffer += me->method_signature_length;
                std::string klassName = std::string(klass_name, me->klass_name_length);
                std::string methodName = std::string(name, me->method_name_length)+std::string(sig, me->method_signature_length);
                const Method* method = analyser->get_method(klassName, methodName);
                md_map[me->idx] = method;
                break;
            }
            case _method_entry: {
                const MethodEntryInfo* me;
                me = (const MethodEntryInfo*)buffer;
                buffer += sizeof(MethodEntryInfo);
                break;
            }
            case _compiled_method_load: {
                const CompiledMethodLoadInfo *cm;
                cm = (const CompiledMethodLoadInfo*)buffer;
                buffer += sizeof(CompiledMethodLoadInfo);
                const Method* mainm = nullptr;
                std::map<int, const Method*> methods;
                for (int i = 0; i < cm->inline_method_cnt; i++) {
                    const InlineMethodInfo*imi;
                    imi = (const InlineMethodInfo*)buffer;
                    buffer += sizeof(InlineMethodInfo);
                    const char *klass_name = (const char *)buffer;
                    buffer += imi->klass_name_length;
                    const char *name = (const char *)buffer;
                    buffer += imi->method_name_length;
                    const char *sig = (const char *)buffer;
                    buffer += imi->method_signature_length;
                    std::string klassName = std::string(klass_name, imi->klass_name_length);
                    std::string methodName = std::string(name, imi->method_name_length)+std::string(sig, imi->method_signature_length);
                    const Method* method = analyser->get_method(klassName, methodName);
                    if (i == 0) mainm = method;
                    methods[imi->method_index] = method;
                }
                const uint8_t *insts, *scopes_pc, *scopes_data;
                insts = (const uint8_t *)buffer;
                buffer += cm->code_size;
                scopes_pc = (const uint8_t *)buffer;
                buffer += cm->scopes_pc_size;
                scopes_data = (const uint8_t *)buffer;
                buffer += cm->scopes_data_size;
                if (!mainm || !mainm->is_jportal()) {
                    std::cerr << "JvmDumpDecoder: unknown or un-jportal section" << std::endl;
                    break;
                }
                CompiledMethodDesc *cmd = new CompiledMethodDesc(cm->entry_point, cm->verified_entry_point,
                      cm->osr_entry_point, cm->inline_method_cnt, mainm, methods);
                JitSection *section = new JitSection(insts, cm->code_begin, cm->code_size,
                                                     scopes_pc, cm->scopes_pc_size,
                                                     scopes_data, cm->scopes_data_size, cmd, nullptr);
                section_map[buffer] = section;
                break;
            }
            case _compiled_method_unload: {
                buffer += sizeof(CompiledMethodUnloadInfo);
                break;
            }
            case _thread_start: {
                const ThreadStartInfo *th;
                th = (const ThreadStartInfo*)buffer;
                buffer += sizeof(ThreadStartInfo);
                thread_map[th->sys_tid] = th->java_tid;
                break;
            }
            case _inline_cache_add: {
                const InlineCacheAdd *ic;
                ic = (const InlineCacheAdd*)buffer;
                buffer += sizeof(InlineCacheAdd);
                break;
            }
            case _inline_cache_clear: {
                const InlineCacheClear *ic;
                ic = (const InlineCacheClear*)buffer;
                buffer += sizeof(InlineCacheClear);
                break;
            }
            default: {
                buffer = end;
                std::cerr << "JvmDumpDecoder: unknown dump type" << std::endl;
                return;
            }
        }
    }

    _initialized = true;
    return;
}

void JVMRuntime::destroy() {
    for (auto section : section_map)
        delete section.second;
    section_map.clear();

    md_map.clear();

    delete[] begin;

    begin = nullptr;
    end = nullptr;

    _initialized = false;
}

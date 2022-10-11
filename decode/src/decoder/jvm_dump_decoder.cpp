#include "decoder/jvm_dump_decoder.hpp"
#include "java/analyser.hpp"
#include "java/method.hpp"
#include "utilities/load_file.hpp"
#include "runtime/jit_section.hpp"
#include "runtime/codelets_entry.hpp"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

uint8_t *JvmDumpDecoder::begin = nullptr;
uint8_t *JvmDumpDecoder::end = nullptr;
map<long, long> JvmDumpDecoder::thread_map;
map<int, const Method*> JvmDumpDecoder::md_map;
map<const uint8_t *, JitSection *> JvmDumpDecoder::section_map;

JvmDumpDecoder::DumpInfoType JvmDumpDecoder::dumper_event(uint64_t time, long tid,
                                                const void *&data) {
    const DumpInfo *info;
    data = nullptr;
    if (current < end) {
        info = (const struct DumpInfo *)current;
        if (current + info->size > end || info->time > time)
           return _illegal;
        current += sizeof(DumpInfo);
        switch(info->type) {
            case _codelet_info: {
                data = current;
                current += sizeof(CodeletsInfo);
                return _codelet_info;
            }
            case _method_entry_initial: {
                const MethodEntryInitial* me;
                me = (const MethodEntryInitial*)current;
                current += sizeof(MethodEntryInitial);
                const char *klass_name = (const char *)current;
                current += me->klass_name_length;
                const char *name = (const char *)current;
                current += me->method_name_length;
                const char *signature = (const char *)current;
                current += me->method_signature_length;
                auto iter1 = thread_map.find(tid);
                if (iter1 == thread_map.end() || iter1->second != me->tid)
                    return _no_thing;
                auto iter = md_map.find(me->idx);
                if (iter == md_map.end())
                    return _no_thing;
                data = iter->second;
                return _method_entry;
            }
            case _method_entry: {
                const MethodEntryInfo* me;
                me = (const MethodEntryInfo*)current;
                current += sizeof(MethodEntryInfo);
                auto iter1 = thread_map.find(tid);
                if (iter1 == thread_map.end() || iter1->second != me->tid)
                    return _no_thing;
                auto iter = md_map.find(me->idx);
                if (iter == md_map.end())
                    return _no_thing;
                data = iter->second;
                return _method_entry;
            }
            case _method_exit: {
                const MethodExitInfo* me;
                me = (const MethodExitInfo*)current;
                current += sizeof(MethodExitInfo);
                auto iter1 = thread_map.find(tid);
                if (iter1 == thread_map.end() || iter1->second != me->tid)
                    return _no_thing;
                auto iter = md_map.find(me->idx);
                if (iter == md_map.end())
                    return _no_thing;
                data = iter->second;
                return _method_exit;
            }
            case _compiled_method_load: {
                current += (info->size - sizeof(DumpInfo));
                auto iter = section_map.find(current);
                if (iter == section_map.end())
                    return _no_thing;
                data = iter->second;
                return _compiled_method_load;
            }
            case _compiled_method_unload: {
                data = current;
                current += sizeof(CompiledMethodUnloadInfo);
                return _compiled_method_unload;
            }
            case _thread_start: {
                data = current;
                const ThreadStartInfo *th;
                th = (const ThreadStartInfo*)current;
                current += sizeof(ThreadStartInfo);
                return _thread_start;
            }
            case _inline_cache_add: {
                data = current;
                current += sizeof(InlineCacheAdd);
                return _inline_cache_add;
            }
            case _inline_cache_clear: {
                data = current;
                current += sizeof(InlineCacheClear);
                return _inline_cache_clear;
            }
            default: {
                current = end;
                return _illegal;
            }
        }
    }
    return _illegal;
}

long JvmDumpDecoder::get_java_tid(long tid) {
    auto iter = thread_map.find(tid);
    if (iter == thread_map.end())
        return -1;
    return iter->second;
}

void JvmDumpDecoder::initialize(char *dump_data, Analyser* analyser) {
    uint8_t *buffer;
    size_t size;
    uint64_t foffset, fsize;
    int errcode;

    errcode = preprocess_filename(dump_data, &foffset, &fsize);
    if (errcode < 0) {
        fprintf(stderr, "JvmDumpDecoder: bad file: %s.\n", dump_data);
        return;
    }
    errcode = load_file(&buffer, &size, dump_data,
                                foffset, fsize, "main");
    if (errcode < 0) {
        fprintf(stderr, "JvmDumpDecoder: bad file: %s.\n", dump_data);
        return;
    }

    begin = buffer;
    end = buffer + size;
    const DumpInfo *info;
    while (buffer < end) {
        info = (const struct DumpInfo *)buffer;
        if (buffer + info->size > end)
           return;
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
                string klassName = string(klass_name, me->klass_name_length);
                string methodName = string(name, me->method_name_length)+string(sig, me->method_signature_length);
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
            case _method_exit: {
                const MethodExitInfo* me;
                me = (const MethodExitInfo*)buffer;
                buffer += sizeof(MethodExitInfo);
                break;
            }
            case _compiled_method_load: {
                const CompiledMethodLoadInfo *cm;
                cm = (const CompiledMethodLoadInfo*)buffer;
                buffer += sizeof(CompiledMethodLoadInfo);
                const Method* mainm = nullptr;
                map<int, const Method*> methods;
                for (int i = 0; i < cm->inline_method_cnt; i++) {
                    const InlineMethodInfo*imi;
                    imi = (const InlineMethodInfo*)buffer;
                    buffer += sizeof(InlineMethodInfo);
                    const char *klass_name = (const char *)buffer;
                    buffer += imi->klass_name_length;
                    const char *name = (const char *)buffer;
                    buffer += imi->name_length;
                    const char *sig = (const char *)buffer;
                    buffer += imi->signature_length;
                    string klassName = string(klass_name, imi->klass_name_length);
                    string methodName = string(name, imi->name_length)+string(sig, imi->signature_length);
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
                    fprintf(stderr, "JvmDumpDecoder: unknown or un-jportal section.\n");
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
                fprintf(stderr, "JvmDumpDecoder: unknown dump type.\n");
                return;
            }
        }
    }
    return;
}

void JvmDumpDecoder::destroy() {
    for (auto section : section_map)
        delete section.second;
    section_map.clear();

    for (auto md : md_map)
        delete md.second;
    md_map.clear();

    free(begin);
    begin = nullptr;
    end = nullptr;
}

#include "runtime/jvm_runtime.hpp"
#include "runtime/jit_image.hpp"
#include "runtime/jit_section.hpp"
#include "runtime/codelets_entry.hpp"
#include "java/analyser.hpp"
#include "utilities/load_file.hpp"

uint8_t *JVMRuntime::begin = nullptr;
uint8_t *JVMRuntime::end = nullptr;
map<long, long> JVMRuntime::thread_map;
map<int, const Method*> JVMRuntime::md_map;
map<const uint8_t *, JitSection *> JVMRuntime::section_map;

JVMRuntime::JVMRuntime() {
    image = new JitImage("jitted-code");
    current = begin;
}

void JVMRuntime::event(uint64_t time, long tid) {
    const DumpInfo *info;
    while (current < end) {
        info = (const struct DumpInfo *)current;
        if (current + info->size > end || info->time > time)
           return;
        current += sizeof(DumpInfo);
        switch(info->type) {
            case _codelet_info: {
                current += sizeof(CodeletsInfo);
                break;
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
                break;
            }
            case _method_entry: {
                const MethodEntryInfo* me;
                me = (const MethodEntryInfo*)current;
                current += sizeof(MethodEntryInfo);
                break;
            }
            case _method_exit: {
                const MethodExitInfo* me;
                me = (const MethodExitInfo*)current;
                current += sizeof(MethodExitInfo);
                break;
            }
            case _compiled_method_load: {
                current += (info->size - sizeof(DumpInfo));
                if (section_map.count(current))
                    image->add(section_map[current]);
                break;
            }
            case _compiled_method_unload: {
                const CompiledMethodUnloadInfo *cmu = (const CompiledMethodUnloadInfo*)current;
                current += sizeof(CompiledMethodUnloadInfo);
                image->remove(cmu->code_begin);
                break;
            }
            case _thread_start: {
                current += sizeof(ThreadStartInfo);
                break;
            }
            case _inline_cache_add: {
                const InlineCacheAdd* ica = (const InlineCacheAdd*)current;
                JitSection* section = image->find(ica->src);
                if (section)
                    ics[{ica->src, section}] = ica->dest;
                current += sizeof(InlineCacheAdd);
                break;
            }
            case _inline_cache_clear: {
                const InlineCacheClear* icc = (const InlineCacheClear*)current;
                JitSection* section = image->find(icc->src);
                if (section)
                    ics.erase({icc->src, section});
                current += sizeof(InlineCacheClear);
                break;
            }
            default: {                
                /* error */

                current = end;
                return;
            }
        }
    }
}

long JVMRuntime::get_java_tid(long tid) {
    auto iter = thread_map.find(tid);
    if (iter == thread_map.end())
        return -1;
    return iter->second;
}

void JVMRuntime::initialize(char *dump_data, Analyser* analyser) {
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

void JVMRuntime::destroy() {
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

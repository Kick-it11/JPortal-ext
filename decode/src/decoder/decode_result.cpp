#include "decoder/decode_result.hpp"

#include <stdlib.h>
#include <string.h>

int TraceData::expand_data(size_t size) {
    if (!data_volume) {
        data_begin = (u1 *)malloc(initial_data_volume);
        if (!data_begin)
            return -1;
        data_end = data_begin;
        data_volume = initial_data_volume;
    }
    while (data_volume - (data_end - data_begin) < size) {
        u1 *new_data = (u1 *)malloc(data_volume + initial_data_volume);
        if (!new_data)
            return -1;
        memcpy(new_data, data_begin, data_volume);
        free(data_begin);
        data_end = new_data + (data_end - data_begin);
        data_begin = new_data;
        data_volume += initial_data_volume;
    }
    return 0;
}

int TraceData::write(void *data, size_t size) {
    if (data_volume - (data_end - data_begin) < size) {
        if (expand_data(size) < 0)
            return -1;
    }
    memcpy(data_end, data, size);
    data_end += size;
    return 0;
}

const Method* TraceData::get_method_info(size_t loc) {
    auto iter = method_info.find(loc);
    if (iter != method_info.end())
        return iter->second;
    return nullptr;
}

bool TraceData::get_inter(size_t loc, const u1 *&codes, size_t &size) {
    const u1 *pointer = data_begin + loc;
    if (pointer > data_end)
        return false;
    CodeletsEntry::Codelet codelet = (CodeletsEntry::Codelet)(*pointer);
    if (codelet != CodeletsEntry::_bytecode)
        return false;
    pointer++;
    const InterRecord *inter = (const InterRecord *)pointer;
    if (pointer + sizeof(InterRecord) > data_end ||
            pointer + sizeof(InterRecord) < pointer)
        return false;
    pointer += sizeof(InterRecord);
    if (pointer + inter->size > data_end || pointer + inter->size < pointer)
        return false;
    codes = pointer;
    size = inter->size;
    return true;
}

bool TraceData::get_jit(size_t loc, const PCStackInfo **&codes, size_t &size, const JitSection *&section) {
    const u1 *pointer = data_begin + loc;
    if (pointer > data_end)
        return false;
    CodeletsEntry::Codelet codelet = CodeletsEntry::Codelet(*pointer);
    if (codelet != CodeletsEntry::_jitcode_entry &&
        codelet != CodeletsEntry::_jitcode_osr_entry &&
        codelet != CodeletsEntry::_jitcode)
        return false;
    pointer++;
    const JitRecord *jit = (const JitRecord *)pointer;
    if (pointer + sizeof(JitRecord) > data_end ||
            pointer + sizeof(JitRecord) < pointer)
        return false;
    pointer += sizeof(JitRecord);
    if (pointer + jit->size * sizeof(PCStackInfo *) > data_end ||
            pointer + jit->size * sizeof(PCStackInfo *) < pointer)
        return false;
    codes = (const PCStackInfo **)pointer;
    size = jit->size;
    section = jit->section;
    return true;
}

int TraceDataRecord::add_bytecode(u8 time, Bytecodes::Code bytecode) {
    current_time = time;
    if (codelet_type != CodeletsEntry::_bytecode) {
        size_t begin = trace.data_end - trace.data_begin;
        codelet_type = CodeletsEntry::_bytecode;
        if (trace.write(&codelet_type, 1) < 0) {
            fprintf(stderr, "trace data record: fail to write.\n");
            return -1;
        }
        InterRecord inter;
        if (trace.write(&inter, sizeof(inter)) < 0) {
            fprintf(stderr, "trace data record: fail to write.\n");
            return -1;
        }
        loc = trace.data_end - trace.data_begin - sizeof(u8);
    }
    if (trace.write(&bytecode, 1) < 0) {
        fprintf(stderr, "trace data record: fail to write.\n");
        return -1;
    }
    (*((u8 *)(trace.data_begin + loc)))++;
    return 0;
}

int TraceDataRecord::add_jitcode(u8 time, const JitSection *section,
                                 PCStackInfo *pc, u8 entry) {
    current_time = time;
    if (codelet_type != CodeletsEntry::_jitcode
        && codelet_type != CodeletsEntry::_jitcode_entry
        && codelet_type != CodeletsEntry::_jitcode_osr_entry
        || last_section != section || entry == section->cmd()->entry_point()
        || entry == section->cmd()->verified_entry_point()) {
        if (entry == section->cmd()->osr_entry_point()
            && codelet_type == CodeletsEntry::_bytecode && (Bytecodes::is_branch(last_bytecode)
            || last_bytecode == Bytecodes::_goto || last_bytecode == Bytecodes::_goto_w))
            codelet_type = CodeletsEntry::_jitcode_osr_entry;
        else if (entry == section->cmd()->entry_point()
                 || entry == section->cmd()->verified_entry_point())
            codelet_type = CodeletsEntry::_jitcode_entry;
        else
            codelet_type = CodeletsEntry::_jitcode;
        if (trace.write(&codelet_type, 1) < 0) {
            fprintf(stderr, "trace data record: fail to write.\n");
            return -1;
        }
        JitRecord jit(section);
        if (trace.write(&jit, sizeof(jit)) < 0) {
            fprintf(stderr, "trace data record: fail to write.\n");
            return -1;
        }
        loc = trace.data_end - trace.data_begin - 2 * sizeof(u8);
        last_section = section;
    }
    if (trace.write(&pc, sizeof(pc)) < 0) {
        fprintf(stderr, "trace data record: fail to write.\n");
        return -1;
    }
    (*((u8 *)(trace.data_begin + loc)))++;
    return 0;
}

int TraceDataRecord::add_codelet(CodeletsEntry::Codelet codelet) {
    switch (codelet) {
        case CodeletsEntry::_method_entry:
        case CodeletsEntry::_throw_ArrayIndexOutOfBoundsException:
        case CodeletsEntry::_throw_ArrayStoreException:
        case CodeletsEntry::_throw_ArithmeticException:
        case CodeletsEntry::_throw_ClassCastException:
        case CodeletsEntry::_throw_NullPointerException:
        case CodeletsEntry::_throw_StackOverflowError:
        case CodeletsEntry::_rethrow_exception:
        case CodeletsEntry::_deopt:
        case CodeletsEntry::_deopt_reexecute_return:
        case CodeletsEntry::_throw_exception:
        case CodeletsEntry::_remove_activation:
        case CodeletsEntry::_remove_activation_preserving_args: {
            codelet_type = codelet;
            if (trace.write(&codelet_type, 1) < 0) {
                fprintf(stderr, "trace data record: fail to write.\n");
                return -1;
            }
            return 0;
        }
        case CodeletsEntry::_invoke_return:
        case CodeletsEntry::_invokedynamic_return:
        case CodeletsEntry::_invokeinterface_return: {
            if (codelet_type == CodeletsEntry::_method_entry) {
                trace.data_end--;
                codelet_type = CodeletsEntry::_illegal;
                return 0;
            }
            codelet_type = codelet;
            if (trace.write(&codelet_type, 1) < 0) {
                fprintf(stderr, "trace data record: fail to write.\n");
                return -1;
            }
            return 0;
        }
        case CodeletsEntry::_result_handlers_for_native_calls: {
            if (codelet_type == CodeletsEntry::_method_entry)
                trace.data_end--;
            codelet_type = CodeletsEntry::_illegal;
            return 0;
        }
        default: {
            fprintf(stdout, "trace data record: unknown codelets type(%d)\n", codelet);
            codelet_type = CodeletsEntry::_illegal;
            return 0;
        }
    }
}

void TraceDataRecord::add_method_info(const Method* method) {
    if (codelet_type == CodeletsEntry::_method_entry)
        trace.method_info[trace.data_end - trace.data_begin] = method;
    return;
}

void TraceDataRecord::switch_out(bool loss) {
    codelet_type = CodeletsEntry::_illegal;
    if (thread) {
        thread->end_addr = trace.data_end - trace.data_begin;
        thread->end_time = current_time;
        thread->tail_loss = loss;
    }
    thread = nullptr;
    return;
}

void TraceDataRecord::switch_in(long tid, u8 time, bool loss) {
    if (thread && thread->tid == tid && !loss)
        return;

    current_time = time;
    auto split = trace.thread_map.find(tid);
    if (split == trace.thread_map.end()) {
        trace.thread_map[tid].push_back(
            ThreadSplit(tid, trace.data_end - trace.data_begin, time));
        thread = &trace.thread_map[tid].back();
        thread->head_loss = loss;
        codelet_type = CodeletsEntry::_illegal;
        return;
    }
    auto iter = split->second.begin();
    for (; iter != split->second.end(); iter++) {
        if (time < iter->start_time)
            break;
    }
    iter = split->second.insert(
            iter, ThreadSplit(tid, trace.data_end - trace.data_begin, time));
    thread = &*iter;
    thread->head_loss = loss;
    codelet_type = CodeletsEntry::_illegal;
    return;
}

bool TraceDataAccess::next_trace(CodeletsEntry::Codelet &codelet, size_t &loc) {
    loc = current - trace.data_begin;
    if (current >= terminal) {
        return false;
    }
    codelet = CodeletsEntry::Codelet(*current);
    if (codelet < CodeletsEntry::_unimplemented_bytecode ||
        codelet > CodeletsEntry::_jitcode) {
        fprintf(stderr, "trace data access: format error %ld.\n", loc);
        current = terminal;
        loc = current - trace.data_begin;
        return false;
    }
    current++;
    if (codelet == CodeletsEntry::_bytecode) {
        const InterRecord *inter = (const InterRecord *)current;
        if (current + sizeof(InterRecord) + inter->size > trace.data_end ||
            current + sizeof(InterRecord) + inter->size < current) {
            fprintf(stderr, "trace data access: format error %ld.\n", loc);
            current = terminal;
            loc = current - trace.data_begin;
            return false;
        }
        current = current + sizeof(InterRecord) + inter->size;
    } else if (codelet == CodeletsEntry::_jitcode_entry ||
               codelet == CodeletsEntry::_jitcode_osr_entry ||
               codelet == CodeletsEntry::_jitcode) {
        const JitRecord *jit = (const JitRecord *)current;
        if (current + sizeof(JitRecord) + jit->size * sizeof(PCStackInfo *) > trace.data_end
            || current + sizeof(JitRecord) + jit->size * sizeof(PCStackInfo *) < current) {
            fprintf(stderr, "trace data access: format error %ld.\n", loc);
            current = terminal;
            loc = current - trace.data_begin;
            return false;
        }
        current = current + sizeof(JitRecord) + jit->size * sizeof(PCStackInfo *);
    }
    return true;
}

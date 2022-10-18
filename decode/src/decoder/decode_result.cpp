#include "decoder/decode_result.hpp"

#include <cstring>
#include <iostream>

void TraceData::expand_data(uint64_t size) {
    if (!data_volume) {
        data_begin = new uint8_t[(initial_data_volume)];
        data_end = data_begin;
        data_volume = initial_data_volume;
        if (data_volume > size)
            return;
    }
    uint64_t new_volume = data_volume;
    while (new_volume - (data_end - data_begin) < size)
        new_volume *= 2;
    uint8_t *new_data = new uint8_t[new_volume];
    memcpy(new_data, data_begin, data_end - data_begin);
    delete[] data_begin;
    data_end = new_data + (data_end - data_begin);
    data_begin = new_data;
    data_volume += new_volume;
}

void TraceData::write(void *data, uint64_t size) {
    if (data_volume - (data_end - data_begin) < size)
        expand_data(size);
    memcpy(data_end, data, size);
    data_end += size;
}

const Method* TraceData::get_method_info(uint64_t loc) {
    auto iter = method_info.find(loc);
    if (iter != method_info.end())
        return iter->second;
    return nullptr;
}

bool TraceData::get_inter(uint64_t loc, const uint8_t *&codes, uint64_t &size) {
    const uint8_t *pointer = data_begin + loc;
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

bool TraceData::get_jit(uint64_t loc, const PCStackInfo **&codes, uint64_t &size, const JitSection *&section) {
    const uint8_t *pointer = data_begin + loc;
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

void TraceDataRecord::add_bytecode(uint64_t time, Bytecodes::Code bytecode) {
    current_time = time;
    if (codelet_type != CodeletsEntry::_bytecode) {
        uint64_t begin = trace.data_end - trace.data_begin;
        codelet_type = CodeletsEntry::_bytecode;
        trace.write(&codelet_type, 1);
        InterRecord inter;
        trace.write(&inter, sizeof(inter));
        loc = trace.data_end - trace.data_begin - sizeof(uint64_t);
    }
    trace.write(&bytecode, 1);
    (*((uint64_t *)(trace.data_begin + loc)))++;
}

void TraceDataRecord::add_jitcode(uint64_t time, const JitSection *section,
                                 PCStackInfo *pc, uint64_t entry) {
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
        trace.write(&codelet_type, 1);
        JitRecord jit(section);
        trace.write(&jit, sizeof(jit));
        loc = trace.data_end - trace.data_begin - 2 * sizeof(uint64_t);
        last_section = section;
    }
    trace.write(&pc, sizeof(pc));
    (*((uint64_t *)(trace.data_begin + loc)))++;
    return;
}

void TraceDataRecord::add_codelet(CodeletsEntry::Codelet codelet) {
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
            trace.write(&codelet_type, 1);
            return;
        }
        case CodeletsEntry::_invoke_return:
        case CodeletsEntry::_invokedynamic_return:
        case CodeletsEntry::_invokeinterface_return: {
            if (codelet_type == CodeletsEntry::_method_entry) {
                trace.data_end--;
                codelet_type = CodeletsEntry::_illegal;
                return;
            }
            codelet_type = codelet;
            trace.write(&codelet_type, 1);
            return;
        }
        case CodeletsEntry::_result_handlers_for_native_calls: {
            if (codelet_type == CodeletsEntry::_method_entry)
                trace.data_end--;
            codelet_type = CodeletsEntry::_illegal;
            return;
        }
        default: {
            std::cerr << "TraceDataRecord: unknown codelets type" << codelet << std::endl;
            codelet_type = CodeletsEntry::_illegal;
            return;
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
        if (loss) thread->tail_loss = 1;
    }
    thread = nullptr;
    return;
}

void TraceDataRecord::switch_in(uint32_t tid, uint64_t time, bool loss) {
    if (thread && thread->tid == tid && !loss)
        return;

    current_time = time;
    auto split = trace.thread_map.find(tid);
    if (split == trace.thread_map.end()) {
        trace.thread_map[tid].push_back(ThreadSplit(tid, trace.data_end - trace.data_begin, time));
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
    if (loss) thread->head_loss = 1;
    codelet_type = CodeletsEntry::_illegal;
    return;
}

bool TraceDataAccess::next_trace(CodeletsEntry::Codelet &codelet, uint64_t &loc) {
    loc = current - trace.data_begin;
    if (current >= terminal) {
        return false;
    }
    codelet = CodeletsEntry::Codelet(*current);
    if (codelet < CodeletsEntry::_unimplemented_bytecode ||
        codelet > CodeletsEntry::_jitcode) {
        std::cerr << "TraceDataAccess: format error " << loc << std::endl;
        current = terminal;
        loc = current - trace.data_begin;
        return false;
    }
    current++;
    if (codelet == CodeletsEntry::_bytecode) {
        const InterRecord *inter = (const InterRecord *)current;
        if (current + sizeof(InterRecord) + inter->size > trace.data_end ||
            current + sizeof(InterRecord) + inter->size < current) {
            std::cerr << "TraceDataAccess: format error" << loc << std::endl;
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
            std::cerr << "TraceDataAccess: format error" << loc << std::endl;
            current = terminal;
            loc = current - trace.data_begin;
            return false;
        }
        current = current + sizeof(JitRecord) + jit->size * sizeof(PCStackInfo *);
    }
    return true;
}

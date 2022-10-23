#include "decoder/decode_result.hpp"

#include <cstring>
#include <iostream>

void TraceData::expand_data(uint64_t size)
{
    if (!data_volume)
    {
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
    data_volume = new_volume;
}

void TraceData::write(void *data, uint64_t size)
{
    if (data_volume - (data_end - data_begin) < size)
        expand_data(size);
    memcpy(data_end, data, size);
    data_end += size;
}

const Method *TraceData::get_method_info(uint64_t loc)
{
    auto iter = method_info.find(loc);
    if (iter != method_info.end())
        return iter->second;
    return nullptr;
}

bool TraceData::get_inter(uint64_t loc, const uint8_t *&codes, uint64_t &size)
{
    const uint8_t *pointer = data_begin + loc;
    if (pointer > data_end)
        return false;
    JVMRuntime::Codelet codelet = (JVMRuntime::Codelet)(*pointer);
    if (codelet != JVMRuntime::_bytecode)
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

bool TraceData::get_jit(uint64_t loc, const PCStackInfo **&codes, uint64_t &size, const JitSection *&section)
{
    const uint8_t *pointer = data_begin + loc;
    if (pointer > data_end)
        return false;
    JVMRuntime::Codelet codelet = JVMRuntime::Codelet(*pointer);
    if (codelet != JVMRuntime::_jitcode_entry &&
        codelet != JVMRuntime::_jitcode_osr_entry &&
        codelet != JVMRuntime::_jitcode)
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

void TraceDataRecord::add_bytecode(uint64_t time, Bytecodes::Code bytecode)
{
    if (!thread)
        switch_in(0, time, true);
    current_time = time;
    if (codelet_type != JVMRuntime::_bytecode)
    {
        uint64_t begin = trace.data_end - trace.data_begin;
        codelet_type = JVMRuntime::_bytecode;
        trace.write(&codelet_type, 1);
        InterRecord inter;
        trace.write(&inter, sizeof(inter));
        loc = trace.data_end - trace.data_begin - sizeof(uint64_t);
    }
    trace.write(&bytecode, 1);
    (*((uint64_t *)(trace.data_begin + loc)))++;
}

void TraceDataRecord::add_jitcode(uint64_t time, const JitSection *section,
                                  PCStackInfo *pc, uint64_t entry)
{
    if (!thread)
        switch_in(0, time, true);
    current_time = time;
    if (codelet_type != JVMRuntime::_jitcode && codelet_type != JVMRuntime::_jitcode_entry && codelet_type != JVMRuntime::_jitcode_osr_entry || last_section != section || entry == section->entry_point() || entry == section->verified_entry_point())
    {
        if (entry == section->osr_entry_point() && codelet_type == JVMRuntime::_bytecode && (Bytecodes::is_branch(last_bytecode) || last_bytecode == Bytecodes::_goto || last_bytecode == Bytecodes::_goto_w))
            codelet_type = JVMRuntime::_jitcode_osr_entry;
        else if (entry == section->entry_point() || entry == section->verified_entry_point())
            codelet_type = JVMRuntime::_jitcode_entry;
        else
            codelet_type = JVMRuntime::_jitcode;
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

void TraceDataRecord::add_codelet(uint64_t time, JVMRuntime::Codelet codelet)
{
    if (!thread)
        switch_in(0, time, true);
    switch (codelet)
    {
    case JVMRuntime::_method_entry_point:
    case JVMRuntime::_throw_ArrayIndexOutOfBoundsException:
    case JVMRuntime::_throw_ArrayStoreException:
    case JVMRuntime::_throw_ArithmeticException:
    case JVMRuntime::_throw_ClassCastException:
    case JVMRuntime::_throw_NullPointerException:
    case JVMRuntime::_throw_StackOverflowError:
    case JVMRuntime::_rethrow_exception:
    case JVMRuntime::_deopt:
    case JVMRuntime::_deopt_reexecute_return:
    case JVMRuntime::_throw_exception:
    case JVMRuntime::_remove_activation:
    case JVMRuntime::_remove_activation_preserving_args:
    {
        codelet_type = codelet;
        trace.write(&codelet_type, 1);
        return;
    }
    case JVMRuntime::_invoke_return:
    case JVMRuntime::_invokedynamic_return:
    case JVMRuntime::_invokeinterface_return:
    {
        if (codelet_type == JVMRuntime::_method_entry_point)
        {
            trace.data_end--;
            codelet_type = JVMRuntime::_illegal;
            return;
        }
        codelet_type = codelet;
        trace.write(&codelet_type, 1);
        return;
    }
    case JVMRuntime::_result_handlers_for_native_calls:
    {
        if (codelet_type == JVMRuntime::_method_entry_point)
            trace.data_end--;
        codelet_type = JVMRuntime::_illegal;
        return;
    }
    default:
    {
        std::cerr << "TraceDataRecord error: Add unknown codelets type " << codelet << std::endl;
        codelet_type = JVMRuntime::_illegal;
        return;
    }
    }
}

void TraceDataRecord::add_method_info(const Method *method)
{
    if (codelet_type == JVMRuntime::_method_entry_point)
        trace.method_info[trace.data_end - trace.data_begin] = method;
    return;
}

void TraceDataRecord::switch_out(bool loss)
{
    codelet_type = JVMRuntime::_illegal;
    if (thread)
    {
        thread->end_addr = trace.data_end - trace.data_begin;
        thread->end_time = current_time;
        if (loss)
            thread->tail_loss = 1;

        /* Thread that records nothing */
        if (thread->end_addr == thread->start_addr && !thread->head_loss && !thread->tail_loss) {
            trace.thread_map[thread->tid].pop_back();
        }
    }
    thread = nullptr;
    return;
}

void TraceDataRecord::switch_in(uint32_t tid, uint64_t time, bool loss)
{
    if (thread && thread->tid == tid && !loss)
        return;

    /* Make sure that every TraceData is for a specific cpu data
     * It will always follow the time
     */
    current_time = time;
    auto split = trace.thread_map.find(tid);
    if (split == trace.thread_map.end())
    {
        trace.thread_map[tid].push_back(ThreadSplit(tid, trace.data_end - trace.data_begin, time));
        thread = &trace.thread_map[tid].back();
        thread->head_loss = loss;
        codelet_type = JVMRuntime::_illegal;
        return;
    }
    auto iter = split->second.begin();
    for (; iter != split->second.end(); iter++)
    {
        if (time < iter->start_time)
            break;
    }
    iter = split->second.insert(
        iter, ThreadSplit(tid, trace.data_end - trace.data_begin, time));
    thread = &*iter;
    if (loss)
        thread->head_loss = 1;
    codelet_type = JVMRuntime::_illegal;
    return;
}

bool TraceDataAccess::next_trace(JVMRuntime::Codelet &codelet, uint64_t &loc)
{
    loc = current - trace.data_begin;
    if (current >= terminal)
    {
        return false;
    }
    codelet = JVMRuntime::Codelet(*current);
    if (codelet < JVMRuntime::_unimplemented_bytecode ||
        codelet > JVMRuntime::_jitcode)
    {
        std::cerr << "TraceDataAccess error: Get unknown codelet " << loc << std::endl;
        current = terminal;
        loc = current - trace.data_begin;
        return false;
    }
    current++;
    if (codelet == JVMRuntime::_bytecode)
    {
        const InterRecord *inter = (const InterRecord *)current;
        if (current + sizeof(InterRecord) + inter->size > trace.data_end ||
            current + sizeof(InterRecord) + inter->size < current)
        {
            std::cerr << "TraceDataAccess: Get bytecodes " << loc << std::endl;
            current = terminal;
            loc = current - trace.data_begin;
            return false;
        }
        current = current + sizeof(InterRecord) + inter->size;
    }
    else if (codelet == JVMRuntime::_jitcode_entry ||
             codelet == JVMRuntime::_jitcode_osr_entry ||
             codelet == JVMRuntime::_jitcode)
    {
        const JitRecord *jit = (const JitRecord *)current;
        if (current + sizeof(JitRecord) + jit->size * sizeof(PCStackInfo *) > trace.data_end || current + sizeof(JitRecord) + jit->size * sizeof(PCStackInfo *) < current)
        {
            std::cerr << "TraceDataAccess: Get jitcodes " << loc << std::endl;
            current = terminal;
            loc = current - trace.data_begin;
            return false;
        }
        current = current + sizeof(JitRecord) + jit->size * sizeof(PCStackInfo *);
    }
    return true;
}

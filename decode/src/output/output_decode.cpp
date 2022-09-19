#include "structure/PT/decode_result.hpp"
#include "structure/java/method.hpp"
#include "structure/java/klass.hpp"
#include "structure/java/analyser.hpp"
#include "output/output_decode.hpp"

#include <queue>
#include <stack>
#include <iostream>
#include <algorithm>
#include <cassert>

class ExecMap {
public:
    const jit_section* section;
    list<pair<const Method*, Block*>> prev_frame;
    ExecMap(): section(nullptr) { }
    ExecMap(const jit_section* s): section(s) { }
};

static bool output_bytecode(FILE* fp, const u1* codes, size_t size) {
    // output bytecode
    for (int i = 0; i < size; i++) {
        // fwrite(codes+i, 1, 1, fp);
        fprintf(fp, "%hhu\n", *(codes+i));
        // fprintf(fp, "%s\n", Bytecodes::name_for(Bytecodes::cast(*(codes+i))));
    }
    return true;
}

static void output_jitcode(FILE* fp, const Method* method, Block* blc) {
    const u1* codes = method->get_bg()->bctcode();
    for (int i = blc->get_bct_codebegin(); i < blc->get_bct_codeend(); ++i) {
        // fwrite(codes+i, 1, 1, fp);
        fprintf(fp, "%hhu\n", *(codes+i));
        // fprintf(fp, "%s\n", Bytecodes::name_for(Bytecodes::cast(*(codes+i))));
    }
}

static void output_jitcode(FILE* fp, list<pair<const Method*, Block*>>& blocks) {
    for (auto block : blocks)
        output_jitcode(fp, block.first, block.second);
}

// // todo: find a shortest path to pass all blocks in block_execs
static bool map_jitcode(ExecMap* jm, set<pair<const Method*, Block*>> &block_execs) {
}

static bool handle_jitcode(ExecMap* exec, const PCStackInfo **pcs, int size, FILE* fp) {
    const jit_section* section = exec->section;
    set<const PCStackInfo*> pc_execs;
    set<pair<const Method*, Block*>> block_execs;
    bool notRetry = true;
    for (int i = 0; i < size; ++i) {
        const PCStackInfo* pc = pcs[i];
        if (pc_execs.count(pc)) {
            map_jitcode(exec, block_execs);
            pc_execs.clear();
            block_execs.clear();
        }
        pc_execs.insert(pc);
        for (int j = pc->numstackframes-1; j >= 0; --j) {
            int mi = pc->methods[j];
            int bci = pc->bcis[j];
            const Method* method = section->cmd->get_method(mi);
            if (!method || !method->is_jportal()) continue;
            Block* block = method->get_bg()->block(bci);
            if (!block) continue;
            if (exec->prev_frame.empty()) {
                notRetry = false;
                output_jitcode(fp, method, block);
                exec->prev_frame.push_back({method, block});
                pc_execs.clear();
                block_execs.clear();
            } else {
                block_execs.insert({method, block});
            }
        }
    }
    map_jitcode(exec, block_execs);
    return notRetry;
}

static void output_return(FILE* fp, stack<ExecMap*> &exec_st, const jit_section* section) {
    while (!exec_st.empty() && exec_st.top()->section != section) {

        // return
        // todo: map
    }
    if (exec_st.empty()) {
        exec_st.push(new ExecMap(section));
    }
}

static void output_trace(TraceData* trace, size_t start, size_t end, FILE* fp) {
    TraceDataAccess access(*trace, start, end);
    CodeletsEntry::Codelet codelet, prev_codelet = CodeletsEntry::_illegal;
    size_t loc;
    stack<ExecMap*> exec_st;
    while (access.next_trace(codelet, loc)) {
        switch(codelet) {
            default: {
                fprintf(stderr, "output_trace: unknown codelet(%d)\n", codelet);
                exec_st = stack<ExecMap*>();
                break;
            }
            case CodeletsEntry::_method_entry_points: {
                if (exec_st.empty() || exec_st.top()->section)
                    exec_st.push(new ExecMap(nullptr));
                break;
            }
            case CodeletsEntry::_throw_ArrayIndexOutOfBoundsException_entry_points:
            case CodeletsEntry::_throw_ArrayStoreException_entry_points:
            case CodeletsEntry::_throw_ArithmeticException_entry_points:
            case CodeletsEntry::_throw_ClassCastException_entry_points:
            case CodeletsEntry::_throw_NullPointerException_entry_points:
            case CodeletsEntry::_throw_StackOverflowError_entry_points: {
                break;
            }
            case CodeletsEntry::_rethrow_exception_entry_entry_points: {
                break;
            }
            case CodeletsEntry::_deopt_entry_points:
            case CodeletsEntry::_deopt_reexecute_return_entry_points: {
                // deopt
                if (!exec_st.empty() && exec_st.top()->section) {
                    delete exec_st.top();
                    exec_st.pop();
                }
                if (exec_st.empty() || exec_st.top()->section)
                    exec_st.push(new ExecMap(nullptr));
                break;
            }
            case CodeletsEntry::_throw_exception_entry_points: {
                // exception handling or throw
                break;
            }
            case CodeletsEntry::_remove_activation_entry_points:
            case CodeletsEntry::_remove_activation_preserving_args_entry_points: {
                // after throw exception or deoptimize
                break;
            }
            case CodeletsEntry::_invoke_return_entry_points:
            case CodeletsEntry::_invokedynamic_return_entry_points: 
            case CodeletsEntry::_invokeinterface_return_entry_points: {
                break;
            }
            case CodeletsEntry::_bytecode: {
                const u1* codes;
                size_t size;
                assert(trace->get_inter(loc, codes, size) && codes);
                output_return(fp, exec_st, nullptr);
                output_bytecode(fp, codes, size);
            }
            case CodeletsEntry::_jitcode_entry:
            case CodeletsEntry::_jitcode_osr_entry:
            case CodeletsEntry::_jitcode: {
                const jit_section *section = nullptr;
                const PCStackInfo **pcs = nullptr;
                size_t size;
                assert(trace->get_jit(loc, pcs, size, section)
                       && pcs && section && section->cmd);
                if (codelet == CodeletsEntry::_jitcode_entry) {
                    ExecMap* exec = new ExecMap(section);
                    const Method* method = section->cmd->mainm;
                    Block* block = method->get_bg()->block(0);
                    output_jitcode(fp, method, block);
                    exec_st.push(exec);
                } else if (codelet == CodeletsEntry::_jitcode_osr_entry) {
                    ExecMap* exec = new ExecMap(section);
                    exec_st.push(exec);
                } else {
                    output_return(fp, exec_st, section);
                }
                handle_jitcode(exec_st.top(), pcs, size, fp);
            }
        }
    }
}

// per thread output
void output_decode(list<TraceData*> &traces) {
    map<long, vector<pair<ThreadSplit, TraceData*>>> threads_data;
    for (auto && trace : traces)
        for (auto && threads: trace->get_thread_map())
            for (auto && thread : threads.second)
                if (thread.end_addr > thread.start_addr)
                    threads_data[threads.first].push_back({thread, trace});
    
    for (auto iter = threads_data.begin(); iter != threads_data.end(); ++iter)
        sort(iter->second.begin(), iter->second.end(),
             [] (pair<ThreadSplit, TraceData*>& x, pair<ThreadSplit, TraceData*>& y) -> bool {
                return x.first.start_time < y.first.start_time
                       || x.first.start_time == y.first.start_time
                          &&  x.first.end_time < y.first.end_time;});

    for (auto iter1 = threads_data.begin(); iter1 != threads_data.end(); ++iter1) {
        char name[32];
        sprintf(name, "thrd%ld", iter1->first);
        // FILE *fp = fopen(name, "wb");
        FILE *fp = fopen(name, "w");
        if (!fp) {
            fprintf(stderr, "Decode output: open decode file(%s) error\n", name);
            continue;
        }
        for (auto iter2 = iter1->second.begin(); iter2 != iter1->second.end(); ++iter2)
            output_trace(iter2->second, iter2->first.start_addr, iter2->first.end_addr, fp);
        fclose(fp);
    }
}

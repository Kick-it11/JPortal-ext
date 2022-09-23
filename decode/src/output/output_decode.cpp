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

class ExecInfo {
public:
    const jit_section* section; // jit_section, nullptr indicates a segment of inter codes
    vector<pair<const Method*, Block*>> prev_frame;
    ExecInfo(): section(nullptr) { }
    ExecInfo(const jit_section* s): section(s) { }
};

class JitMatchTree {
private:
    const Method* method;
    map<Block*, int> seqs;
    JitMatchTree* father;
    map<Block*, JitMatchTree*> children;

    bool match_next(Block* cur, vector<pair<const Method*, Block*>>& ans) {
        vector<pair<int, Block*>> vv;
        unordered_set<Block*> ss;
        queue<pair<int, Block*>> q;
        vector<pair<int, int>> find_next;
        int find_end = -1;
        while (!q.empty()) {
            Block* blc = q.front().second;
            int idx = vv.size();
            vv.push_back({q.front().first, blc});
            q.pop();
            if (blc->get_succs_size() == 0) {
                if (find_end == -1) find_end = idx;
                continue;
            }
            for (auto iter = blc->get_succs_begin(); iter != blc->get_succs_end(); ++iter) {
                if (ss.count(*iter)) continue;
                ss.insert(*iter);
                if (seqs.count(*iter)) {
                    find_next.push_back({seqs[*iter], vv.size()});
                    vv.push_back({idx, *iter});
                } else {
                    q.push({idx, *iter});
                }
            }
        }
        list<pair<const Method*, Block*>> blocks;
        auto iter = min_element(find_next.begin(), find_next.end());
        int idx = find_next.empty() ? find_end : iter->second;
        while (idx > 0) {
            blocks.push_front({method, vv[idx].second});
            idx = vv[idx].first;
        }
        ans.insert(ans.end(), blocks.begin(), blocks.end());
        return !blocks.empty();
    }

public:
    JitMatchTree(const Method* m, JitMatchTree* f) : method(m), father(f) {}
    ~JitMatchTree() {
        for (auto&& child : children)
            delete child.second;
    }

    void insert(vector<pair<const Method*, Block*>>& execs, int seq, int idx) {
        if (idx >= execs.size())
            return;
        const Method* m = execs[idx].first;
        Block* b = execs[idx].second;
        assert(m == method);
        if (!seqs.count(b))
            seqs[b] = seq;
        if (idx == execs.size()-1)
            return;
        if (!children.count(b))
            children[b] = new JitMatchTree(execs[idx+1].first, this);
        children[b]->insert(execs, seq, idx+1);
    }

    void match(vector<pair<const Method*, Block*>>& frame, int idx,
               set<Block*>& notVisited, vector<pair<const Method*, Block*>>& ans) {
        BlockGraph* bg = method->get_bg();
        Block* cur = nullptr;
        if (idx >= frame.size()) {
            cur = bg->block(0);
            frame.push_back({method, cur});
        } else if (idx == frame.size()-1) {
            cur = frame[idx].second;
        } else {
            cur = frame[idx].second;
            if (children.count(cur)) {
                notVisited.erase(cur);
                children[cur]->match(frame, idx+1, notVisited, ans);
            } else {
                return_frame(frame, frame.size()-idx-1, ans);
            }
        }

        while (notVisited.size()) {
            if (children.count(cur))
                children[cur]->match(frame, idx+1, notVisited, ans);
            if (!match_next(cur, ans)) {
                frame.pop_back();
                break;
            } else {
                cur = ans.back().second;
                notVisited.erase(cur);
            }
        }
    }

    static void return_method(const Method* method, Block* cur, vector<pair<const Method*, Block*>>& ans) {
        vector<pair<int, Block*>> vv;
        unordered_set<Block*> ss;
        queue<pair<int, Block*>> q;
        vector<pair<int, int>> find_next;
        int find_end = -1;
        while (!q.empty()) {
            Block* blc = q.front().second;
            int idx = vv.size();
            vv.push_back({q.front().first, blc});
            q.pop();
            if (blc->get_succs_size() == 0)
                break;
            for (auto iter = blc->get_succs_begin(); iter != blc->get_succs_end(); ++iter) {
                if (ss.count(*iter)) continue;
                ss.insert(*iter);
                q.push({idx, *iter});
            }
        }
        list<pair<const Method*, Block*>> blocks;
        int idx = vv.size()-1;
        while (idx > 0) {
            blocks.push_front({method, vv[idx].second});
            idx = vv[idx].first;
        }
        ans.insert(ans.end(), blocks.begin(), blocks.end());
        return;
    }

    static void return_frame(vector<pair<const Method*, Block*>>& frame, int count, vector<pair<const Method*, Block*>>& blocks) {
        assert(count <= frame.size());
        for (int i = 0; i < count; ++i) {
            return_method(frame.back().first, frame.back().second, blocks);
            frame.pop_back();
        }
    }
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

static void output_jitcode(FILE* fp, vector<pair<const Method*, Block*>>& blocks) {
    for (auto block : blocks)
        output_jitcode(fp, block.first, block.second);
}

static bool handle_jitcode(ExecInfo* exec, const PCStackInfo **pcs, int size,
                           vector<pair<const Method*, Block*>>& ans) {
    const jit_section* section = exec->section;
    set<const PCStackInfo*> pc_execs;
    set<Block*> block_execs;
    bool notRetry = true;
    JitMatchTree* tree = new JitMatchTree(section->cmd->mainm, nullptr);
    for (int i = 0; i < size; ++i) {
        const PCStackInfo* pc = pcs[i];
        if (pc_execs.count(pc)) {
            tree->match(exec->prev_frame, 0, block_execs, ans);
            delete tree;
            tree = new JitMatchTree(section->cmd->mainm, nullptr);
            block_execs.clear();
            pc_execs.clear();
        }
        pc_execs.insert(pc);
        vector<pair<const Method*, Block*>> frame;
        for (int j = pc->numstackframes-1; j >= 0; --j) {
            int mi = pc->methods[j];
            int bci = pc->bcis[j];
            const Method* method = section->cmd->get_method(mi);
            if (!method || !method->is_jportal()) continue;
            Block* block = method->get_bg()->block(bci);
            if (!block) continue;
            frame.push_back({method, block});
            block_execs.insert(block);
        }
        if (exec->prev_frame.empty()) {
            ans.insert(ans.end(), frame.begin(), frame.end());
            exec->prev_frame = frame;
            block_execs.clear();
            notRetry = false;
        } else {
            tree->insert(frame, i, 0);
        }
    }
    tree->match(exec->prev_frame, 0, block_execs, ans);
    delete tree;
    return notRetry;
}

static void return_exec(stack<ExecInfo*> &exec_st, const jit_section* section,
                        vector<pair<const Method*, Block*>>& ans) {
    while (!exec_st.empty() && exec_st.top()->section != section) {
        JitMatchTree::return_frame(exec_st.top()->prev_frame, exec_st.top()->prev_frame.size(), ans);
        delete exec_st.top();
        exec_st.pop();
    }
    if (exec_st.empty()) {
        exec_st.push(new ExecInfo(section));
    }
}

static void output_trace(TraceData* trace, size_t start, size_t end, FILE* fp) {
    TraceDataAccess access(*trace, start, end);
    CodeletsEntry::Codelet codelet, prev_codelet = CodeletsEntry::_illegal;
    size_t loc;
    stack<ExecInfo*> exec_st;
    while (access.next_trace(codelet, loc)) {
        switch(codelet) {
            default: {
                fprintf(stderr, "output_trace: unknown codelet(%d)\n", codelet);
                exec_st = stack<ExecInfo*>();
                break;
            }
            case CodeletsEntry::_method_entry_points: {
                if (exec_st.empty() || exec_st.top()->section)
                    exec_st.push(new ExecInfo(nullptr));
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
                    exec_st.push(new ExecInfo(nullptr));
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
                vector<pair<const Method*, Block*>> blocks;
                return_exec(exec_st, nullptr, blocks);
                output_jitcode(fp, blocks);
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
                vector<pair<const Method*, Block*>> blocks;
                if (codelet == CodeletsEntry::_jitcode_entry) {
                    ExecInfo* exec = new ExecInfo(section);
                    const Method* method = section->cmd->mainm;
                    Block* block = method->get_bg()->block(0);
                    blocks.push_back({method, block});
                    exec_st.push(exec);
                } else if (codelet == CodeletsEntry::_jitcode_osr_entry) {
                    ExecInfo* exec = new ExecInfo(section);
                    exec_st.push(exec);
                } else {
                    return_exec(exec_st, section, blocks);
                }
                handle_jitcode(exec_st.top(), pcs, size, blocks);
                output_jitcode(fp, blocks);
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
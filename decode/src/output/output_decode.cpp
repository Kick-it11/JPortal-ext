#include "decoder/decode_result.hpp"
#include "java/method.hpp"
#include "java/klass.hpp"
#include "java/analyser.hpp"
#include "java/block.hpp"
#include "output/output_decode.hpp"

#include <queue>
#include <stack>
#include <set>
#include <vector>
#include <iostream>
#include <algorithm>
#include <cassert>

using std::queue;
using std::set;
using std::stack;
using std::vector;

struct ExecInfo {
    const JitSection* section; // jit_section, nullptr indicates a segment of inter codes
    vector<pair<const Method*, Block*>> prev_frame;
    ExecInfo(): section(nullptr) { }
    ExecInfo(const JitSection* s): section(s) { }
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
        q.push({-1, cur});
        ss.insert(cur);
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
        int idx = find_end;
        if (!find_next.empty()) {
            idx = min_element(find_next.begin(), find_next.end())->second;
            seqs.erase(vv[idx].second);
        }
        while (idx > 0) {
            blocks.push_front({method, vv[idx].second});
            idx = vv[idx].first;
        }
        ans.insert(ans.end(), blocks.begin(), blocks.end());
        return !blocks.empty();
    }

    void skip_match(vector<pair<const Method*, Block*>>& frame, int idx,
                    set<pair<const Method*, Block*>>& notVisited,
                    vector<pair<const Method*, Block*>>& ans) {
        Block* cur = nullptr;
        if (idx >= frame.size()) {
            frame.push_back({method, cur});
            notVisited.erase({method, cur});
        } else {
            cur = frame[idx].second;
            notVisited.erase({method, cur});
            if (idx != frame.size()-1 && !children.count(cur)) {
                for (int i = 0; i < frame.size()-idx-1; ++i)
                    frame.pop_back();
            }
        }

        while (!seqs.empty()) {
            auto iter = min_element(seqs.begin(), seqs.end(), [](pair<Block*, int>&& l, pair<Block*, int>&& r)
                                    ->bool { return l.second < r.second; });
            cur = iter->first;
            notVisited.erase({method, cur});
            seqs.erase(iter);
            if (children.count(cur))
                children[cur]->match(frame, idx+1, notVisited, ans);
        }

        if (idx == frame.size()-1)
            frame.pop_back();
    }

public:
    JitMatchTree(const Method* m, JitMatchTree* f) : method(m), father(f) {}
    ~JitMatchTree() {
        for (auto&& child : children)
            delete child.second;
    }

    bool insert(vector<pair<const Method*, Block*>>& execs, int seq, int idx) {
        if (idx >= execs.size())
            return true;
        const Method* m = execs[idx].first;
        Block* b = execs[idx].second;
        if (m != method)
            return false;
        if (idx < execs.size()-1) {
            if (!children.count(b))
                children[b] = new JitMatchTree(execs[idx+1].first, this);
            if (!children[b]->insert(execs, seq, idx+1))
                return false;
        }
        if (!seqs.count(b))
            seqs[b] = seq;
        return true;
    }

    void match(vector<pair<const Method*, Block*>>& frame, int idx,
               set<pair<const Method*, Block*>>& notVisited, vector<pair<const Method*, Block*>>& ans) {
        notVisited.erase({method, nullptr});
        if (!method || !method->is_jportal())
            return skip_match(frame, idx, notVisited, ans);

        BlockGraph* bg = method->get_bg();
        Block* cur = nullptr;
        if (idx < frame.size() && frame[idx].first != method)
            return_frame(frame, frame.size()-idx, ans);
        if (idx >= frame.size()) {
            cur = bg->block(0);
            frame.push_back({method, cur});
            ans.push_back({method, cur});
            notVisited.erase({method, cur});
        } else {
            cur = frame[idx].second;
            if (!cur) cur = bg->block(0);
            notVisited.erase({method, cur});
            if (idx < frame.size()-1 && !children.count(cur))
                return_frame(frame, frame.size()-idx-1, ans);
        }

        while (notVisited.size()) {
            if (children.count(cur))
                children[cur]->match(frame, idx+1, notVisited, ans);
            if (!notVisited.size())
                break;
            if (!match_next(cur, ans)) {
                frame.pop_back();
                return;
            } else {
                cur = ans.back().second;
                frame[idx] = ans.back();
                notVisited.erase({method, cur});
            }
        }

    }

    static void return_method(const Method* method, Block* cur, vector<pair<const Method*, Block*>>& ans) {
        vector<pair<int, Block*>> vv;
        unordered_set<Block*> ss;
        queue<pair<int, Block*>> q;
        vector<pair<int, int>> find_next;
        int find_end = -1;
        q.push({0, cur});
        ss.insert(cur);
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

static bool output_bytecode(FILE* fp, const uint8_t* codes, size_t size) {
    // output bytecode
    for (int i = 0; i < size; i++) {
        // fwrite(codes+i, 1, 1, fp);
        fprintf(fp, "%hhu\n", *(codes+i));
        // fprintf(fp, "%s\n", Bytecodes::name_for(Bytecodes::cast(*(codes+i))));
    }
    return true;
}

static void output_jitcode(FILE* fp, vector<pair<const Method*, Block*>>& blocks) {
    for (auto block : blocks) {
        const Method* method = block.first;
        Block* blc = block.second;
        assert(method && method->is_jportal() && blc);
        const uint8_t* codes = method->get_bg()->bctcode();
        for (int i = blc->get_bct_codebegin(); i < blc->get_bct_codeend(); ++i) {
            // fwrite(codes+i, 1, 1, fp);
            fprintf(fp, "%hhu\n", *(codes+i));
            // fprintf(fp, "%s\n", Bytecodes::name_for(Bytecodes::cast(*(codes+i))));
        }
    }
}

static bool handle_jitcode(ExecInfo* exec, const PCStackInfo **pcs, int size,
                           vector<pair<const Method*, Block*>>& ans) {
    const JitSection* section = exec->section;
    set<const PCStackInfo*> pc_execs;
    set<pair<const Method*, Block*>> block_execs;
    bool notRetry = true;
    JitMatchTree* tree = new JitMatchTree(section->cmd()->mainm(), nullptr);
    auto call_match = [&exec, &tree, &block_execs, &pc_execs, &ans, section] (bool newtree) -> void {
        tree->match(exec->prev_frame, 0, block_execs, ans);
        delete tree;
        tree = newtree? new JitMatchTree(section->cmd()->mainm(), nullptr) : nullptr;
        block_execs.clear();
        pc_execs.clear();
    };
    for (int i = 0; i < size; ++i) {
        const PCStackInfo* pc = pcs[i];
        if (pc_execs.count(pc))
            call_match(true);
        vector<pair<const Method*, Block*>> frame;
        for (int j = pc->numstackframes-1; j >= 0; --j) {
            int mi = pc->methods[j];
            int bci = pc->bcis[j];
            const Method* method = section->cmd()->method(mi);
            Block* block = (method && method->is_jportal())? method->get_bg()->block(bci) : (Block*)(long long)bci;
            frame.push_back({method, block});
            block_execs.insert({method, block});
        }
        if (exec->prev_frame.empty()) {
            for (auto blc : frame)
                if (blc.first && blc.first->is_jportal() && blc.second)
                    ans.push_back(blc);
            exec->prev_frame = frame;
            block_execs.clear();
            notRetry = false;
        } else if (!tree->insert(frame, i, 0)) {
            call_match(true);
            tree->insert(frame, i, 0);
        }
        pc_execs.insert(pc);
    }
    call_match(false);
    return notRetry;
}

static void return_exec(stack<ExecInfo*> &exec_st, const JitSection* section,
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
            case CodeletsEntry::_method_entry: {
                if (exec_st.empty() || exec_st.top()->section)
                    exec_st.push(new ExecInfo(nullptr));
                break;
            }
            case CodeletsEntry::_throw_ArrayIndexOutOfBoundsException:
            case CodeletsEntry::_throw_ArrayStoreException:
            case CodeletsEntry::_throw_ArithmeticException:
            case CodeletsEntry::_throw_ClassCastException:
            case CodeletsEntry::_throw_NullPointerException:
            case CodeletsEntry::_throw_StackOverflowError: {
                break;
            }
            case CodeletsEntry::_rethrow_exception: {
                break;
            }
            case CodeletsEntry::_deopt:
            case CodeletsEntry::_deopt_reexecute_return: {
                // deopt
                if (!exec_st.empty() && exec_st.top()->section) {
                    delete exec_st.top();
                    exec_st.pop();
                }
                if (exec_st.empty() || exec_st.top()->section)
                    exec_st.push(new ExecInfo(nullptr));
                break;
            }
            case CodeletsEntry::_throw_exception: {
                // exception handling or throw
                break;
            }
            case CodeletsEntry::_remove_activation:
            case CodeletsEntry::_remove_activation_preserving_args: {
                // after throw exception or deoptimize
                break;
            }
            case CodeletsEntry::_invoke_return:
            case CodeletsEntry::_invokedynamic_return: 
            case CodeletsEntry::_invokeinterface_return: {
                break;
            }
            case CodeletsEntry::_bytecode: {
                const uint8_t* codes;
                size_t size;
                assert(trace->get_inter(loc, codes, size) && codes);
                vector<pair<const Method*, Block*>> blocks;
                return_exec(exec_st, nullptr, blocks);
                output_jitcode(fp, blocks);
                output_bytecode(fp, codes, size);
                break;
            }
            case CodeletsEntry::_jitcode_entry:
            case CodeletsEntry::_jitcode_osr_entry:
            case CodeletsEntry::_jitcode: {
                const JitSection *section = nullptr;
                const PCStackInfo **pcs = nullptr;
                size_t size;
                assert(trace->get_jit(loc, pcs, size, section)
                       && pcs && section && section->cmd());
                vector<pair<const Method*, Block*>> blocks;
                if (codelet == CodeletsEntry::_jitcode_entry) {
                    ExecInfo* exec = new ExecInfo(section);
                    const Method* method = section->cmd()->mainm();
                    Block* block = method->get_bg()->block(0);
                    blocks.push_back({method, block});
                    exec->prev_frame.push_back({method, block});
                    exec_st.push(exec);
                } else if (codelet == CodeletsEntry::_jitcode_osr_entry) {
                    ExecInfo* exec = new ExecInfo(section);
                    exec_st.push(exec);
                } else {
                    return_exec(exec_st, section, blocks);
                }
                handle_jitcode(exec_st.top(), pcs, size, blocks);
                output_jitcode(fp, blocks);
                break;
            }
        }
    }
    while (!exec_st.empty()) {
        vector<pair<const Method*, Block*>> blocks;
        JitMatchTree::return_frame(exec_st.top()->prev_frame, exec_st.top()->prev_frame.size(), blocks);
        output_jitcode(fp, blocks);
        delete exec_st.top();
        exec_st.pop();
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
             [] (const pair<ThreadSplit, TraceData*>& x, const pair<ThreadSplit, TraceData*>& y) -> bool {
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

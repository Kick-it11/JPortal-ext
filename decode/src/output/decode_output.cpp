#include "structure/PT/decode_result.hpp"
#include "structure/java/method.hpp"
#include "structure/java/klass.hpp"
#include "structure/java/analyser.hpp"
#include "output/decode_output.hpp"

#include <queue>
#include <stack>
#include <iostream>
#include <algorithm>
#include <cassert>

struct JitMap {
    const jit_section* section;
    const Method* main_method;
    // all executions
    map<const Method*, map<Block*, int>> execs;
    // caller block -> callee method
    map<Block*, const Method*> children;

    stack<pair<const Method*, pair<Block*, bool>>> frames;

    JitMap(const jit_section* s, const Method* m): section(s), main_method(m) {}
};

static void get_jitcode(const Method* method, list<Block*> &blocks, vector<u1>& ans) {
    const u1* codes = method->get_bg()->bctcode();
    for (auto blc : blocks) {
        for (int i = blc->get_bct_codebegin(); i < blc->get_bct_codeend(); ++i) {
            ans.push_back(*(codes+i));
        }
    }
}

static void get_jitcode(const Method* method, Block* blc, vector<u1>& ans) {
    const u1* codes = method->get_bg()->bctcode();
    for (int i = blc->get_bct_codebegin(); i < blc->get_bct_codeend(); ++i) {
        ans.push_back(*(codes+i));
    }
}

static void find_jitpath(JitMap* jm, const Method* method, Block* pre, list<Block*>& blocks) {
    vector<pair<int, Block*>> vv;
    unordered_set<Block*> ss;
    queue<pair<int, Block*>> q;
    q.push({-1, pre});
    int find_end = -1;
    vector<pair<int, int>> find_next;
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
            if (jm->execs[method].count(*iter)) {
                find_next.push_back({jm->execs[method][*iter], vv.size()});
                vv.push_back({idx, *iter});
            } else {
                q.push({idx, *iter});
            }
        }
    }
    auto iter = min_element(find_next.begin(), find_next.end());
    int idx = find_next.empty() ? find_end : iter->second;
    if (!find_next.empty())
        jm->execs[method].erase(jm->execs[method].find(vv[idx].second));
    while (idx > 0) {
        blocks.push_front(vv[idx].second);
        idx = vv[idx].first;
    }
    return;
}

static void map_jitcode(JitMap* jm, const Method* lm, Block* lb, vector<u1>& ans) {
    while (!jm->frames.empty()) {
        const Method* cur_m = jm->frames.top().first;
        Block* cur_b = jm->frames.top().second.first;
        bool should_call = jm->frames.top().second.second;
        if (jm->children.count(cur_b) && should_call) {
            const Method* callee = jm->children[cur_b];
            Block* start_b = callee->get_bg()->block(0);
            get_jitcode(callee, start_b, ans);
            jm->frames.top().second.second = false;
            jm->frames.push({callee, {start_b, true}});
            continue;
        }
        if (!cur_b) jm->frames.pop();
        if (cur_m == lm && cur_b == lb) break;
        if (!cur_b) continue;
        list<Block*> blocks;
        find_jitpath(jm, cur_m, cur_b, blocks);
        get_jitcode(cur_m, blocks, ans);
        jm->frames.top().second.first = blocks.empty()?nullptr:blocks.back();
    }
    jm->execs.clear();
    jm->children.clear();
}

static void output_jitcode(TraceData* trace, TraceDataAccess& access, FILE* fp, size_t loc,
                           CodeletsEntry::Codelet codelet, Analyser *analyser,
                           stack<JitMap*>& jm_st) {
    const jit_section *section = nullptr;
    const PCStackInfo **pcs = nullptr;
    size_t size, new_loc;
    assert(trace->get_jit(loc, pcs, size, section, new_loc) && pcs && section && section->cmd);
    const Method* method = section->cmd->mainm;
    if (!method || !method->is_jportal()) return;
    vector<u1> ans;
    if (codelet == CodeletsEntry::_jitcode_entry) {
        JitMap* jm = new JitMap(section, method);
        Block* start_b = method->get_bg()->block(0);
        get_jitcode(method, start_b, ans);
        jm->frames.push({method, {start_b, true}});
        jm_st.push(jm);
    } else if (codelet == CodeletsEntry::_jitcode_osr_entry) {
        JitMap* jm = new JitMap(section, method);
        jm_st.push(jm);
    } else {
        while (!jm_st.empty() && jm_st.top()->section != section) {
            map_jitcode(jm_st.top(), jm_st.top()->main_method, nullptr, ans);
            delete jm_st.top();
            jm_st.pop();
        }
        if (jm_st.empty()) {
            JitMap* jm = new JitMap(section, method);
            jm_st.push(jm);
        }
    }
    JitMap* jm = jm_st.top();
    unordered_set<Block*> execs;
    const Method* lm = nullptr;
    Block* lb = nullptr;
    if (!jm->frames.empty()) execs.insert(jm->frames.top().second.first);
    // todo: handle block -> block might also be a loop
    for (int i = 0; i < size; i++) {
        const PCStackInfo *pc = pcs[i];
        Block* prev_block = nullptr;
        for (int j = pc->numstackframes-1; j >= 0; --j) {
            int mi = pc->methods[j];
            int bci = pc->bcis[j];
            method = section->cmd->get_method(mi);
            if (!method || !method->is_jportal()) continue;
            Block* block = method->get_bg()->block(bci);

            if (jm->frames.empty()) {
                fprintf(stderr, "decode_output: an osr method or a match failure(%ld).\n", loc);
                if (!block) block = method->get_bg()->block(0);
                get_jitcode(method, block, ans);
                jm->frames.push({method, {block, true}});
                execs.insert(block);
            }
            if (execs.size() == 1 && jm->frames.top().second.first == block)
                continue;
            if (j == 0 && execs.count(block)) {
                map_jitcode(jm, lm, lb, ans);
                execs.clear();
                if (!jm->frames.empty()) execs.insert(jm->frames.top().second.first);
                --i; break;
            }
            if (j == 0 && block) execs.insert(block);
            if (!jm->execs[method].count(block)) jm->execs[method][block] = i;

            if (prev_block) jm->children[prev_block] = method;
            lm = method;
            lb = block;
            prev_block = block;
        }
    }
    map_jitcode(jm, lm, lb, ans);
    for (auto code : ans) fprintf(fp, "%hhu\n", code);
}

static void output_bytecode(TraceData* trace, TraceDataAccess& access, FILE* fp, size_t loc) {
    const u1* codes;
    size_t size;
    size_t new_loc;
    if (!trace->get_inter(loc, codes, size, new_loc)) {
        fprintf(stderr, "Decode output: cannot get inter.\n");
        return;
    }
    // output bytecode
    for (int i = 0; i < size; i++) {
        // fwrite(codes+i, 1, 1, fp);
        fprintf(fp, "%hhu\n", *(codes+i));
        // fprintf(fp, "%s\n", Bytecodes::name_for(Bytecodes::cast(*(codes+i))));
    }
    access.set_current(new_loc);
}

// per thread output
void decode_output(Analyser* analyser, list<TraceData*> &traces) {
    if (!analyser) return;
    map<long, vector<pair<ThreadSplit, TraceData*>>> threads_data;
    for (auto && trace : traces) {
        for (auto && threads: trace->get_thread_map()) {
            for (auto && thread : threads.second) {
                threads_data[threads.first].push_back({thread, trace});
            }
        }
    }
    for (auto iter = threads_data.begin(); iter != threads_data.end(); ++iter) {
        sort(iter->second.begin(), iter->second.end(),
             [] (pair<ThreadSplit, TraceData*> x, pair<ThreadSplit, TraceData*> y) -> bool {
                return x.first.start_time < y.first.start_time
                       || x.first.start_time == y.first.start_time
                          &&  x.first.end_time < y.first.end_time;});
    }
    for (auto iter1 = threads_data.begin(); iter1 != threads_data.end(); ++iter1) {
        char name[32];
        sprintf(name, "thrd%ld", iter1->first);
        // FILE *fp = fopen(name, "wb");
        FILE *fp = fopen(name, "w");
        if (!fp) {
            fprintf(stderr, "Decode output: open decode file(%s) error\n", name);
            continue;
        }
        for (auto iter2 = iter1->second.begin(); iter2 != iter1->second.end(); ++iter2) {
            TraceData *trace = iter2->second;
            TraceDataAccess access(*trace, iter2->first.start_addr, iter2->first.end_addr);
            CodeletsEntry::Codelet codelet, prev_codelet = CodeletsEntry::_illegal;
            size_t loc;
            stack<JitMap*> jm_st;
            int number_of_sections = 0;
            while (access.next_trace(codelet, loc)) {
                if (prev_codelet == CodeletsEntry::_jitcode ||
                    prev_codelet == CodeletsEntry::_jitcode_entry ||
                    prev_codelet == CodeletsEntry::_jitcode_osr_entry) {
                    if (codelet == CodeletsEntry::_invoke_return_entry_points ||
                        codelet == CodeletsEntry::_invokedynamic_return_entry_points ||
                        codelet == CodeletsEntry::_invokeinterface_return_entry_points ||
                        codelet == CodeletsEntry::_bytecode) {
                        vector<u1> ans;
                        for (int i = 0; i < jm_st.size()-number_of_sections && !jm_st.empty(); ++i) {
                            map_jitcode(jm_st.top(), jm_st.top()->main_method, nullptr, ans);
                            delete jm_st.top();
                            jm_st.pop();
                        }
                        for (auto code : ans) fprintf(fp, "%hhu\n", code);
                    } else if (codelet == CodeletsEntry::_deopt_entry_points ||
                               codelet == CodeletsEntry::_deopt_reexecute_return_entry_points ||
                               codelet == CodeletsEntry::_rethrow_exception_entry_entry_points ||
                               codelet == CodeletsEntry::_throw_exception_entry_points) {
                        for (int i = 0; i < jm_st.size()-number_of_sections && !jm_st.empty(); ++i) {
                            delete jm_st.top();
                            jm_st.pop();
                        }
                    } else if (codelet != CodeletsEntry::_method_entry_points &&
                               codelet != CodeletsEntry::_jitcode_osr_entry &&
                               codelet != CodeletsEntry::_jitcode_entry &&
                               codelet != CodeletsEntry::_jitcode) {
                        cerr << "Unknown after jit: " << codelet << endl;
                    }
                }
                if (codelet == CodeletsEntry::_bytecode) {
                    output_bytecode(trace, access, fp, loc);
                    number_of_sections = jm_st.size();
                } else if (codelet == CodeletsEntry::_jitcode_entry ||
                           codelet == CodeletsEntry::_jitcode) {
                    output_jitcode(trace, access, fp, loc, codelet, analyser, jm_st);
                }
                prev_codelet = codelet;
            }
        }
        fclose(fp);
    }
}

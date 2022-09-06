#include "structure/PT/decode_result.hpp"
#include "structure/java/method.hpp"
#include "structure/java/klass.hpp"
#include "structure/java/analyser.hpp"
#include "output/decode_output.hpp"

#include <queue>
#include <stack>
#include <iostream>
#include <algorithm>

static bool cmp(pair<ThreadSplit, TraceData*> x, 
          pair<ThreadSplit, TraceData*> y) {
    if (x.first.start_time < y.first.start_time
        || x.first.start_time == y.first.start_time && 
            x.first.end_time < y.first.end_time)
        return true;
    return false;
}

static bool first_block(const Method* method, int bci) {
    if (!method || !method->is_jportal())
        return false;
    BlockGraph *bg = method->get_bg();
    if (bg->blocks().empty()) return false;
    Block* start = bg->blocks()[0];
    auto bct_iter = bg->bct_offset().find(bci);
    Block* block = (bct_iter == bg->bct_offset().end()) ? nullptr :
                   bg->blocks()[bg->block_id()[bct_iter->second]];
    return start == block;
}

static bool get_bytecodes(const Method* method, int src, int dest, bool& ret, vector<u1>& ans) {
    if (!method || !method->is_jportal())
        return false;
    BlockGraph *bg = method->get_bg();
    const u1* codes = bg->bctcode();

    int src_bct = bg->bct_offset().count(src)?(bg->bct_offset().find(src)->second):-1;
    Block* src_block = (src_bct == -1) ? nullptr : bg->blocks()[bg->block_id()[src_bct]];
    int dest_bct = bg->bct_offset().count(dest)?(bg->bct_offset().find(dest)->second):-1;
    Block* dest_block = (dest_bct == -1) ? nullptr : bg->blocks()[bg->block_id()[dest_bct]];
    if (!src_block && !dest_block) {
        cerr << "unknown get_bytecodes" << endl;
        return false;
    }
    if (!src_block) {
        src_bct = dest_block->get_bct_codebegin()-1;
        src_block = dest_block;
    }
    if (src_block == dest_block) {
        if (dest_block->get_succs_size() == 0 && dest_bct+1 == dest_block->get_bct_codeend())
            ret = true;
        for (int i = src_bct+1; i < dest_bct+1; ++i)
            ans.push_back(*(codes+i));
        return true;
    }

    vector<Block*> ans_blocks;
    queue<vector<Block*>> q;
    q.push(vector<Block*>(1, src_block));
    const int BFS_LIMIT = 4;
    while (!q.empty()) {
        vector<Block*> blocks = q.front();
        q.pop();
        if (blocks.size() >= BFS_LIMIT) {
            ans_blocks = blocks;
            break;
        }
        Block* block = blocks.back();
        int succs_size = block->get_succs_size();
        if (succs_size == 0 || succs_size == 1) {
            Block* next_block = succs_size == 0 ? nullptr : *block->get_succs_begin();
            if (dest_block == next_block) {
                ans_blocks = blocks;
                ans_blocks.push_back(dest_block);
                break;
            } else {
                if (q.empty() && !next_block) { ans_blocks = blocks; break; }
                if (next_block) {
                    blocks.push_back(next_block);
                    q.push(blocks);
                }
                continue;
            }
        }
        for (auto iter = block->get_succs_begin(); iter != block->get_succs_end(); ++iter) {
            blocks.push_back(*iter);
            if (*iter == dest_block) break;
            else {
                q.push(blocks);
                blocks.pop_back();
            }
        }
        if (blocks.back() == dest_block) { ans_blocks = blocks; break; }
    }

    for (int i = src_bct+1; i < src_block->get_bct_codeend(); ++i)
        ans.push_back(*(codes+i));
    for (int i = 1; i < ans_blocks.size()-1; ++i) {
        Block* block = ans_blocks[i];
        for (int j = block->get_bct_codebegin(); j < block->get_bct_codeend(); ++j)
            ans.push_back(*(codes+j));
    }
    if (dest_block) {
        for (int i = dest_block->get_bct_codebegin(); i < dest_bct+1; ++i)
            ans.push_back(*(codes+i));
    }
    if (ans_blocks.back() == dest_block && (!dest_block ||
        dest_block->get_succs_size() == 0 &&
        dest_bct+1 == dest_block->get_bct_codeend()))
        ret = true;
    return ans_blocks.back() == dest_block;
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
        fprintf(fp, "%hhu\n", *(codes+i));
        // fprintf(fp, "%s\n", Bytecodes::name_for(Bytecodes::cast(*(codes+i))));
    }
    access.set_current(new_loc);
}

static void print_ans(FILE* fp, list<vector<u1>>& ans) {
    int size = ans.size();
    for (int i = 0; i < size; ++i) {
        for (auto byte : ans.front()) {
            fprintf(fp, "%hhu\n", byte);
            //fprintf(fp, "%s\n", Bytecodes::name_for(Bytecodes::cast(byte)));
        }
        ans.pop_front();
    }
}

static void jit_return(FILE* fp, int prev_frame, stack<pair<const Method*, int>> &jit_stack) {
    list<vector<u1>> ans;
    for (int j = 0; j < prev_frame && !jit_stack.empty(); ++j) {
        const Method* method = jit_stack.top().first;
        int bci = jit_stack.top().second;
        jit_stack.pop();
        if (!fp || !method || !method->is_jportal())
            continue;
        vector<u1> aa;
        bool ret = true;
        if (!get_bytecodes(method, bci, -1, ret, aa)) {
            aa.clear();
            cerr << -1 << " " << method->get_klass()->get_name() << " " << method->get_name() << " " << bci << endl;
        }
        ans.emplace_back(aa);
    }
    if (fp) print_ans(fp, ans);
}

//////////////////////////////////////////
/////////////////////////////////////////
// stack<prev_frame> & stack<jit_section>
int ssss = 0;
int SSSS = 2;
static void output_jit(TraceData* trace, TraceDataAccess& access, FILE* fp, size_t loc,
                       CodeletsEntry::Codelet codelet, stack<pair<const Method*, int>> &jit_stack,
                       stack<pair<const jit_section*, int>>& jit_stack2, Analyser* analyser) {
    const jit_section *section = nullptr;
    const PCStackInfo **pcs = nullptr;
    size_t size;
    size_t new_loc;
    if (!trace->get_jit(loc, pcs, size, section, new_loc) || !pcs || !section) {
        fprintf(stderr, "Decode output: cannot get jit.\n");
        return;
    }
    const CompiledMethodDesc *cmd = section->cmd;
    if (!cmd) {
        access.set_current(new_loc);
        return;
    }
    string klass_name, name, sig;
    list<vector<u1>> ans;
    // pop last frame methods
    if (codelet != CodeletsEntry::_jitcode_entry) {
        while (!jit_stack2.empty() && jit_stack2.top().first != section) {
            jit_return(fp, jit_stack2.top().second, jit_stack);
            jit_stack2.pop();
        }
    }
    if (jit_stack2.empty() || jit_stack2.top().first != section)
        jit_stack2.push({section, 0});
    for (int i = 0; i < size; i++) {
        stack<pair<const Method *, int>> temp_stack;
        const PCStackInfo *pc = pcs[i];
        // current frame
        for (int j = 0 ; j < pc->numstackframes; ++j) {
            int mi = pc->methods[j];
            int bci = pc->bcis[j];
            if (!cmd->get_method_desc(mi, klass_name, name, sig)) {
                fprintf(stderr, "decode: unknown method index\n");
                jit_stack = stack<pair<const Method*, int>>();
                return;
            }
            const Klass* klass = analyser->getKlass(klass_name);
            if (!klass) continue;
            const Method* method = klass->getMethod(name+sig);
            if (!method || !method->is_jportal()) continue;
            // ssss=(ssss+1)%SSSS;
            // if (ssss==0) {
            //     fprintf(fp, "%s %d %lu\n", (name+sig).c_str(), bci, loc);
            // }
            if (jit_stack.empty() || jit_stack.top().first != method) {
                vector<u1> aa;
                if (bci == -1) bci = 0;
                bool ret = false;
                if (!get_bytecodes(method, -1, bci, ret, aa)) {
                    aa.clear();
                    cerr << -2 << " " << method->get_klass()->get_name() << " " << method->get_name() << " " << pc << " -1 " << bci << " " << jit_stack.size() << " " << loc << endl;
                }
                if (!ret && i+1 < size && (pc->numstackframes-j > pcs[i+1]->numstackframes
                    || mi != pcs[i+1]->methods[j+pcs[i+1]->numstackframes-pc->numstackframes])) {
                    ret = true;
                    if (!get_bytecodes(method, bci, -1, ret, aa)) {
                        cerr << -3 << " " << method->get_klass()->get_name() << " " << method->get_name() << " " << pc << " " << bci << " " << loc << endl;
                        aa.clear();
                    }
                }
                ans.emplace_front(aa);
                if (!ret) temp_stack.push({method, bci});
            } else {
                int prev_bci = jit_stack.top().second;
                if (prev_bci == bci) break;
                //if (bci == -1) bci = prev_bci;
                vector<u1> aa;
                bool ret = false;
                if (!get_bytecodes(method, prev_bci, bci, ret, aa)) {
                    aa.clear();
                    if (!first_block(method, bci) || !get_bytecodes(method, -1, bci, ret, aa)) {
                        // self call failure
                        aa.clear();
                        cerr << -4 << " " << method->get_klass()->get_name() << " " << method->get_name() << " " << pc << " " << prev_bci << " " << bci << " " << loc << endl;
                        bci = prev_bci;
                        --jit_stack2.top().second;
                        jit_stack.pop();
                    }
                } else {
                    --jit_stack2.top().second;
                    jit_stack.pop();
                }
                if (!ret && i+1 < size && (pc->numstackframes-j > pcs[i+1]->numstackframes
                    || mi != pcs[i+1]->methods[j+pcs[i+1]->numstackframes-pc->numstackframes])) {
                    ret = true;
                    if (!get_bytecodes(method, bci, -1, ret, aa)) {
                        aa.clear();
                        cerr << -5 << " " << method->get_klass()->get_name() << " " << method->get_name() << " " << pc << " " << bci << " " << loc << endl;
                    }
                }
                ans.emplace_front(aa);
                if (!ret) temp_stack.push({method, bci});
            }
        }
        print_ans(fp, ans);
        jit_stack2.top().second += temp_stack.size();
        while (!temp_stack.empty()) {
            jit_stack.push(temp_stack.top());
            temp_stack.pop();
        }
    }
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
        sort(iter->second.begin(), iter->second.end(), cmp);
    }
    for (auto iter1 = threads_data.begin(); iter1 != threads_data.end(); ++iter1) {
        char name[32];
        sprintf(name, "thrd%ld", iter1->first);
        FILE *fp = fopen(name, "w");
        if (!fp) {
            fprintf(stderr, "Decode output: open decode file(%s) error\n", name);
            continue;
        }
        for (auto iter2 = iter1->second.begin(); iter2 != iter1->second.end(); ++iter2) {
            TraceData *trace = iter2->second;
            TraceDataAccess access(*trace, iter2->first.start_addr, iter2->first.end_addr);
            stack<pair<const Method*, int>> jit_stack;
            stack<pair<const jit_section*, int>> jit_stack2;
            int number_of_sections = 0;
            CodeletsEntry::Codelet codelet, prev_codelet = CodeletsEntry::_illegal;
            size_t loc;
            while (access.next_trace(codelet, loc)) {
                if (prev_codelet == CodeletsEntry::_jitcode ||
                    prev_codelet == CodeletsEntry::_jitcode_entry) {
                    if (codelet == CodeletsEntry::_invoke_return_entry_points ||
                        codelet == CodeletsEntry::_invokedynamic_return_entry_points ||
                        codelet == CodeletsEntry::_invokeinterface_return_entry_points ||
                        codelet == CodeletsEntry::_bytecode) {
                        for (int i = 0; i < jit_stack2.size()-number_of_sections && !jit_stack2.empty(); ++i) {
                            jit_return(fp, jit_stack2.top().second, jit_stack);
                            jit_stack2.pop();
                        }
                        number_of_sections = jit_stack2.size();
                    } else if (codelet == CodeletsEntry::_deopt_entry_points ||
                               codelet == CodeletsEntry::_deopt_reexecute_return_entry_points ||
                               codelet == CodeletsEntry::_rethrow_exception_entry_entry_points ||
                               codelet == CodeletsEntry::_throw_exception_entry_points) {
                        if (!jit_stack2.empty()) {
                            jit_return(nullptr, jit_stack2.top().second, jit_stack);
                            jit_stack2.pop();
                            --number_of_sections;
                        }
                    } else if (codelet != CodeletsEntry::_method_entry_points &&
                               codelet != CodeletsEntry::_jitcode_entry &&
                               codelet != CodeletsEntry::_jitcode) {
                        cerr << "Unknown after jit: " << codelet << endl;
                    }
                }
                if (codelet == CodeletsEntry::_bytecode) {
                    output_bytecode(trace, access, fp, loc);
                    number_of_sections = jit_stack2.size();
                } else if (codelet == CodeletsEntry::_jitcode_entry ||
                           codelet == CodeletsEntry::_jitcode) {
                    output_jit(trace, access, fp, loc, codelet, jit_stack, jit_stack2, analyser);
                }
                prev_codelet = codelet;
            }
        }
        fclose(fp);
    }
}

#include "decoder/decode_data.hpp"
#include "decoder/decode_data_output.hpp"
#include "java/analyser.hpp"
#include "java/block.hpp"

#include <cassert>
#include <iostream>
#include <fstream>

struct ExecFrame
{
    /* for a Interpreter frame, section is nullptr, and prev_frame.size = 1 */
    const JitSection *section;
    std::vector<std::pair<const Method *, Block *>> prev_frame;
    ExecFrame() : section(nullptr) {}
    ExecFrame(const JitSection *s) : section(s) {}
};

DecodeDataOutput::DecodeDataOutput(const std::vector<DecodeData *> &data)
{
    _splits = DecodeData::sort_all_by_time(data);
}

void DecodeDataOutput::output(const std::string prefix)
{
    // for (auto &&thread : _splits)
    // {
    //     std::vector<ExecFrame *> frames;
    //     std::ofstream file(prefix + "-" + "thrd" + std::to_string(thread.first));
    //     for (auto &&split : thread.second)
    //     {
    //         DecodeDataAccess access(split);
    //         DecodeData::DecodeDataType type;
    //         uint64_t loc;
    //         while (access.next_trace(type, loc))
    //         {
    //             switch (type)
    //             {
    //             case DecodeData::_method_entry:
    //             {
    //                 const Method *method;
    //                 if (!access.get_method_entry(loc, method))
    //                 {
    //                     std::cerr << "DecodeDataOutput error: Fail to get method entry" << std::endl;
    //                     exit(1);
    //                 }
                    
    //             }
    //             case DecodeData::_taken:
    //                 break;
    //             case DecodeData::_not_taken:
    //                 break;
    //             case DecodeData::_exception:
    //             {
    //                 const Method *method;
    //                 int current_bci, handler_bci;
    //                 if (!access.get_exception_handling(loc, method, current_bci, handler_bci))
    //                 {
    //                     std::cerr << "DecodeDataOutput error: Fail to get exception" << std::endl;
    //                     exit(1);
    //                 }
    //                 break;
    //             }
    //             case DecodeData::_deoptimization:
    //             {
    //                 const Method *method;
    //                 int bci;
    //                 if (!access.get_deoptimization(loc, method, bci))
    //                 {
    //                     std::cerr << "DecodeDataOutput error: Fail to get deoptimization" << std::endl;
    //                     exit(1);
    //                 }
    //                 break;
    //             }
    //             case DecodeData::_jit_entry:
    //             case DecodeData::_jit_osr_entry:
    //             case DecodeData::_jit_code:
    //             {
    //                 const JitSection *section;
    //                 const PCStackInfo **info;
    //                 uint64_t size;
    //                 if (!access.get_jit_code(loc, section, info, size))
    //                 {
    //                     std::cerr << "DecodeDataOutput error: Fail to get jit code" << std::endl;
    //                     exit(1);
    //                 }
    //                 if (type == DecodeData::_jit_entry)
    //                 {
    //                 }
    //                 if (type == DecodeData::_jit_osr_entry)
    //                 {
    //                 }
    //                 if (type == DecodeData::_jit_code)
    //                 {
    //                 }
    //                 break;
    //             }
    //             case DecodeData::_jit_exception:
    //                 break;
    //             case DecodeData::_jit_deopt:
    //                 break;
    //             case DecodeData::_jit_deopt_mh:
    //                 break;
    //             case DecodeData::_data_loss:
    //                 break;
    //             case DecodeData::_decode_error:
    //                 break;
    //             default:
    //                 std::cerr << "DecodeDataOutput error: unknown type" << std::endl;
    //                 exit(1);
    //             }
    //         }
    //     }
    // }
}

void DecodeDataOutput::print()
{
    for (auto &&thread : _splits)
    {
        std::cout << "Thread " << thread.first << std::endl;
        for (auto &&split : thread.second)
        {
            std::cout << "  Part: " << split.start_addr << " " << split.end_addr
                      << " " << split.start_time << " " << split.end_time << std::endl;
            DecodeDataAccess access(split);
            DecodeData::DecodeDataType type;
            uint64_t loc;
            while (access.next_trace(type, loc))
            {
                switch (type)
                {
                case DecodeData::_method_entry:
                {
                    const Method *method;
                    if (!access.get_method_entry(loc, method))
                    {
                        std::cerr << "DecodeDataOutput error: Fail to get method entry" << std::endl;
                        exit(1);
                    }
                    std::cout << "    Method Entry: " << method->get_klass()->get_name()
                              << " " << method->get_name() << std::endl;
                    break;
                }
                case DecodeData::_method_exit:
                    std::cout << "    Method Exit" << std::endl;
                    break;
                case DecodeData::_taken:
                    std::cout << "    Branch Taken" << std::endl;
                    break;
                case DecodeData::_not_taken:
                    std::cout << "    Branch Not Taken" << std::endl;
                    break;
                case DecodeData::_switch_case:
                {
                    int index;
                    if (!access.get_switch_case_index(loc, index))
                    {
                        std::cerr << "DecodeDataOutput error: Fail to get method entry" << std::endl;
                        exit(1);
                    }
                    std::cout << "    Switch Case Index: " << index << std::endl;
                }
                case DecodeData::_switch_default:
                    std::cout << "    Switch Default" << std::endl;
                    break;
                case DecodeData::_invoke_site:
                    std::cout << "    Invoke Site" << std::endl;
                    break;
                case DecodeData::_exception:
                {
                    const Method *method;
                    int current_bci, handler_bci;
                    if (!access.get_exception_handling(loc, method, current_bci, handler_bci))
                    {
                        std::cerr << "DecodeDataOutput error: Fail to get exception" << std::endl;
                        exit(1);
                    }
                    std::cout << "    Exception Handling: " << method->get_klass()->get_name()
                              << " " << method->get_name() << " " << current_bci
                              << " " << handler_bci << std::endl;
                    break;
                }
                case DecodeData::_deoptimization:
                {
                    const Method *method;
                    int bci;
                    if (!access.get_deoptimization(loc, method, bci))
                    {
                        std::cerr << "DecodeDataOutput error: Fail to get deoptimization" << std::endl;
                        exit(1);
                    }
                    std::cout << "    Deoptimization: " << method->get_klass()->get_name()
                              << " " << method->get_name() << " " << bci << std::endl;
                    break;
                }
                case DecodeData::_jit_entry:
                case DecodeData::_jit_osr_entry:
                case DecodeData::_jit_code:
                {
                    const JitSection *section;
                    const PCStackInfo **info;
                    uint64_t size;
                    if (!access.get_jit_code(loc, section, info, size))
                    {
                        std::cerr << "DecodeDataOutput error: Fail to get jit code" << std::endl;
                        exit(1);
                    }
                    if (type == DecodeData::_jit_entry)
                    {
                        std::cout << "  Jit code entry: ";
                    }
                    if (type == DecodeData::_jit_osr_entry)
                    {
                        std::cout << "Jit code osr entry: ";
                    }
                    if (type == DecodeData::_jit_code)
                    {
                        std::cout << "Jit code: ";
                    }
                    std::cout << section->mainm()->get_klass()->get_name()
                              << " " << section->mainm()->get_name()
                              << " " << size << std::endl;
                    break;
                }
                case DecodeData::_jit_exception:
                    std::cout << "    Jit exception" << std::endl;
                    break;
                case DecodeData::_jit_deopt:
                    std::cout << "    Jit deopt" << std::endl;
                    break;
                case DecodeData::_jit_deopt_mh:
                    std::cout << "    Jit deopt mh" << std::endl;
                    break;
                case DecodeData::_data_loss:
                    std::cout << "    Data loss" << std::endl;
                    break;
                case DecodeData::_decode_error:
                    std::cout << "    Decode error" << std::endl;
                    break;
                default:
                    std::cerr << "DecodeDataOutput error: unknown type" << std::endl;
                    exit(1);
                }
            }
        }
    }
}

// class JitMatchTree
// {
// private:
//     const Method *method;
//     std::map<Block *, int> seqs;
//     JitMatchTree *father;
//     std::map<Block *, JitMatchTree *> children;

//     bool match_next(Block *cur, std::vector<std::pair<const Method *, Block *>> &ans)
//     {
//         std::vector<std::pair<int, Block *>> vv;
//         std::unordered_set<Block *> ss;
//         std::queue<std::pair<int, Block *>> q;
//         std::vector<std::pair<int, int>> find_next;
//         int find_end = -1;
//         q.push({-1, cur});
//         ss.insert(cur);
//         while (!q.empty())
//         {
//             Block *blc = q.front().second;
//             int idx = vv.size();
//             vv.push_back({q.front().first, blc});
//             q.pop();
//             if (blc->get_succs_size() == 0)
//             {
//                 if (find_end == -1)
//                     find_end = idx;
//                 continue;
//             }
//             for (auto iter = blc->get_succs_begin(); iter != blc->get_succs_end(); ++iter)
//             {
//                 if (ss.count(*iter))
//                     continue;
//                 ss.insert(*iter);
//                 if (seqs.count(*iter))
//                 {
//                     find_next.push_back({seqs[*iter], vv.size()});
//                     vv.push_back({idx, *iter});
//                 }
//                 else
//                 {
//                     q.push({idx, *iter});
//                 }
//             }
//         }
//         std::list<std::pair<const Method *, Block *>> blocks;
//         int idx = find_end;
//         if (!find_next.empty())
//         {
//             idx = min_element(find_next.begin(), find_next.end())->second;
//             seqs.erase(vv[idx].second);
//         }
//         while (idx > 0)
//         {
//             blocks.push_front({method, vv[idx].second});
//             idx = vv[idx].first;
//         }
//         ans.insert(ans.end(), blocks.begin(), blocks.end());
//         return !blocks.empty();
//     }

//     void skip_match(std::vector<std::pair<const Method *, Block *>> &frame, int idx,
//                     std::set<std::pair<const Method *, Block *>> &notVisited,
//                     std::vector<std::pair<const Method *, Block *>> &ans)
//     {
//         Block *cur = nullptr;
//         if (idx >= frame.size())
//         {
//             frame.push_back({method, cur});
//             notVisited.erase({method, cur});
//         }
//         else
//         {
//             cur = frame[idx].second;
//             notVisited.erase({method, cur});
//             if (idx != frame.size() - 1 && !children.count(cur))
//             {
//                 for (int i = 0; i < frame.size() - idx - 1; ++i)
//                     frame.pop_back();
//             }
//         }

//         while (!seqs.empty())
//         {
//             auto iter = min_element(seqs.begin(), seqs.end(), [](std::pair<Block *, int> &&l, std::pair<Block *, int> &&r) -> bool
//                                     { return l.second < r.second; });
//             cur = iter->first;
//             notVisited.erase({method, cur});
//             seqs.erase(iter);
//             if (children.count(cur))
//                 children[cur]->match(frame, idx + 1, notVisited, ans);
//         }

//         if (idx == frame.size() - 1)
//             frame.pop_back();
//     }

// public:
//     JitMatchTree(const Method *m, JitMatchTree *f) : method(m), father(f) {}
//     ~JitMatchTree()
//     {
//         for (auto &&child : children)
//             delete child.second;
//     }

//     bool insert(std::vector<std::pair<const Method *, Block *>> &execs, int seq, int idx)
//     {
//         if (idx >= execs.size())
//             return true;
//         const Method *m = execs[idx].first;
//         Block *b = execs[idx].second;
//         if (m != method)
//             return false;
//         if (idx < execs.size() - 1)
//         {
//             if (!children.count(b))
//                 children[b] = new JitMatchTree(execs[idx + 1].first, this);
//             if (!children[b]->insert(execs, seq, idx + 1))
//                 return false;
//         }
//         if (!seqs.count(b))
//             seqs[b] = seq;
//         return true;
//     }

//     void match(std::vector<std::pair<const Method *, Block *>> &frame, int idx,
//                std::set<std::pair<const Method *, Block *>> &notVisited,
//                std::vector<std::pair<const Method *, Block *>> &ans)
//     {
//         notVisited.erase({method, nullptr});
//         if (!method || !method->is_jportal())
//             return skip_match(frame, idx, notVisited, ans);

//         BlockGraph *bg = method->get_bg();
//         Block *cur = nullptr;
//         if (idx < frame.size() && frame[idx].first != method)
//             return_frame(frame, frame.size() - idx, ans);
//         if (idx >= frame.size())
//         {
//             cur = bg->block(0);
//             frame.push_back({method, cur});
//             ans.push_back({method, cur});
//             notVisited.erase({method, cur});
//         }
//         else
//         {
//             cur = frame[idx].second;
//             if (!cur)
//                 cur = bg->block(0);
//             notVisited.erase({method, cur});
//             if (idx < frame.size() - 1 && !children.count(cur))
//                 return_frame(frame, frame.size() - idx - 1, ans);
//         }

//         while (notVisited.size())
//         {
//             if (children.count(cur))
//                 children[cur]->match(frame, idx + 1, notVisited, ans);
//             if (!notVisited.size())
//                 break;
//             if (!match_next(cur, ans))
//             {
//                 frame.pop_back();
//                 return;
//             }
//             else
//             {
//                 cur = ans.back().second;
//                 frame[idx] = ans.back();
//                 notVisited.erase({method, cur});
//             }
//         }
//     }

//     static void return_method(const Method *method, Block *cur,
//                               std::vector<std::pair<const Method *, Block *>> &ans)
//     {
//         if (!method || !method->is_jportal() || !cur)
//             return;

//         std::vector<std::pair<int, Block *>> vv;
//         std::unordered_set<Block *> ss;
//         std::queue<std::pair<int, Block *>> q;
//         std::vector<std::pair<int, int>> find_next;
//         int find_end = -1;
//         q.push({0, cur});
//         ss.insert(cur);
//         while (!q.empty())
//         {
//             Block *blc = q.front().second;
//             int idx = vv.size();
//             vv.push_back({q.front().first, blc});
//             q.pop();
//             if (blc->get_succs_size() == 0)
//                 break;
//             for (auto iter = blc->get_succs_begin(); iter != blc->get_succs_end(); ++iter)
//             {
//                 if (ss.count(*iter))
//                     continue;
//                 ss.insert(*iter);
//                 q.push({idx, *iter});
//             }
//         }
//         std::list<std::pair<const Method *, Block *>> blocks;
//         int idx = vv.size() - 1;
//         while (idx > 0)
//         {
//             blocks.push_front({method, vv[idx].second});
//             idx = vv[idx].first;
//         }
//         ans.insert(ans.end(), blocks.begin(), blocks.end());
//         return;
//     }

//     static void return_frame(std::vector<std::pair<const Method *, Block *>> &frame, int count,
//                              std::vector<std::pair<const Method *, Block *>> &blocks)
//     {
//         assert(count <= frame.size());
//         for (int i = 0; i < count; ++i)
//         {
//             return_method(frame.back().first, frame.back().second, blocks);
//             frame.pop_back();
//         }
//     }
// };

// static bool output_bytecode(FILE *fp, const uint8_t *codes, uint64_t size)
// {
//     /* output bytecode */
//     for (int i = 0; i < size; i++)
//         fprintf(fp, "%hhu\n", *(codes + i));
//     return true;
// }

// static void output_jitcode(FILE *fp, std::vector<std::pair<const Method *, Block *>> &blocks)
// {
//     for (auto block : blocks)
//     {
//         const Method *method = block.first;
//         Block *blc = block.second;
//         assert(method && method->is_jportal() && blc);
//         const uint8_t *codes = method->get_bg()->bctcode();
//         for (int i = blc->get_bct_codebegin(); i < blc->get_bct_codeend(); ++i)
//             fprintf(fp, "%hhu\n", *(codes + i));
//     }
// }

// static bool handle_jitcode(ExecInfo *exec, const PCStackInfo **pcs, int size,
//                            std::vector<std::pair<const Method *, Block *>> &ans)
// {
//     const JitSection *section = exec->section;
//     std::set<const PCStackInfo *> pc_execs;
//     std::set<std::pair<const Method *, Block *>> block_execs;
//     bool notRetry = true;
//     JitMatchTree *tree = new JitMatchTree(section->mainm(), nullptr);
//     auto call_match = [&exec, &tree, &block_execs, &pc_execs, &ans, section](bool newtree) -> void
//     {
//         tree->match(exec->prev_frame, 0, block_execs, ans);
//         delete tree;
//         tree = newtree ? new JitMatchTree(section->mainm(), nullptr) : nullptr;
//         block_execs.clear();
//         pc_execs.clear();
//     };
//     for (int i = 0; i < size; ++i)
//     {
//         const PCStackInfo *pc = pcs[i];
//         if (pc_execs.count(pc))
//             call_match(true);
//         std::vector<std::pair<const Method *, Block *>> frame;
//         for (int j = pc->numstackframes - 1; j >= 0; --j)
//         {
//             int mi = pc->methods[j];
//             int bci = pc->bcis[j];
//             const Method *method = section->method(mi);
//             Block *block = (method && method->is_jportal()) ? method->get_bg()->block(bci) : (Block *)(uint64_t)bci;
//             frame.push_back({method, block});
//             block_execs.insert({method, block});
//         }
//         if (exec->prev_frame.empty())
//         {
//             for (auto blc : frame)
//                 if (blc.first && blc.first->is_jportal() && blc.second)
//                     ans.push_back(blc);
//             exec->prev_frame = frame;
//             block_execs.clear();
//             notRetry = false;
//         }
//         else if (!tree->insert(frame, i, 0))
//         {
//             call_match(true);
//             tree->insert(frame, i, 0);
//         }
//         pc_execs.insert(pc);
//     }
//     call_match(false);
//     return notRetry;
// }

// static void return_exec(std::stack<ExecInfo *> &exec_st, const JitSection *section,
//                         std::vector<std::pair<const Method *, Block *>> &ans)
// {
//     while (!exec_st.empty() && exec_st.top()->section != section)
//     {
//         JitMatchTree::return_frame(exec_st.top()->prev_frame, exec_st.top()->prev_frame.size(), ans);
//         delete exec_st.top();
//         exec_st.pop();
//     }
//     if (exec_st.empty())
//     {
//         exec_st.push(new ExecInfo(section));
//     }
// }

// static void output_trace(TraceData *trace, uint64_t start, uint64_t end, FILE *fp)
// {
//     // TraceDataAccess access(*trace, start, end);
//     // JVMRuntime::Codelet codelet, prev_codelet = JVMRuntime::_illegal;
//     // uint64_t loc;
//     // std::stack<ExecInfo *> exec_st;
//     // while (access.next_trace(codelet, loc))
//     // {
//     //     switch (codelet)
//     //     {
//     //     default:
//     //     {
//     //         fprintf(stderr, "output_trace: unknown codelet(%d)\n", codelet);
//     //         exec_st = std::stack<ExecInfo *>();
//     //         break;
//     //     }
//     //     case JVMRuntime::_method_entry_point:
//     //     {
//     //         if (exec_st.empty() || exec_st.top()->section)
//     //             exec_st.push(new ExecInfo(nullptr));
//     //         break;
//     //     }
//     //     case JVMRuntime::_throw_ArrayIndexOutOfBoundsException:
//     //     case JVMRuntime::_throw_ArrayStoreException:
//     //     case JVMRuntime::_throw_ArithmeticException:
//     //     case JVMRuntime::_throw_ClassCastException:
//     //     case JVMRuntime::_throw_NullPointerException:
//     //     case JVMRuntime::_throw_StackOverflowError:
//     //     {
//     //         break;
//     //     }
//     //     case JVMRuntime::_rethrow_exception:
//     //     {
//     //         break;
//     //     }
//     //     case JVMRuntime::_deopt:
//     //     case JVMRuntime::_deopt_reexecute_return:
//     //     {
//     //         if (!exec_st.empty() && exec_st.top()->section)
//     //         {
//     //             delete exec_st.top();
//     //             exec_st.pop();
//     //         }
//     //         if (exec_st.empty() || exec_st.top()->section)
//     //             exec_st.push(new ExecInfo(nullptr));
//     //         break;
//     //     }
//     //     case JVMRuntime::_throw_exception:
//     //     {
//     //         /* exception handling or throw */
//     //         break;
//     //     }
//     //     case JVMRuntime::_remove_activation:
//     //     case JVMRuntime::_remove_activation_preserving_args:
//     //     {
//     //         /* after throw exception or deoptimize */
//     //         break;
//     //     }
//     //     case JVMRuntime::_invoke_return:
//     //     case JVMRuntime::_invokedynamic_return:
//     //     case JVMRuntime::_invokeinterface_return:
//     //     {
//     //         break;
//     //     }
//     //     case JVMRuntime::_bytecode:
//     //     {
//     //         const uint8_t *codes;
//     //         uint64_t size;
//     //         assert(trace->get_inter(loc, codes, size) && codes);
//     //         std::vector<std::pair<const Method *, Block *>> blocks;
//     //         return_exec(exec_st, nullptr, blocks);
//     //         output_jitcode(fp, blocks);
//     //         output_bytecode(fp, codes, size);
//     //         break;
//     //     }
//     //     case JVMRuntime::_jitcode_entry:
//     //     case JVMRuntime::_jitcode_osr_entry:
//     //     case JVMRuntime::_jitcode:
//     //     {
//     //         const JitSection *section = nullptr;
//     //         const PCStackInfo **pcs = nullptr;
//     //         uint64_t size;
//     //         assert(trace->get_jit(loc, pcs, size, section) && pcs && section);
//     //         std::vector<std::pair<const Method *, Block *>> blocks;
//     //         if (codelet == JVMRuntime::_jitcode_entry)
//     //         {
//     //             ExecInfo *exec = new ExecInfo(section);
//     //             const Method *method = section->mainm();
//     //             Block *block = method->get_bg()->block(0);
//     //             blocks.push_back({method, block});
//     //             exec->prev_frame.push_back({method, block});
//     //             exec_st.push(exec);
//     //         }
//     //         else if (codelet == JVMRuntime::_jitcode_osr_entry)
//     //         {
//     //             ExecInfo *exec = new ExecInfo(section);
//     //             exec_st.push(exec);
//     //         }
//     //         else
//     //         {
//     //             return_exec(exec_st, section, blocks);
//     //         }
//     //         handle_jitcode(exec_st.top(), pcs, size, blocks);
//     //         output_jitcode(fp, blocks);
//     //         break;
//     //     }
//     //     }
//     // }
//     // while (!exec_st.empty())
//     // {
//     //     std::vector<std::pair<const Method *, Block *>> blocks;
//     //     JitMatchTree::return_frame(exec_st.top()->prev_frame, exec_st.top()->prev_frame.size(), blocks);
//     //     output_jitcode(fp, blocks);
//     //     delete exec_st.top();
//     //     exec_st.pop();
//     // }
// }

// void output_decode(const std::list<DecodeData *> &traces)
// {
//     std::map<uint32_t, std::vector<std::pair<ThreadSplit, TraceData *>>> threads_data;
//     for (auto &&trace : traces)
//         for (auto &&threads : trace->get_thread_map())
//             for (auto &&thread : threads.second)
//                 if (thread.end_addr > thread.start_addr)
//                     threads_data[threads.first].push_back({thread, trace});

//     for (auto iter = threads_data.begin(); iter != threads_data.end(); ++iter)
//         sort(iter->second.begin(), iter->second.end(),
//              [](const std::pair<ThreadSplit, TraceData *> &x,
//                 const std::pair<ThreadSplit, TraceData *> &y) -> bool
//              { return x.first.start_time < y.first.start_time || x.first.start_time == y.first.start_time && x.first.end_time < y.first.end_time; });

//     for (auto iter1 = threads_data.begin(); iter1 != threads_data.end(); ++iter1)
//     {
//         char name[32];
//         sprintf(name, "thrd%ld", iter1->first);
//         FILE *fp = fopen(name, "w");
//         if (!fp)
//         {
//             fprintf(stderr, "Decode output: open decode file(%s) error\n", name);
//             continue;
//         }
//         for (auto iter2 = iter1->second.begin(); iter2 != iter1->second.end(); ++iter2)
//             output_trace(iter2->second, iter2->first.start_addr, iter2->first.end_addr, fp);
//         fclose(fp);
//     }
// }

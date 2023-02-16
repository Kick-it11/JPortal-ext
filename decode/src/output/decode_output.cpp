#include "decoder/decode_data.hpp"
#include "java/block.hpp"
#include "output/decode_output.hpp"
#include "output/output_frame.hpp"
#include "runtime/jvm_runtime.hpp"

#include <cassert>
#include <iostream>
#include <fstream>
#include <vector>

using std::vector;

DecodeOutput::DecodeOutput(const std::vector<DecodeData *> &data)
{
    _splits = DecodeData::sort_all_by_time(data);
}

void DecodeOutput::clear_frames(std::vector<OutputFrame *> frames)
{
    while (!frames.empty())
    {
        delete frames.back();
        frames.pop_back();
    }
}
void DecodeOutput::output_cfg(const std::string prefix)
{
    for (auto &&thread : _splits)
    {
        std::ofstream file(prefix + "-" + "thrd" + std::to_string(thread.first));
        DecodeDataAccess access(thread.second);
        DecodeData::DecodeDataType type;
        uint64_t loc;

        std::vector<OutputFrame *> frames;
        while (access.next_trace(type, loc))
        {
            if (type == DecodeData::_method)
            {
                int method_id;
                const Method *method;
                if (!access.get_method(loc, method_id))
                {
                    std::cerr << "DecodeOutput error: Fail to get method "
                              << access.id() << " " << access.pos() << std::endl;
                    exit(1);
                }
                if (!(method = JVMRuntime::method_by_id(method_id)))
                {
                    std::cerr << "DecodeOutput error: Fail to get method by id "
                              << access.id() << " " << access.pos() << std::endl;
                    exit(1);
                }
                int bci1, bci2;
                if (!access.current_trace(type) || type != DecodeData::_bci)
                {
                    /* method entry */
                    frames.push_back(new InterFrame(method, 0, false));

                    /* TODO OUTPUT*/
                }
                else if (!access.next_trace(type, loc) || !access.get_bci(loc, bci1))
                {
                    std::cerr << "DecodeOutput error: Fail to get bci "
                              << access.id() << " " << access.pos() << std::endl;
                    exit(1);
                }
                else if (!access.current_trace(type) || type != DecodeData::_bci)
                {
                    /* method & bci : invoke return */
                    while (!frames.empty() && frames.back()->is_jit_frame())
                    {
                        ((JitFrame*)frames.back())->jit_return();
                        delete frames.back();
                        frames.pop_back();
                    }
                    if (!frames.empty() && ((InterFrame*)frames.back())->method() != method)
                    {
                        std::cerr << "DecodeOutput error: False method invoke return site "
                              << access.id() << " " << access.pos() << std::endl;
                    }
                    if (frames.empty())
                    {

                    }
                }
                else if (access.next_trace(type, loc) || !access.get_bci(loc, bci2))
                {
                    std::cerr << "DecodeOutput error: Fail to get bci "
                              << access.id() << " " << access.pos() << std::endl;
                    exit(1);
                }
                else
                {
                    /* method & bci1 & bci2 : exception */
                    if (frames.empty())
                    {
                        frames.push_back(new InterFrame(method, bci2, false));
                    }
                    else if (frames.back()->is_inter_frame())
                    {
                        if (((InterFrame *)frames.back())->)
                        {

                        }
                        /* to do, forward to bci */
                        ((InterFrame *)frames.back())->forward(bci1);
                        ((InterFrame *)frames.back())->change(bci2);
                    }
                    else
                    {
                        /* deoptimize */
                        delete frames.back();
                        frames.pop_back();
                        frames.push_back(new InterFrame(method, bci1, false));
                    }
                }

            }
            else if (type == DecodeData::_bci)
            {
                /* bci should always follow a method or bci */
                std::cerr << "DecodeOutput error: Unexpected bci "
                          << access.id() << " " << access.pos() << std::endl;
                continue;
            }
            switch (type)
            {
            case DecodeData::_taken:
                
                break;
            case DecodeData::_not_taken:
                
                break;
            case DecodeData::_switch_case:
            {
                int index;
                if (!access.get_switch_case_index(loc, index))
                {
                    std::cerr << "DecodeOutput error: Fail to get switch case index "
                              << access.id() << " " << access.pos() << std::endl;
                    exit(1);
                }
                
                break;
            }
            case DecodeData::_switch_default:
                break;
            case DecodeData::_jit_entry:
            case DecodeData::_jit_osr_entry:
            case DecodeData::_jit_code:
            {
                int section_id;
                std::vector<int> pcs;
                uint64_t size;
                if (!access.get_jit_code(loc, section_id, pcs))
                {
                    std::cerr << "DecodeOutput error: Fail to get jit code "
                              << access.id() << " " << access.pos() << std::endl;
                    exit(1);
                }
                break;
            }
            case DecodeData::_jit_return:
                break;
            case DecodeData::_data_loss:
                break;
            case DecodeData::_decode_error:
                break;
            default:
                std::cerr << "DecodeOutput error: unknown type"
                          << access.id() << " " << access.pos() << std::endl;
                exit(1);
            }
        }
    }
}

void DecodeOutput::output_func(const std::string prefix)
{
    for (auto &&thread : _splits)
    {
        std::ofstream file(prefix + "-" + "thrd" + std::to_string(thread.first));
        DecodeDataAccess access(thread.second);
        DecodeData::DecodeDataType type;
        uint64_t loc;
        while (access.next_trace(type, loc))
        {
            switch (type)
            {
            case DecodeData::_method:
            {
                int method_id;
                if (!access.get_method(loc, method_id))
                {
                    std::cerr << "DecodeOutput error: Fail to get method entry "
                              << access.id() << " " << access.pos() << std::endl;
                    exit(1);
                }
                file << "i:" << method_id << std::endl;
                break;
            }
            case DecodeData::_method_exit:
            {
                int method_id;
                if (!access.get_method(loc, method_id))
                {
                    std::cerr << "DecodeOutput error: Fail to get method entry "
                              << access.id() << " " << access.pos() << std::endl;
                    exit(1);
                }
                file << "i:" << method_id << std::endl;
                break;
            }
            case DecodeData::_data_loss:
                break;
            case DecodeData::_decode_error:
                break;
            default:
                std::cerr << "DecodeOutput error: unknown type"
                          << access.id() << " " << access.pos() << std::endl;
                exit(1);
            }
        }
    }
}

void DecodeOutput::print()
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
                case DecodeData::_method:
                {
                    int method_id;
                    if (!access.get_method(loc, method_id))
                    {
                        std::cerr << "DecodeOutput error: Fail to get method entry" << std::endl;
                        exit(1);
                    }
                    std::cout << "    Method Entry: " << method_id << std::endl;
                    break;
                }
                case DecodeData::_method_exit:
                {
                    int method_id;
                    if (!access.get_method(loc, method_id))
                    {
                        std::cerr << "DecodeOutput error: Fail to get method entry" << std::endl;
                        exit(1);
                    }
                    std::cout << "    Method Exit: " << method_id << std::endl;
                    break;
                }
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
                        std::cerr << "DecodeOutput error: Fail to get method entry" << std::endl;
                        exit(1);
                    }
                    std::cout << "    Switch Case Index: " << index << std::endl;
                }
                case DecodeData::_switch_default:
                    std::cout << "    Switch Default" << std::endl;
                    break;
                case DecodeData::_bci:
                {
                    int bci;
                    if (!access.get_bci(loc, bci))
                    {
                        std::cerr << "DecodeOutput error: Fail to get exception" << std::endl;
                        exit(1);
                    }
                    std::cout << "    Exception Handling: " << bci << std::endl;
                    break;
                }
                case DecodeData::_jit_entry:
                case DecodeData::_jit_osr_entry:
                case DecodeData::_jit_code:
                {
                    int section_id;
                    std::vector<int> pcs;
                    if (!access.get_jit_code(loc, section_id, pcs))
                    {
                        std::cerr << "DecodeOutput error: Fail to get jit code" << std::endl;
                        exit(1);
                    }
                    if (type == DecodeData::_jit_entry)
                    {
                        std::cout << "  Jit code entry: " << std::endl;
                    }
                    if (type == DecodeData::_jit_osr_entry)
                    {
                        std::cout << "Jit code osr entry: " << std::endl;
                    }
                    if (type == DecodeData::_jit_code)
                    {
                        std::cout << "Jit code: " << std::endl;
                    }
                    break;
                }
                case DecodeData::_jit_return:
                    std::cout << "    Jit return" << std::endl;
                    break;
                case DecodeData::_deoptimization:
                    std::cout << "    Deoptimization" << std::endl;
                    break;
                case DecodeData::_data_loss:
                    std::cout << "    Data loss" << std::endl;
                    break;
                case DecodeData::_decode_error:
                    std::cout << "    Decode error" << std::endl;
                    break;
                default:
                    std::cerr << "DecodeOutput error: unknown type" << std::endl;
                    exit(1);
                }
            }
        }
    }
}

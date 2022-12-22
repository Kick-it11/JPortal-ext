#include "decoder/decode_data.hpp"
#include "java/analyser.hpp"
#include "java/block.hpp"
#include "output/decode_output.hpp"
#include "output/output_frame.hpp"
#include "runtime/jvm_runtime.hpp"

#include <cassert>
#include <iostream>
#include <fstream>

DecodeOutput::DecodeOutput(const std::vector<DecodeData *> &data, Analyser *analyser)
{
    _analyser = analyser;
    _splits = DecodeData::sort_all_by_time(data);
}

/* Todo: output cfg info: methods and bytecodes */
void DecodeOutput::output_cfg(const std::string prefix)
{
    for (auto &&thread : _splits)
    {
        std::ofstream file(prefix + "-" + "thrd" + std::to_string(thread.first));
        for (auto &&split : thread.second)
        {
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
                        std::cerr << "DecodeOutput error: Fail to get method entry "
                                  << access.id() << " " << access.pos() << std::endl;
                        exit(1);
                    }
                    break;
                }
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
                case DecodeData::_bci:
                {
                    int bci;
                    if (!access.get_bci(loc, bci))
                    {
                        std::cerr << "DecodeOutput error: Fail to get bci "
                                  << access.id() << " " << access.pos() << std::endl;
                        exit(1);
                    }
                    break;
                }
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
}

void DecodeOutput::output_func(const std::string prefix)
{
    for (auto &&thread : _splits)
    {
        std::ofstream file(prefix + "-" + "thrd" + std::to_string(thread.first));
        for (auto &&split : thread.second)
        {
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

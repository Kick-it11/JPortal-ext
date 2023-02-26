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
void DecodeOutput::output(const std::string prefix)
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

                default:
                    std::cerr << "DecodeOutput error: unknown type" << std::endl;
                    exit(1);
                }
            }
        }
    }
}

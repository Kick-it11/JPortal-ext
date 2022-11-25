#include "decoder/decode_data.hpp"
#include "java/analyser.hpp"
#include "java/block.hpp"
#include "output/decode_output.hpp"
#include "output/output_frame.hpp"

#include <cassert>
#include <iostream>
#include <fstream>

DecodeOutput::DecodeOutput(const std::vector<DecodeData *> &data)
{
    _splits = DecodeData::sort_all_by_time(data);
}

void DecodeOutput::to_method_entry(DecodeDataAccess &access, std::ofstream &file, const Method *method)
{
    OutputFrame *frame = new InterFrame(method, 0, false);
    _frames.push_back(frame);
}

/* For normal return, or exception, or early return
 * It will all have a method exit execution
 * For native codes, it might have a duplicated
 * method exit in case of exception happening,
 * But We do not support naitive code trace
 */
void DecodeOutput::to_method_exit(DecodeDataAccess &access, std::ofstream &file)
{
    if (_frames.empty())
    {
        return;
    }
    if (!_frames.back()->is_inter_frame())
    {
        std::cerr << "DecodeOutput error: Fail to find inter frame "
                  << access.id() << " " << access.pos() << std::endl;
        clear_frame();
        return;
    }

    std::vector<uint8_t> codes;
    if (!((InterFrame *)_frames.back())->method_exit(codes))
    {
        std::cerr << "DecodeOutput error: Fail to reach method exit "
                  << access.id() << " " << access.pos() << std::endl;
        clear_frame();
        return;
    }
    output_codes(file, codes);

    delete _frames.back();
    _frames.pop_back();
}

void DecodeOutput::to_branch_taken(DecodeDataAccess &access, std::ofstream &file)
{
    if (_frames.empty())
    {
        return;
    }
    if (!_frames.back()->is_inter_frame())
    {
        std::cerr << "DecodeOutput error: Fail to find inter frame "
                  << access.id() << " " << access.pos() << std::endl;
        clear_frame();
        return;
    }

    std::vector<uint8_t> codes;
    if (!((InterFrame *)_frames.back())->branch_taken(codes))
    {
        std::cerr << "DecodeOutput error: Fail to take a branch "
                  << access.id() << " " << access.pos() << std::endl;
        clear_frame();
        return;
    }
    output_codes(file, codes);
}

void DecodeOutput::to_branch_not_taken(DecodeDataAccess &access, std::ofstream &file)
{
    if (_frames.empty())
    {
        return;
    }
    if (!_frames.back()->is_inter_frame())
    {
        std::cerr << "DecodeOutput error: Fail to find inter frame "
                  << access.id() << " " << access.pos() << std::endl;
        clear_frame();
        return;
    }

    std::vector<uint8_t> codes;
    if (!((InterFrame *)_frames.back())->branch_not_taken(codes))
    {
        std::cerr << "DecodeOutput error: Fail not to take a branch "
                  << access.id() << " " << access.pos() << std::endl;
        clear_frame();
        return;
    }
    output_codes(file, codes);
}

void DecodeOutput::to_switch_case(DecodeDataAccess& access, std::ofstream &file, int index)
{
    if (_frames.empty())
    {
        return;
    }
    if (!_frames.back()->is_inter_frame())
    {
        std::cerr << "DecodeOutput error: Fail to find inter frame "
                  << access.id() << " " << access.pos() << std::endl;
        clear_frame();
        return;
    }

    std::vector<uint8_t> codes;
    if (!((InterFrame *)_frames.back())->switch_case(codes, index))
    {
        std::cerr << "DecodeOutput error: Fail to find switch case "
                  << access.id() << " " << access.pos() << std::endl;
        clear_frame();
        return;
    }
    output_codes(file, codes);
}

void DecodeOutput::to_switch_default(DecodeDataAccess &access, std::ofstream &file)
{
    if (_frames.empty())
    {
        return;
    }
    if (!_frames.back()->is_inter_frame())
    {
        std::cerr << "DecodeOutput error: Fail to find inter frame "
                  << access.id() << " " << access.pos() << std::endl;
        clear_frame();
        return;
    }

    std::vector<uint8_t> codes;
    if (!((InterFrame *)_frames.back())->switch_default(codes))
    {
        std::cerr << "DecodeOutput error: Fail to find switch default "
                  << access.id() << " " << access.pos() << std::endl;
        clear_frame();
        return;
    }
    output_codes(file, codes);
}

void DecodeOutput::to_invoke_site(DecodeDataAccess &access, std::ofstream &file)
{
    if (_frames.empty())
    {
        return;
    }
    if (!_frames.back()->is_inter_frame())
    {
        std::cerr << "DecodeOutput error: Fail to find inter frame "
                  << access.id() << " " << access.pos() << std::endl;
        clear_frame();
        return;
    }

    std::vector<uint8_t> codes;
    if (!((InterFrame *)_frames.back())->invoke_site(codes))
    {
        std::cerr << "DecodeOutput error: Fail to reach invoke site "
                  << access.id() << " " << access.pos() << std::endl;
        clear_frame();
        return;
    }
    output_codes(file, codes);
}

void DecodeOutput::to_exception_handling(DecodeDataAccess &access, std::ofstream &file,
                                         const Method *method, int bci, int handler_bci)
{
    if (_frames.empty())
    {
        _frames.push_back(new InterFrame(method, bci, true));
    }
    if (!_frames.back()->is_inter_frame())
    {
        std::cerr << "DecodeOutput error: Fail to find inter frame "
                  << access.id() << " " << access.pos() << std::endl;
        clear_frame();
        _frames.push_back(new InterFrame(method, bci, true));
    }

    if (((InterFrame *)(_frames.back()))->method() != method)
    {
        std::cerr << "DecodeOutput error: Fail to find inter frame "
                  << access.id() << " " << access.pos() << std::endl;
        clear_frame();
        _frames.push_back(new InterFrame(method, bci, true));
    }

    std::vector<uint8_t> codes;
    if (!((InterFrame *)_frames.back())->exception_handling(codes, bci, handler_bci))
    {
        std::cerr << "DecodeOutput error: Fail to reach throw site "
                  << access.id() << " " << access.pos() << std::endl;
        clear_frame();
        _frames.push_back(new InterFrame(method, handler_bci, false));
        return;
    }
    output_codes(file, codes);
}

void DecodeOutput::to_deoptimization(DecodeDataAccess &access, std::ofstream &file,
                                     const Method *method, int bci,
                                     bool use_next_bci, bool is_bottom_frame)
{
    if (is_bottom_frame)
    {
        if (_frames.empty() || !_frames.back()->is_jit_frame() ||
            ((JitFrame *)(_frames.back()))->section()->mainm() != method)
        {
            std::cerr << "DecodeOutput error: Fail to find deopt frame "
                      << access.id() << " " << access.pos() << std::endl;
            clear_frame();
        } else {
            delete _frames.back();
            _frames.pop_back();
        }
    }
    OutputFrame *frame = new InterFrame(method, bci, use_next_bci);

    _frames.push_back(frame);
}

void DecodeOutput::to_jit_code(DecodeDataAccess &access, std::ofstream &file,
                               const JitSection *section, const PCStackInfo **info,
                               uint64_t size, bool entry, bool osr_entry)
{
    if (osr_entry)
    {
        if (_frames.empty() || !_frames.back()->is_inter_frame()
            || ((InterFrame *)(_frames.back()))->method() != section->mainm())
        {
            std::cerr << "DecodeOutput error: Fail for osr entry "
                      << access.id() << " " << access.pos() << std::endl;
            clear_frame();
        } else {
            delete _frames.back();
            _frames.pop_back();
        }
    }
    if (entry || osr_entry)
    {
        _frames.push_back(new JitFrame(section));
    }
    if (_frames.empty() || ((JitFrame *)(_frames.back()))->section() != section)
    {
        std::cerr << "DecodeOutput error: Fail to find jit frame "
                  << access.id() << " " << access.pos() << std::endl;
        clear_frame();
        _frames.push_back(new JitFrame(section));
    }

    std::vector<uint8_t> codes;
    if (!((JitFrame *)(_frames.back()))->jit_code(codes, section, info, size, entry))
    {
        std::cerr << "DecodeOutput error: Fail to reach jit codes "
                  << access.id() << " " << access.pos() << std::endl;
        clear_frame();
        _frames.push_back(new JitFrame(section));
    }
    output_codes(file, codes);
}

void DecodeOutput::to_jit_return(DecodeDataAccess &access, std::ofstream &file)
{
    if (_frames.empty())
    {
        return;
    }

    if (!_frames.back()->is_jit_frame())
    {
        std::cerr << "DecodeOutput error: Fail to find jit frame "
                  << access.id() << " " << access.pos() << std::endl;
        clear_frame();
        return;
    }

    std::vector<uint8_t> codes;
    if (!((JitFrame *)_frames.back())->jit_return(codes))
    {
        std::cerr << "DecodeOutput error: Fail to reach jit return "
                  << access.id() << " " << access.pos() << std::endl;
        clear_frame();
    }
    output_codes(file, codes);

    delete _frames.back();
    _frames.pop_back();
}

void DecodeOutput::to_data_loss(DecodeDataAccess &access, std::ofstream &file)
{
    std::cerr << "There is a data loss around here "
              << access.id() << " " << access.pos() << std::endl;
}

void DecodeOutput::to_decode_error(DecodeDataAccess &access, std::ofstream &file)
{
    std::cerr << "There is a decode error around here "
              << access.id() << " " << access.pos() << std::endl;
}



void DecodeOutput::clear_frame()
{
    while (!_frames.empty())
    {
        delete _frames.back();
        _frames.pop_back();
    }
}

void DecodeOutput::output_codes(std::ofstream &file, std::vector<uint8_t> &codes)
{
    for (auto code : codes)
    {
        file << (int)code << std::endl;
    }
}

void DecodeOutput::output(const std::string prefix)
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
                case DecodeData::_method_entry:
                {
                    const Method *method;
                    if (!access.get_method_entry(loc, method))
                    {
                        std::cerr << "DecodeOutput error: Fail to get method entry "
                                  << access.id() << " " << access.pos() << std::endl;
                        exit(1);
                    }
                    to_method_entry(access, file, method);
                    break;
                }
                case DecodeData::_method_exit:
                    to_method_exit(access, file);
                    break;
                case DecodeData::_taken:
                    to_branch_taken(access, file);
                    break;
                case DecodeData::_not_taken:
                    to_branch_not_taken(access, file);
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
                    to_switch_case(access, file, index);
                    break;
                }
                case DecodeData::_switch_default:
                    to_switch_default(access, file);
                    break;
                case DecodeData::_invoke_site:
                    to_invoke_site(access, file);
                    break;
                case DecodeData::_exception:
                {
                    const Method *method;
                    int current_bci, handler_bci;
                    if (!access.get_exception_handling(loc, method, current_bci, handler_bci))
                    {
                        std::cerr << "DecodeOutput error: Fail to get exception "
                                  << access.id() << " " << access.pos() << std::endl;
                        exit(1);
                    }
                    to_exception_handling(access, file, method, current_bci, handler_bci);
                    break;
                }
                case DecodeData::_deoptimization:
                {
                    const Method *method;
                    int bci;
                    uint8_t use_next_bci, is_bottom_frame;
                    if (!access.get_deoptimization(loc, method, bci, use_next_bci, is_bottom_frame))
                    {
                        std::cerr << "DecodeOutput error: Fail to get deoptimization"
                                  << access.id() << " " << access.pos() << std::endl;
                        exit(1);
                    }
                    to_deoptimization(access, file, method, bci, use_next_bci, is_bottom_frame);
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
                        std::cerr << "DecodeOutput error: Fail to get jit code "
                                  << access.id() << " " << access.pos() << std::endl;
                        exit(1);
                    }
                    to_jit_code(access, file, section, info, size,
                                type == DecodeData::_jit_entry,
                                type == DecodeData::_jit_osr_entry);
                    break;
                }
                case DecodeData::_jit_return:
                    to_jit_return(access, file);
                    break;
                case DecodeData::_data_loss:
                    to_data_loss(access, file);
                    break;
                case DecodeData::_decode_error:
                    to_decode_error(access, file);
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
                case DecodeData::_method_entry:
                {
                    const Method *method;
                    if (!access.get_method_entry(loc, method))
                    {
                        std::cerr << "DecodeOutput error: Fail to get method entry" << std::endl;
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
                        std::cerr << "DecodeOutput error: Fail to get method entry" << std::endl;
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
                        std::cerr << "DecodeOutput error: Fail to get exception" << std::endl;
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
                    uint8_t use_next_bci, is_bottom_frame;
                    if (!access.get_deoptimization(loc, method, bci, use_next_bci, is_bottom_frame))
                    {
                        std::cerr << "DecodeOutput error: Fail to get deoptimization" << std::endl;
                        exit(1);
                    }
                    std::cout << "    Deoptimization: " << method->get_klass()->get_name()
                              << " " << method->get_name() << " " << bci
                              << " " << use_next_bci << " " << is_bottom_frame << std::endl;
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
                        std::cerr << "DecodeOutput error: Fail to get jit code" << std::endl;
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

#include "decoder/decode_data.hpp"
#include "java/block.hpp"
#include "java/method.hpp"
#include "output/decode_output.hpp"
#include "output/output_frame.hpp"
#include "runtime/jit_section.hpp"
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

void DecodeOutput::clear_frames(std::vector<InterFrame *> &inters,
                                std::vector<JitFrame *> &jits)
{
    while (!inters.empty())
    {
        delete inters.back();
        inters.pop_back();
    }
    while (!jits.empty())
    {
        delete jits.back();
        jits.pop_back();
    }
}

bool DecodeOutput::return_frames(std::ofstream &file,
                                 std::vector<InterFrame *> &inters,
                                 std::vector<JitFrame *> &jits)
{
    while (!inters.empty())
    {
        std::vector<std::pair<int, int>> ans;
        inters.back()->forward(ans);
        if (!Bytecodes::is_return(inters.back()->code()))
        {
            clear_frames(inters, jits);
            return false;
        }
        if (_method_id != inters.back()->method()->id())
        {
            _method_id = inters.back()->method()->id();
            file << _method_id << std::endl;
        }
        for (auto single : ans)
            file << "[" << single.first << "," << single.second << "]" << std::endl;
        delete inters.back();
        inters.pop_back();
    }
    clear_frames(inters, jits);
    return true;
}

void DecodeOutput::find_jit_frame(std::vector<JitFrame *> &jits,
                                  const JitSection *section)
{
    while (!jits.empty() && jits.back()->section() != section)
    {
        delete jits.back();
        jits.pop_back();
    }
    if (jits.empty())
        jits.push_back(new JitFrame(section));
}

bool DecodeOutput::check_pre_event(DecodeDataEvent &event, std::ofstream &file,
                                   std::vector<InterFrame *> &inters,
                                   std::vector<JitFrame *> &jits)
{
    if (!event.pending())
        return false;

    switch (event.type())
    {
    case DecodeData::_method_entry:
    {
        if (inters.empty())
        {
            inters.push_back(new InterFrame(event.method(), 0));
            event.set_unpending();
        }
        return true;
    }
    case DecodeData::_method_point:
    {
        if (inters.empty())
        {
            inters.push_back(new InterFrame(event.method(), 0));
            event.set_unpending();
            return true;
        }
        else if (inters.back()->method() == event.method() && event.bci_or_ind() == inters.back()->bci())
        {
            /* Handle: Ignore: Has a call back target method entry */
            event.set_unpending();
            return true;
        }
        return true;
    }
    case DecodeData::_taken:
    case DecodeData::_not_taken:
    case DecodeData::_switch_case:
    case DecodeData::_switch_default:
    {
        if (inters.empty())
            event.set_unpending();
        return true;
    }
    case DecodeData::_osr:
    {
        std::vector<std::pair<int, int>> ans;
        if (inters.empty())
        {
            event.set_unpending();
        }
        else if (inters.back()->method() == event.method() && inters.back()->forward(event.bci_or_ind(), ans))
        {
            if (!ans.empty())
            {
                if (inters.back()->method()->id() != _method_id)
                {
                    _method_id = inters.back()->method()->id();
                    file << _method_id << std::endl;
                }
                for (auto off : ans)
                    file << "[" << off.first << "," << off.second << "]"
                         << std::endl;
            }
            delete inters.back();
            inters.pop_back();
            event.set_unpending();
        }
        return true;
    }
    case DecodeData::_throw_exception:
    {
        std::vector<std::pair<int, int>> ans;
        if (inters.empty())
        {
            event.set_unpending();
        }
        if (inters.back()->method() == event.method() && inters.back()->forward(event.bci_or_ind(), ans))
        {
            if (!ans.empty())
            {
                if (inters.back()->method()->id() != _method_id)
                {
                    _method_id = inters.back()->method()->id();
                    file << _method_id << std::endl;
                }
                for (auto off : ans)
                    file << "[" << off.first << "," << off.second << "]"
                         << std::endl;
            }
            event.set_unpending();
        }
        return true;
    }
    case DecodeData::_rethrow_exception:
    {
        if (!inters.empty())
        {
            delete inters.back();
            inters.pop_back();
        }
        event.set_unpending();
        return true;
    }
    case DecodeData::_handle_exception:
    {
        if (inters.empty())
        {
            inters.push_back(new InterFrame(event.method(), event.bci_or_ind()));
            event.set_unpending();
        }
        else if (inters.back()->method() == event.method())
        {
            if (!inters.back()->straight(event.bci_or_ind()))
                return false;
            event.set_unpending();
        }
        return true;
    }
    case DecodeData::_ret_code:
    {
        if (inters.empty())
        {
            inters.push_back(new InterFrame(event.method(), event.bci_or_ind()));
            event.set_unpending();
        }
        return true;
    }
    case DecodeData::_deoptimization:
    case DecodeData::_non_invoke_ret:
    {
        /* Always push */
        inters.push_back(new InterFrame(event.method(), event.bci_or_ind()));
        event.set_unpending();
        return true;
    }
    case DecodeData::_pop_frame:
    case DecodeData::_earlyret:
    {
        std::vector<std::pair<int, int>> ans;
        if (inters.empty())
        {
            event.set_unpending();
        }
        else if (inters.back()->method() == event.method() && inters.back()->forward(event.bci_or_ind(), ans))
        {
            if (!ans.empty())
            {
                if (inters.back()->method()->id() != _method_id)
                {
                    _method_id = inters.back()->method()->id();
                    file << _method_id << std::endl;
                }
                for (auto off : ans)
                    file << "[" << off.first << "," << off.second << "]"
                         << std::endl;
            }
            delete inters.back();
            inters.pop_back();
            event.set_unpending();
        }
        return true;
    }
    case DecodeData::_jit_code:
    {
        if (inters.empty())
        {
            if (!output_jit_cfg(event, file, jits))
            {
                event.set_unpending();
                return false;
            }
            event.set_unpending();
            return true;
        }
        return true;
    }
    case DecodeData::_data_loss:
        file << "l" << std::endl;
        event.set_unpending();
        return true;
    case DecodeData::_decode_error:
        file << "e" << std::endl;
        event.set_unpending();
        return true;
    default:
        std::cerr << "DecodeOutput error: Unexpected event " << event.type();
        exit(1);
    }
}

bool DecodeOutput::check_post_event(DecodeDataEvent &event, std::ofstream &file,
                                    std::vector<InterFrame *> &inters,
                                    std::vector<JitFrame *> &jits)
{
    if (!event.pending() || inters.empty())
        return false;

    Bytecodes::Code code = inters.back()->code();
    DecodeData::DecodeDataType type = event.type();
    if (type == DecodeData::_jit_code)
    {
        if (!output_jit_cfg(event, file, jits))
        {
            event.set_unpending();
            return false;
        }
        event.set_unpending();
        return true;
    }

    if (Bytecodes::is_branch(code))
    {
        if (DecodeData::_taken == type)
        {
            if (!inters.back()->taken())
                return false;
            event.set_unpending();
            return true;
        }
        else if (DecodeData::_not_taken == type)
        {
            if (!inters.back()->not_taken())
                return false;
            event.set_unpending();
            return true;
        }
        else if (type == DecodeData::_osr)
        {
            delete inters.back();
            inters.pop_back();
            return true;
        }
        else
        {
            return false;
        }
    }
    else if (Bytecodes::_lookupswitch == code || Bytecodes::_tableswitch == code)
    {
        if (DecodeData::_switch_case == type)
        {
            if (!inters.back()->switch_case(event.bci_or_ind()))
                return false;
            event.set_unpending();
            return true;
        }
        else if (DecodeData::_switch_default == type)
        {
            if (inters.back()->switch_default())
                return false;
            event.set_unpending();
            return true;
        }
        else
        {
            return false;
        }
    }
    else if (Bytecodes::_ret == code)
    {
        if (DecodeData::_ret_code == type)
        {
            if (inters.back()->method() != event.method())
                return false;
            if (!inters.back()->straight(event.bci_or_ind()))
                return false;
            event.set_unpending();
            return true;
        }
        else
        {
            return false;
        }
    }
    else if (Bytecodes::is_invoke(code))
    {
        const Method *callee = nullptr;
        if (DecodeData::_method_point && event.bci_or_ind() == inters.back()->next_bci())
        {
            inters.back()->invoke();
            event.set_unpending();
            return true;
        }
        else if (Bytecodes::_invokestatic == code || Bytecodes::_invokespecial == code && (callee = inters.back()->callee()))
        {
            inters.back()->invoke();
            inters.push_back(new InterFrame(callee, 0));
            return true;
        }
        else if (Bytecodes::_invokevirtual == code || Bytecodes::_invokeinterface == code && (callee = inters.back()->callee()) && callee->is_final())
        {
            inters.back()->invoke();
            inters.push_back(new InterFrame(callee, 0));
            return true;
        }
        else if (DecodeData::_method_entry == type)
        {
            inters.push_back(new InterFrame(event.method(), 0));
            event.set_unpending();
            return true;
        }
        else
        {
            return false;
        }
    }
    else if (Bytecodes::is_return(code))
    {
        delete inters.back();
        inters.pop_back();
        return true;
    }
    else if (Bytecodes::_athrow == code)
    {
        if (DecodeData::_throw_exception != type || event.bci_or_ind() != inters.back()->bci())
        {
            delete inters.back();
            inters.pop_back();
            event.set_unpending();
        }
        else
            return false;
    }
    return false;
}

bool DecodeOutput::output_jit_cfg(DecodeDataEvent &event, std::ofstream &file,
                                  std::vector<JitFrame *> &jits)
{
    if (!event.pending() || event.type() != DecodeData::_jit_code)
        return false;

    std::set<int> execs;
    std::vector<const PCStackInfo *> pcs;
    const JitSection *section = event.section();
    std::vector<std::pair<int, std::pair<int, int>>> cfgs;
    std::vector<std::pair<std::string, int>> methods;
    for (auto i : event.pcs())
    {
        const PCStackInfo *pc = section->get_pc(i);
        if ((!pc || execs.count(i)) && !execs.empty())
        {
            /* indicate a loop */
            find_jit_frame(jits, section);
            jits.back()->jit_code(pcs, true, false, cfgs, methods);
            execs.clear();
            pcs.clear();
            continue;
        }
        else if (pc)
        {
            execs.insert(i);
            pcs.push_back(pc);
            continue;
        }
        switch (-i)
        {
        case DecodeData::_jit_entry:
            jits.push_back(new JitFrame(section));
            jits.back()->entry(true, false, cfgs, methods);
            break;
        case DecodeData::_jit_osr_entry:
            jits.push_back(new JitFrame(section));
            break;
        case DecodeData::_jit_return:
            find_jit_frame(jits, section);
            jits.back()->jit_return(true, false, cfgs, methods);
            delete jits.back();
            jits.pop_back();
            break;
        case DecodeData::_jit_exception:
            find_jit_frame(jits, section);
            jits.clear();
            break;
        case DecodeData::_jit_unwind:
        case DecodeData::_jit_deopt:
        case DecodeData::_jit_deopt_mh:
            find_jit_frame(jits, section);
            jits.pop_back();
            break;
        default:
            std::cerr << "DecodeOutput error: Unknown jit type" << std::endl;
            exit(1);
        }
    }
    if (!execs.empty())
    {
        find_jit_frame(jits, section);
        jits.back()->jit_code(pcs, true, false, cfgs, methods);
    }
    for (auto single : cfgs)
    {
        if (single.first != _method_id)
        {
            _method_id = single.first;
            file << _method_id << std::endl;
        }
        file << "[" << single.second.first << ", " << single.second.second << "]" << std::endl;
    }
    return true;
}

/* DecodeData::DecodeDataType can be used for event
 *   _java_call_begin
 *   _java_call_end
 *   _method_entry
 *   _method_exit
 *   _method_point
 *   _taken
 *   _not_taken
 *   _switch_case
 *   _switch_default
 *   _osr
 *   _throw_exception
 *   _handle_exception
 *   _ret_code
 *   _deoptimization
 *   _non_invoke_ret
 *   _pop_frame
 *   _earlyret
 *   _jit_code
 *   _data_loss
 *   _decode_error
 */

bool DecodeOutput::output_cfg(DecodeDataEvent &event, std::ofstream &file)
{
    std::vector<InterFrame *> inters;
    std::vector<JitFrame *> jits;
    while (event.current_event())
    {
        std::vector<std::pair<int, int>> ans;
        DecodeData::DecodeDataType type = event.type();
        /* process java_call_begin & java_call_end here */
        if (type == DecodeData::_java_call_begin)
        {
            event.set_unpending();
            if (!output_cfg(event, file))
            {
                clear_frames(inters, jits);
                return false;
            }
            continue;
        }
        if (type == DecodeData::_java_call_end)
        {
            event.set_unpending();
            return return_frames(file, inters, jits);
        }

        if (!check_pre_event(event, file, inters, jits))
        {
            clear_frames(inters, jits);
            return false;
        }

        if (!event.pending())
            continue;

        if (!inters.back()->forward(ans))
        {
            clear_frames(inters, jits);
            return false;
        }

        /* output */
        if (!ans.empty())
        {
            if (inters.back()->method()->id() != _method_id)
            {
                _method_id = inters.back()->method()->id();
                file << _method_id << std::endl;
            }
            for (auto off : ans)
                file << "[" << off.first << "," << off.second << "]"
                     << std::endl;
        }

        if (!check_post_event(event, file, inters, jits))
        {
            clear_frames(inters, jits);
            return false;
        }

        if (event.pending() && ans.empty())
        {
            clear_frames(inters, jits);
            return false;
        }
    }
    return return_frames(file, inters, jits);
}

void DecodeOutput::output_cfg(const std::string prefix)
{
    for (auto &&thread : _splits)
    {
        std::ofstream file(prefix + "-" + "thrd" + std::to_string(thread.first));
        DecodeDataEvent event(thread.second);

        /* decode data not processed completely */
        while (event.remaining())
        {
            if (!output_cfg(event, file))
                file << "e" << std::endl;
        }
    }
}

bool DecodeOutput::output_jit_method(DecodeDataEvent &event, std::ofstream &file,
                                     std::vector<JitFrame *> &jits)
{
    if (!event.pending() || event.type() != DecodeData::_jit_code)
        return false;

    std::set<int> execs;
    std::vector<const PCStackInfo *> pcs;
    const JitSection *section = event.section();
    std::vector<std::pair<int, std::pair<int, int>>> cfgs;
    std::vector<std::pair<std::string, int>> methods;
    for (auto i : event.pcs())
    {
        const PCStackInfo *pc = section->get_pc(i);
        if ((!pc || execs.count(i)) && !execs.empty())
        {
            /* indicate a loop */
            find_jit_frame(jits, section);
            jits.back()->jit_code(pcs, false, true, cfgs, methods);
            execs.clear();
            pcs.clear();
            continue;
        }
        else if (pc)
        {
            execs.insert(i);
            pcs.push_back(pc);
            continue;
        }
        switch (-i)
        {
        case DecodeData::_jit_entry:
            jits.push_back(new JitFrame(section));
            jits.back()->entry(false, true, cfgs, methods);
            break;
        case DecodeData::_jit_osr_entry:
            jits.push_back(new JitFrame(section));
            break;
        case DecodeData::_jit_return:
            find_jit_frame(jits, section);
            jits.back()->jit_return(false, true, cfgs, methods);
            delete jits.back();
            jits.pop_back();
            break;
        case DecodeData::_jit_exception:
            find_jit_frame(jits, section);
            jits.clear();
            break;
        case DecodeData::_jit_unwind:
        case DecodeData::_jit_deopt:
        case DecodeData::_jit_deopt_mh:
            find_jit_frame(jits, section);
            jits.pop_back();
            break;
        default:
            std::cerr << "DecodeOutput error: Unknown jit type" << std::endl;
            exit(1);
        }
    }
    if (!execs.empty())
    {
        find_jit_frame(jits, section);
        jits.back()->jit_code(pcs, false, true, cfgs, methods);
    }
    for (auto single : methods)
    {
        file << single.first << ":" << single.second << std::endl;
    }
    return true;
}

void DecodeOutput::output_method(const std::string prefix)
{
    for (auto &&thread : _splits)
    {
        std::ofstream file(prefix + "-" + "thrd" + std::to_string(thread.first));
        DecodeDataEvent event(thread.second);

        std::vector<JitFrame *> jits;
        while (event.current_event())
        {
            switch (event.type())
            {
                case DecodeData::_method_entry:
                    file << "i:" << event.method()->id() << std::endl;
                    break;
                case DecodeData::_method_exit:
                    file << "o:" << event.method()->id() << std::endl;
                    break;
                case DecodeData::_jit_code:
                    if (!output_jit_method(event, file, jits))
                        file << "e" << std::endl;
                    break;
                case DecodeData::_decode_error:
                    file << "e" << std::endl;
                    break;
                case DecodeData::_data_loss:
                    file << "l" << std::endl;
                    break;
                default:
                    std::cerr << "DecodeOutput error: unknown type for Method Trace" << std::endl;
                    exit(1);
            }
            event.set_unpending();
        }
    }
}

void DecodeOutput::output_method_noinline(const std::string prefix)
{
    for (auto &&thread : _splits)
    {
        std::ofstream file(prefix + "-" + "thrd" + std::to_string(thread.first));
        DecodeDataEvent event(thread.second);

        std::vector<JitFrame *> jits;
        while (event.current_event())
        {
            switch (event.type())
            {
                case DecodeData::_method_entry:
                    file << "e:" << event.method()->id() << std::endl;
                    break;
                case DecodeData::_method_exit:
                    file << "x:" << event.method()->id() << std::endl;
                    break;
                default:
                    std::cerr << "DecodeOutput error: unknown type for Method Trace" << std::endl;
                    exit(1);
            }
            event.set_unpending();
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
                case DecodeData::_java_call_begin:
                {
                    std::cout << "Java call begin" << std::endl;
                    break;
                }
                case DecodeData::_java_call_end:
                {
                    std::cout << "Java call end" << std::endl;
                    break;
                }
                case DecodeData::_method_entry:
                {
                    const Method *method = nullptr;
                    if (!access.get_method(loc, method))
                    {
                        std::cerr << "DecodeOutput error: Fail to get method entry at "
                                  << loc << std::endl;
                        break;
                    }
                    std::cout << "Method Entry:" << method->id() << std::endl;
                    break;
                }
                case DecodeData::_method_exit:
                {
                    const Method *method = nullptr;
                    if (!access.get_method(loc, method))
                    {
                        std::cerr << "DecodeOutput error: Fail to get method exit at "
                                  << loc << std::endl;
                        break;
                    }
                    std::cout << "Method Exit:" << method->id() << std::endl;
                    break;
                }
                case DecodeData::_method_point:
                {
                    const Method *method;
                    if (!access.get_method(loc, method))
                    {
                        std::cerr << "DecodeOutput error: Fail to get method point at "
                                  << loc << std::endl;
                        break;
                    }
                    std::cout << "Method Point:" << method->id() << " " << std::endl;
                    break;
                }
                case DecodeData::_taken:
                {
                    std::cout << "Taken" << std::endl;
                    break;
                }
                case DecodeData::_not_taken:
                {
                    std::cout << "Not taken" << std::endl;
                    break;
                }
                case DecodeData::_switch_case:
                {
                    int index;
                    if (!access.get_switch_case_index(loc, index))
                    {
                        std::cerr << "DecodeOutput error: Fail to get switch case index at "
                                  << loc << std::endl;
                        break;
                    }
                    std::cout << "Swirch case: " << index << std::endl;
                    break;
                }
                case DecodeData::_switch_default:
                {
                    std::cout << "Switch default" << std::endl;
                    break;
                }
                case DecodeData::_bci:
                {
                    int bci;
                    if (!access.get_bci(loc, bci))
                    {
                        std::cerr << "DecodeOutput error: Fail to get bci at "
                                  << loc << std::endl;
                        break;
                    }
                    std::cout << "Bci: " << bci << std::endl;
                    break;
                }
                case DecodeData::_osr:
                {
                    std::cout << "OSR" << std::endl;
                    break;
                }
                case DecodeData::_throw_exception:
                {
                    std::cout << "Throw exception" << std::endl;
                    break;
                }
                case DecodeData::_rethrow_exception:
                {
                    std::cout << "Rethrow exception" << std::endl;
                    break;
                }
                case DecodeData::_handle_exception:
                {
                    std::cout << "Handle exception" << std::endl;
                    break;
                }
                case DecodeData::_ret_code:
                {
                    std::cout << "Ret code" << std::endl;
                    break;
                }
                case DecodeData::_deoptimization:
                {
                    std::cout << "Deopt" << std::endl;
                    break;
                }
                case DecodeData::_non_invoke_ret:
                {
                    std::cout << "Non invoke ret" << std::endl;
                    break;
                }
                case DecodeData::_pop_frame:
                {
                    std::cout << "Pop frame" << std::endl;
                    break;
                }
                case DecodeData::_earlyret:
                {
                    std::cout << "Earlyret" << std::endl;
                    break;
                }
                case DecodeData::_jit_code:
                {
                    const JitSection *section;
                    std::vector<int> pcs;
                    if (!access.get_jit_code(loc, section, pcs))
                    {
                        std::cerr << "DecodeOutput error: Fail to get jit code at "
                                  << loc << std::endl;
                        exit(1);
                    }
                    std::cout << "Jit code: " << section->id() << std::endl;
                    break;
                }
                case DecodeData::_data_loss:
                {
                    std::cout << "Data loss" << std::endl;
                    break;
                }
                case DecodeData::_decode_error:
                {
                    std::cout << "Decode error" << std::endl;
                    break;
                }
                default:
                {
                    std::cerr << "DecodeOutput error: unknown type" << std::endl;
                    exit(1);
                }
                }
            }
        }
    }
}

#include "java/block.hpp"
#include "java/bytecodes.hpp"
#include "java/class_file_stream.hpp"

#include <iomanip>
#include <iostream>

Block *BlockGraph::offset2block(int offset)
{
    auto iter = _offset2block.find(offset);
    if (iter != _offset2block.end())
    {
        return iter->second;
    }
    else
    {
        return nullptr;
    }
}

Block *BlockGraph::make_block_at(int offset, Block *current)
{
    Block *block = offset2block(offset);
    if (block == nullptr)
    {
        int id = _blocks.size();
        _blocks.push_back(new Block(id, offset));
        block = _blocks[id];
        _offset2block[offset] = block;
    }
    if (current != nullptr)
    {
        current->add_succs(block);
        block->add_preds(current);
    }
    return block;
}

void BlockGraph::build_graph()
{
    if (_is_build_graph)
        return;
    Block *current = nullptr;
    ClassFileStream bs(_code, _code_length);
    int bc_size = 0;
    std::unordered_set<int> block_start;
    block_start.insert(0);
    int bci = -1;
    while (!bs.at_eos())
    {
        bci = bs.get_offset();
        Bytecodes::Code bc = Bytecodes::cast(bs.get_u1());
        if (Bytecodes::is_block_end(bc))
        {
            switch (bc)
            {
            /* track bytecodes that affect the control flow */
            case Bytecodes::_athrow: /* fall through */
                block_start.insert(bs.get_offset());
                break;
            case Bytecodes::_ireturn: /* fall through */
            case Bytecodes::_lreturn: /* fall through */
            case Bytecodes::_freturn: /* fall through */
            case Bytecodes::_dreturn: /* fall through */
            case Bytecodes::_areturn: /* fall through */
            case Bytecodes::_return:
                block_start.insert(bs.get_offset());
                break;

            case Bytecodes::_ifeq:      /* fall through */
            case Bytecodes::_ifne:      /* fall through */
            case Bytecodes::_iflt:      /* fall through */
            case Bytecodes::_ifge:      /* fall through */
            case Bytecodes::_ifgt:      /* fall through */
            case Bytecodes::_ifle:      /* fall through */
            case Bytecodes::_if_icmpeq: /* fall through */
            case Bytecodes::_if_icmpne: /* fall through */
            case Bytecodes::_if_icmplt: /* fall through */
            case Bytecodes::_if_icmpge: /* fall through */
            case Bytecodes::_if_icmpgt: /* fall through */
            case Bytecodes::_if_icmple: /* fall through */
            case Bytecodes::_if_acmpeq: /* fall through */
            case Bytecodes::_if_acmpne: /* fall through */
            case Bytecodes::_ifnull:    /* fall through */
            case Bytecodes::_ifnonnull:
            {
                int jmp_offset = (short)bs.get_u2();
                block_start.insert(bci + jmp_offset);
                block_start.insert(bs.get_offset());
                break;
            }

            case Bytecodes::_goto:
            {
                int jmp_offset = (short)bs.get_u2();
                block_start.insert(bci + jmp_offset);
                block_start.insert(bs.get_offset());
                break;
            }

            case Bytecodes::_goto_w:
            {
                int jmp_offset = bs.get_u4();
                block_start.insert(bci + jmp_offset);
                block_start.insert(bs.get_offset());
                break;
            }

            case Bytecodes::_tableswitch:
            {
                bs.skip_u1(alignup(bci+1));
                int default_offset = bs.get_u4();
                block_start.insert(bci + default_offset);
                jlong lo = bs.get_u4();
                jlong hi = bs.get_u4();
                int case_count = hi - lo + 1;
                for (int i = 0; i < case_count; i++)
                {
                    int case_offset = bs.get_u4();
                    block_start.insert(bci + case_offset);
                }
                block_start.insert(bs.get_offset());
                break;
            }

            case Bytecodes::_lookupswitch:
            {
                bs.skip_u1(alignup(bci+1));
                int default_offset = bs.get_u4();
                block_start.insert(bci + default_offset);
                jlong npairs = bs.get_u4();
                for (int i = 0; i < npairs; i++)
                {
                    int case_key = bs.get_u4();
                    int case_offset = bs.get_u4();
                    block_start.insert(bci + case_offset);
                }
                block_start.insert(bs.get_offset());
                break;
            }

            case Bytecodes::_ret:
            {
                block_start.insert(bs.get_offset());
                break;
            }

            case Bytecodes::_jsr:
            {
                int jsr_offset = (short)bs.get_u2();
                block_start.insert(bci + jsr_offset);
                block_start.insert(bs.get_offset());
                break;
            }

            case Bytecodes::_jsr_w:
            {
                int jsr_offset = (short)bs.get_u4();
                block_start.insert(bci + jsr_offset);
                block_start.insert(bs.get_offset());
                break;
            }

            default:
            {
                std::cerr << "BlockGraph error: block end bytecode should not appear "
                          << Bytecodes::name_for(bc) << std::endl;
                exit(1);
            }
            }
        }
        else if (bc == Bytecodes::_invokevirtual ||
                 bc == Bytecodes::_invokespecial ||
                 bc == Bytecodes::_invokestatic)
        {
            u2 methodref_index = bs.get_u2();
            block_start.insert(bs.get_offset());
            _method_refs[bci] = methodref_index;
        }
        else if (bc == Bytecodes::_invokeinterface)
        {
            u2 interface_methodref_index = bs.get_u2();
            bs.get_u2();
            block_start.insert(bs.get_offset());
            _method_refs[bci] = interface_methodref_index;
        }
        else if (bc == Bytecodes::_invokedynamic)
        {
            u2 dynamic_method_index = bs.get_u2();
            bs.get_u2();
            block_start.insert(bs.get_offset());
            _method_refs[bci] = dynamic_method_index;
        }
        else
        {
            bc_size = Bytecodes::length_for(bc);
            if (bc_size == 0)
            {
                jint offset = bs.get_offset();
                bc_size =
                    Bytecodes::special_length_at(bc, bs.current() - 1, offset);
            }
            bs.skip_u1(bc_size - 1);
        }
    }
    bs.set_current();
    bci = -1;
    while (!bs.at_eos())
    {
        if (current == nullptr)
        {
            current = make_block_at(bs.get_offset(), current);
        }
        else
        {
            auto exist = block_start.find(bs.get_offset());
            if (exist != block_start.end())
            {
                Block *temp = make_block_at(bs.get_offset(), current);
                current->set_end_bci(bci);
                current = temp;
            }
        }
        bci = bs.get_offset();
        _offset2block[bci] = current;
        Bytecodes::Code bc = Bytecodes::cast(bs.get_u1());
        if (Bytecodes::is_block_end(bc))
        {
            switch (bc)
            {
            case Bytecodes::_athrow:  /* fall through */
            case Bytecodes::_ireturn: /* fall through */
            case Bytecodes::_lreturn: /* fall through */
            case Bytecodes::_freturn: /* fall through */
            case Bytecodes::_dreturn: /* fall through */
            case Bytecodes::_areturn: /* fall through */
            case Bytecodes::_return:
                current->set_end_bci(bci);
                current = nullptr;
                break;

            case Bytecodes::_ifeq:      /* fall through */
            case Bytecodes::_ifne:      /* fall through */
            case Bytecodes::_iflt:      /* fall through */
            case Bytecodes::_ifge:      /* fall through */
            case Bytecodes::_ifgt:      /* fall through */
            case Bytecodes::_ifle:      /* fall through */
            case Bytecodes::_if_icmpeq: /* fall through */
            case Bytecodes::_if_icmpne: /* fall through */
            case Bytecodes::_if_icmplt: /* fall through */
            case Bytecodes::_if_icmpge: /* fall through */
            case Bytecodes::_if_icmpgt: /* fall through */
            case Bytecodes::_if_icmple: /* fall through */
            case Bytecodes::_if_acmpeq: /* fall through */
            case Bytecodes::_if_acmpne: /* fall through */
            case Bytecodes::_ifnull:    /* fall through */
            case Bytecodes::_ifnonnull:
            {
                int jmp_offset = (short)bs.get_u2();
                make_block_at(bci + jmp_offset, current);
                make_block_at(bs.get_offset(), current);
                current->set_end_bci(bci);
                current = nullptr;
                break;
            }
            case Bytecodes::_goto:
            {
                int jmp_offset = (short)bs.get_u2();
                make_block_at(bci + jmp_offset, current);
                current->set_end_bci(bci);
                current = nullptr;
                break;
            }
            case Bytecodes::_goto_w:
            {
                int jmp_offset = bs.get_u4();
                make_block_at(bci + jmp_offset, current);
                current->set_end_bci(bci);
                current = nullptr;
                break;
            }
            case Bytecodes::_tableswitch:
            {
                bs.skip_u1(alignup(bci+1));
                int default_offset = bs.get_u4();
                jlong lo = bs.get_u4();
                jlong hi = bs.get_u4();
                int case_count = hi - lo + 1;
                for (int i = 0; i < case_count; i++)
                {
                    int case_offset = bs.get_u4();
                    make_block_at(bci + case_offset, current);
                }
                make_block_at(bci + default_offset, current);
                current->set_end_bci(bci);
                current = nullptr;
                break;
            }

            case Bytecodes::_lookupswitch:
            {
                bs.skip_u1(alignup(bci+1));
                int default_offset = bs.get_u4();
                jlong npairs = bs.get_u4();
                for (int i = 0; i < npairs; i++)
                {
                    int case_key = bs.get_u4();
                    int case_offset = bs.get_u4();
                    make_block_at(bci + case_offset, current);
                }
                make_block_at(bci + default_offset, current);
                current->set_end_bci(bci);
                current = nullptr;
                break;
            }

            case Bytecodes::_ret:
            {
                current->set_end_bci(bci);
                current = nullptr;
                break;
            }

            case Bytecodes::_jsr:
            {
                int jsr_offset = (short)bs.get_u2();
                make_block_at(bci + jsr_offset, current);
                current->set_end_bci(bci);
                current = nullptr;
                break;
            }

            case Bytecodes::_jsr_w:
            {
                int jsr_offset = bs.get_u4();
                make_block_at(bci + jsr_offset, current);
                current->set_end_bci(bci);
                current = nullptr;
                break;
            }
            default:
            {
                std::cerr << "BlockGraph error: block end bytecode should not appear "
                          << Bytecodes::name_for(bc) << std::endl;
                exit(1);
            }
            }
        }
        else if (bc == Bytecodes::_invokevirtual ||
                 bc == Bytecodes::_invokespecial ||
                 bc == Bytecodes::_invokestatic)
        {
            int index = bs.get_u2();
            make_block_at(bs.get_offset(), current);
            current->set_end_bci(bci);
            current->set_call_site_index(index);
            current = nullptr;
        }
        else if (bc == Bytecodes::_invokeinterface)
        {
            int index = bs.get_u2();
            bs.get_u2();
            make_block_at(bs.get_offset(), current);
            current->set_end_bci(bci);
            current->set_call_site_index(index);
            current = nullptr;
        }
        else if (bc == Bytecodes::_invokedynamic)
        {
            int index = bs.get_u2();
            bs.get_u2();
            make_block_at(bs.get_offset(), current);
            current->set_end_bci(bci);
            current->set_call_site_index(index);
            current = nullptr;
        }
        else
        {
            bc_size = Bytecodes::length_for(bc);
            if (bc_size == 0)
            {
                jint offset = bs.get_offset();
                bc_size =
                    Bytecodes::special_length_at(bc, bs.current() - 1, offset);
            }
            bs.skip_u1(bc_size - 1);
        }
    }
    ClassFileStream excep_bs(_exception_table,
                             _exception_table_length * 4 * sizeof(u2));
    for (int i = 0; i < _exception_table_length; ++i)
    {
        u2 a1 = excep_bs.get_u2();
        u2 a2 = excep_bs.get_u2();
        u2 a3 = excep_bs.get_u2();
        u2 a4 = excep_bs.get_u2();
        Excep ecp(a1, a2, a3, a4);
        _exceps.push_back(ecp);
    }
    _is_build_graph = true;
}

void BlockGraph::print_graph()
{
    if (!_is_build_graph)
        build_graph();

    for (auto block : _blocks)
    {
        std::cout << "[B" << block->get_id() << "]: [" << block->get_begin_bci()
                  << ", " << block->get_end_bci() << "]" << std::endl;
        int preds_size = block->get_preds_size();
        int succs_size = block->get_succs_size();
        if (preds_size)
        {
            std::cout << " Preds (" << preds_size << "):";
            auto iter = block->get_preds_begin();
            auto end = block->get_preds_end();
            while (iter != end)
            {
                std::cout << " B" << (*iter)->get_id();
                ++iter;
            }
            std::cout << std::endl;
        }
        if (succs_size)
        {
            std::cout << " Succs (" << succs_size << "):";
            auto iter = block->get_succs_begin();
            auto end = block->get_succs_end();
            while (iter != end)
            {
                std::cout << " B" << (*iter)->get_id();
                ++iter;
            }
            std::cout << std::endl;
        }
    }
    if (_exception_table_length)
    {
        std::cout << "Exception table:" << std::endl;
        std::cout << "\tfrom\tto\ttarget" << std::endl;
    }
    for (auto ecp : _exceps)
    {
        std::cout << "\t" << ecp.from << "\t" << ecp.to << "\t" << ecp.target << std::endl;
    }
}

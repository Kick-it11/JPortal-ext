#ifndef BLOCK_HPP
#define BLOCK_HPP

#include "java/bytecodes.hpp"
#include "java/definitions.hpp"
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Block
{
private:
    /* BlockGraph::_blocks[_block_id] */
    int _block_id;

    /* The first bytecode index(included) */
    int _begin_bci;

    /* The last bytecode index(included) */
    int _end_bci;

    /* Invalid if the last code is invoke, point to a methodref in constant pool */
    int _call_site_index;

    std::vector<Block *> _preds;
    std::vector<Block *> _succs;

public:
    Block(int id, int begin_bci)
        : _block_id(id), _begin_bci(begin_bci),
          _end_bci(-1), _call_site_index(-1) {}
    ~Block() {}
    void set_end_bci(int bci) { _end_bci = bci; }
    void set_call_site_index(int index) { _call_site_index = index; }
    void add_preds(Block *block)
    {
        _preds.push_back(block);
    }
    void add_succs(Block *block)
    {
        _succs.push_back(block);
    }
    std::vector<Block *>::iterator get_preds_begin() { return _preds.begin(); }
    std::vector<Block *>::iterator get_preds_end() { return _preds.end(); }
    std::vector<Block *>::iterator get_succs_begin() { return _succs.begin(); }
    std::vector<Block *>::iterator get_succs_end() { return _succs.end(); }
    int get_preds_size() const { return _preds.size(); }
    int get_succs_size() const { return _succs.size(); }
    Block *get_succes_block(int id) const { return id >= 0 && id < get_succs_size() ? _succs[id] : nullptr; }
    int get_id() const { return _block_id; }
    int get_begin_bci() const { return _begin_bci; }
    int get_end_bci() const { return _end_bci; }
    int get_call_site_index() const { return _call_site_index; }
};

struct Excep
{
    u2 from;
    u2 to;
    u2 target;
    u2 type;
    Excep(u2 a1, u2 a2, u2 a3, u2 a4)
        : from(a1), to(a2), target(a3), type(a4) {}
};

class BlockGraph
{
private:
    const u1 *_code;
    const int _code_length; /* size of _code */
    const u1 *_exception_table;
    const u2 _exception_table_length;
    std::vector<Block *> _blocks;
    std::vector<Excep> _exceps;
    std::unordered_map<int, Block *> _offset2block;
    std::unordered_map<int, u2> _method_refs; /* invoke bci -> method ref */
    bool _is_build_graph;

public:
    BlockGraph(const u1 *code, int code_length, const u1 *exception_table,
               const u2 exception_table_length)
        : _code_length(code_length),
          _exception_table_length(exception_table_length),
          _is_build_graph(false)
    {
        _code = new u1[code_length * sizeof(u1)];
        memcpy((void *)_code, code, code_length * sizeof(u1));
        if (exception_table_length)
        {
            _exception_table = new u1[exception_table_length * 4 * sizeof(u2)];
            memcpy((void *)_exception_table, exception_table,
                   exception_table_length * 4 * sizeof(u2));
        }
        else
        {
            _exception_table = nullptr;
        }
    };
    ~BlockGraph()
    {
        for (auto block : _blocks)
        {
            delete block;
        }
        _blocks.clear();

        delete[] _exception_table;
        _exception_table = nullptr;

        delete[] _code;
        _code = nullptr;
    }
    Bytecodes::Code code(int bci)
    {
        if (bci < 0 || bci >= _code_length)
            return Bytecodes::_illegal;
        return Bytecodes::cast(*(_code + bci));
    }
    u2 method_ref(int bci)
    {
        return _method_refs.count(bci) ? _method_refs.at(bci) : -1;
    }
    void set_exceps(std::vector<Excep> exceps) {}
    std::vector<Excep> *get_exceps() { return &_exceps; }
    Block *offset2block(int offset);
    Block *make_block_at(int offset, Block *current);
    const std::vector<Block *> &blocks() const { return _blocks; }
    void build_graph();
    void print_graph();
};

#endif /* BLOCK_HPP */

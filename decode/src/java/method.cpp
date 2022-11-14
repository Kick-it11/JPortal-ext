#include "java/block.hpp"
#include "java/method.hpp"

#include <cassert>
#include <iostream>

Method::Method(std::string name_signature, const u1 *const code_buffer,
               const u2 code_length, const u1 *const exception_table,
               const u2 exception_table_length, const Klass *klass, u2 flags)
    : _name_signature(name_signature),
      _bg(new BlockGraph(code_buffer, code_length, exception_table,
                         exception_table_length)),
      _klass(klass), _flags(flags)
{
    assert(klass != nullptr);
}

Method::~Method()
{
    delete _bg;
    _bg = nullptr;
}

BlockGraph *Method::get_bg() const
{
    _bg->build_bctlist();
    return _bg;
}

void Method::print_graph() const
{
    std::cout << _name_signature << " graph:" << std::endl;
    _bg->print_graph();
}

void Method::print_bctlist() const
{
    std::cout << _name_signature << " bctlist:" << std::endl;
    _bg->print_bctlist();
}

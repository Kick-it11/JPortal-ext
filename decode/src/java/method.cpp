#include "java/block.hpp"
#include "java/method.hpp"

#include <cassert>
#include <iostream>

std::atomic_int Method::MethodCounter(0);

Method::Method(std::string name_signature, const u1 *const code_buffer,
               const u2 code_length, const u1 *const exception_table,
               const u2 exception_table_length, const Klass *klass, u2 flags)
    : _id(MethodCounter++), _name_signature(name_signature),
      _bg(nullptr),
      _klass(klass), _flags(flags)
{
    assert(klass != nullptr);
    if (is_jportal())
        _bg = new BlockGraph(code_buffer, code_length, exception_table,
                             exception_table_length);
}

Method::~Method()
{
    delete _bg;
    _bg = nullptr;
}

BlockGraph *Method::get_bg() const
{
    assert(is_jportal());
    _bg->build_graph();
    return _bg;
}

void Method::print_graph() const
{
    std::cout << _name_signature << " graph:" << std::endl;
    _bg->print_graph();
}

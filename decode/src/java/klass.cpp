#include "java/klass.hpp"
#include "java/method.hpp"

#include <iostream>

void Klass::insert_method_ref(u2 index, std::string name)
{
    _cp_index2method_ref[index] = name;
}

void Klass::insert_method_map(Method *mptr)
{
    _method_map[mptr->get_name()] = mptr;
}

const Method *Klass::getMethod(std::string methodName) const
{
    auto iter = _method_map.find(methodName);
    if (iter != _method_map.end())
    {
        return iter->second;
    }
    return nullptr;
}

std::string Klass::index2method(u2 index) const
{
    auto iter = _cp_index2method_ref.find(index);
    if (iter != _cp_index2method_ref.end())
        return iter->second;
    else
        return "";
}

void Klass::print() const
{
    std::cout << "Methodref:" << std::endl;
    for (auto method_ref : _cp_index2method_ref)
    {
        std::cout << method_ref.first << ": " << method_ref.second << std::endl;
    }
}

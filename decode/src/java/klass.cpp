#include "java/klass.hpp"
#include "java/method.hpp"

#include <iostream>

void Klass::insert_method_ref(u2 index, std::string kname, std::string mname)
{
    _cp_index2method_ref[index] = {kname, mname};
}

void Klass::insert_method_map(const Method *mptr)
{
    _method_map[mptr->get_name()] = mptr;
}

const Method *Klass::get_method(std::string methodName) const
{
    auto iter = _method_map.find(methodName);
    if (iter != _method_map.end())
    {
        return iter->second;
    }
    return nullptr;
}

std::pair<std::string, std::string> Klass::index2methodref(u2 index) const
{
    auto iter = _cp_index2method_ref.find(index);
    if (iter != _cp_index2method_ref.end())
        return iter->second;
    else
        return {"", ""};
}

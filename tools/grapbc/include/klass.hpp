#ifndef JAVA_KLASS_HPP
#define JAVA_KLASS_HPP

#include "method.hpp"
#include <map>
#include <list>

using std::map;
using std::list;

class Klass {
  private:
    string _name;
    // <ConstantPool_index, class.name+signature>
    map<u2, string> _cp_index2method_ref;
    map<string, Method *> _method_map;
    list<string> _interface_name_list;
  public:
    Klass(string name, bool exist) : _name(name) {}

    ~Klass() {
        for (auto i : _method_map) {
            delete (i.second);
        }
    }
    string get_name() const { return _name; }
    void insert_method_ref(u2 index, string name) { _cp_index2method_ref[index] = name; }
    void insert_method_map(Method *mptr) { _method_map[mptr->get_name()] = mptr; }
    const Method *getMethod(string methodName) const { 
        auto iter = _method_map.find(methodName);
        if (iter != _method_map.end()) {
            return iter->second;
        }
        return nullptr;
    }
    string index2method(u2 index) const {
        auto iter = _cp_index2method_ref.find(index);
        if (iter != _cp_index2method_ref.end())
            return iter->second;
        else
          return "";
    }
    void add_interface_name(string interface_name) { _interface_name_list.push_back(interface_name); }
    list<string> get_interface_name_list() { return _interface_name_list; }
    map<string, Method*> *get_method_map() { return &_method_map; }
};

#endif // JAVA_KLASS_HPP
#ifndef JAVA_KLASS_HPP
#define JAVA_KLASS_HPP

#include "type_defs.hpp"

#include <list>
#include <map>
#include <string>

using std::list;
using std::map;
using std::string;

class Method;

class Klass {
  private:
    string _name;
    // <ConstantPool_index, class.name+signature>
    map<u2, string> _cp_index2method_ref;
    map<string, const Method *> _method_map;
    string _father_name;
    list<string> _interface_name_list;
  public:
    Klass(const string &name) : _name(name) {}
    ~Klass() {}

    string get_name() const { return _name; }
    void insert_method_ref(u2 index, string name);
    void insert_method_map(Method *mptr);
    const Method *getMethod(string methodName) const;
    string index2method(u2 index) const;
    void print() const;
    void set_father_name(const string &father_name) { _father_name = father_name; }
    string get_father_name() { return _father_name; }
    void add_interface_name(const string &interface_name) { _interface_name_list.push_back(interface_name); }
    list<string> get_interface_name_list() { return _interface_name_list; }
    map<string, const Method*> *get_method_map() { return &_method_map; }
};

#endif // JAVA_KLASS_HPP

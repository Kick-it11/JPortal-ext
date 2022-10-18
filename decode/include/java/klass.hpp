#ifndef KLASS_HPP
#define KLASS_HPP

#include "utilities/definitions.hpp"

#include <list>
#include <map>
#include <string>

class Method;

class Klass {
  private:
    std::string _name;
    // <ConstantPool_index, class.name+signature>
    std::map<u2, std::string> _cp_index2method_ref;
    std::map<std::string, const Method *> _method_map;
    std::string _father_name;
    std::list<std::string> _interface_name_list;
  public:
    Klass(const std::string &name) : _name(name) {}
    ~Klass() {}

    std::string get_name() const { return _name; }
    void insert_method_ref(u2 index, std::string name);
    void insert_method_map(Method *mptr);
    const Method *getMethod(std::string methodName) const;
    std::string index2method(u2 index) const;
    void print() const;
    void set_father_name(const std::string &father_name) { _father_name = father_name; }
    std::string get_father_name() { return _father_name; }
    void add_interface_name(const std::string &interface_name) { _interface_name_list.push_back(interface_name); }
    std::list<std::string> get_interface_name_list() { return _interface_name_list; }
    std::map<std::string, const Method*> *get_method_map() { return &_method_map; }
};

#endif // KLASS_HPP

#ifndef KLASS_HPP
#define KLASS_HPP

#include "java/definitions.hpp"

#include <map>
#include <string>
#include <vector>

class Method;

class Klass
{
private:
  std::string _name;
  std::map<u2, std::pair<std::string, std::string>> _cp_index2method_ref;
  std::map<std::string, const Method *> _method_map;
  std::string _father_name;
  std::vector<std::string> _interface_name_list;

public:
  Klass(const std::string &name) : _name(name) {}
  ~Klass() {}

  std::string get_name() const { return _name; }
  void insert_method_ref(u2 index, std::string kname, std::string mname);
  void insert_method_map(const Method *mptr);
  const Method *get_method(std::string methodName) const;
  std::pair<std::string, std::string> index2methodref(u2 index) const;
  void set_father_name(const std::string &father_name) { _father_name = father_name; }
  std::string get_father_name() const { return _father_name; }
  void add_interface_name(const std::string &interface_name) { _interface_name_list.push_back(interface_name); }
  std::vector<std::string> get_interface_name_list() { return _interface_name_list; }
  std::map<std::string, const Method *> *get_method_map() { return &_method_map; }
};

#endif /* KLASS_HPP */

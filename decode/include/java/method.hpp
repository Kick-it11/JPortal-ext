#ifndef METHOD_HPP
#define METHOD_HPP

#include "java/definitions.hpp"

#include <list>
#include <map>
#include <string>

class BlockGraph;
class Klass;

class Method {
  private:
    const u2 ACC_JPORTAL = 0x2000;
    u2 _flags;
    const std::string _name_signature;
    BlockGraph *_bg;
    const Klass *_klass;
  public:
    Method(std::string name_signatiue, const u1 *const code_start,
           const u2 code_length, const u1 *const exception_table,
           const u2 exception_table_length, const Klass *klass, u2 flags);
    ~Method();
    std::string get_name() const { return _name_signature; }
    BlockGraph *get_bg() const { return _bg; }
    const Klass *get_klass() const { return _klass; }
    void print_graph() const;
    void print_bctlist() const;
    bool is_jportal() const { return (_flags & ACC_JPORTAL) == ACC_JPORTAL; }
};
#endif /* METHOD_HPP */

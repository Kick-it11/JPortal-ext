#ifndef JAVA_METHOD_HPP
#define JAVA_METHOD_HPP

#include "type_defs.hpp"
#include <string>

using std::string;

class Klass;
class BlockGraph;

class Method {
  private:
    const u2     JPORTAL = 0x2000;
    const string _name_signature; // name+signature
    const u2     _flags;
    u1*          _code_start;
    const u2     _code_length;
    u1*          _bctcode;
    int          _bctcode_length;

  public:
    Method(string name_signature, const u2 flags, const u1 *const code_start, const u2 code_length);
    ~Method() { delete[] _code_start; _code_start = nullptr; delete _bctcode; _bctcode = nullptr; }
    bool   is_jportal() const { return (_flags & JPORTAL) == JPORTAL; }
    string get_name() const { return _name_signature; }
    const u1 code_at(int idx) const;
    const int bctcode_length() const { return _bctcode_length; }
};

#endif // JAVA_METHOD_HPP
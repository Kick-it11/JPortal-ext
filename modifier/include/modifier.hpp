#ifndef Modifier_HPP
#define Modifier_HPP

#include "buffer_stream.hpp"
#include "constantPool.hpp"

#include <map>
#include <set>

class Modifier {
  private:
    // JPORTAL flag
    const int JPORTAL = 0x2000;

    // filters
    double random_rate = 0.0;
    set<string>* enables = nullptr;
    set<string>* disables = nullptr;

    bool enable_all = true;
    bool disable_all = false;
    string class_name;

    // Constant pool parsing
    void parse_constant_pool(BufferStream &stream, ConstantPool &cp, const int length);

    // Field parsing
    void parse_fields(BufferStream &stream, ConstantPool &cp);

    // Method parsing
    void parse_methods(BufferStream &stream, ConstantPool &cp);

    void parse_method(BufferStream &stream, ConstantPool &cp);

    // Classfile attribute parsing
    void parse_classfile_attributes(BufferStream &stream);

    void parse_class(BufferStream &stream, map<string, set<string>> &ef,
                      map<string, set<string>> &df);

  public:
    Modifier(string path, double rate, map<string, set<string>> &ef,
              map<string, set<string>> &df);

    ~Modifier() {};
};

#endif // JAVA_Modifier_HPP
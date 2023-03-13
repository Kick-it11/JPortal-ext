#ifndef METHOD_HPP
#define METHOD_HPP

#include "java/definitions.hpp"

#include <atomic>
#include <string>

class BlockGraph;
class Klass;

class Method
{
private:
    static std::atomic_int MethodCounter;
    const u2 ACC_JPORTAL = 0x2000;
    const u2 ACC_FINAL = 0x0010;
    u2 _flags;
    int _id;
    const std::string _name_signature;
    BlockGraph *_bg;
    const Klass *_klass;

public:
    Method(std::string name_signatiue, const u1 *const code_start,
           const u2 code_length, const u1 *const exception_table,
           const u2 exception_table_length, const Klass *klass, u2 flags);
    ~Method();
    std::string get_name() const { return _name_signature; }
    BlockGraph *get_bg() const;
    const Klass *get_klass() const { return _klass; }
    int id() const { return _id; }
    void print_graph() const;
    bool is_jportal() const { return (_flags & ACC_JPORTAL) == ACC_JPORTAL; }
    bool is_final() const { return (_flags & ACC_FINAL) == ACC_FINAL; }
};
#endif /* METHOD_HPP */

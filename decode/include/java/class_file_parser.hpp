#ifndef CLASS_FILE_PARSER_HPP
#define CLASS_FILE_PARSER_HPP

#include "java/definitions.hpp"

#include <string>

class ClassFileStream;
class Klass;
class Method;
class ConstantPool;
class Analyser;

class ClassFileParser {
private:
    Analyser *_analyser = nullptr;
    Klass *_klass = nullptr;
    ClassFileStream *_stream = nullptr;
    // string _class_name;
    // int _num_patched_klasses;
    // int _max_num_patched_klasses;
    // int _first_patched_klass_resolved_index;

    u2 _major_version;
    u2 _minor_version;
    // AccessFlags _access_flags;
    u2 _this_class_index;
    u2 _super_class_index;
    // vector<u2> _interfaces_index;

    // int _orig_cp_size;

    // u2 _itfs_len;
    // u2 _java_fields_count;

    // bool _has_nonstatic_concrete_methods;
    // bool _declares_nonstatic_concrete_methods;
    // bool _has_final_method;

    // Constant pool parsing
    void parse_constant_pool(const ClassFileStream *const stream,
                             ConstantPool *const cp, const int length);

    // Field parsing
    void parse_fields(const ClassFileStream *const stream, ConstantPool *cp);

    // Method parsing
    void parse_methods(const ClassFileStream *const stream, ConstantPool *cp);

    Method *parse_method(const ClassFileStream *const stream, ConstantPool *cp);

    // Classfile attribute parsing
    void parse_classfile_attributes(const ClassFileStream *const stream);

    void parse_class(const ClassFileStream *const stream);

public:
    ClassFileParser(std::string &file_path, Analyser* analyser, Klass* klass);
    ~ClassFileParser();
};

#endif // CLASS_FILE_PARSER_HPP

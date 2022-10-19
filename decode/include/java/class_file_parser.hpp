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

    u2 _major_version;
    u2 _minor_version;
    u2 _this_class_index;
    u2 _super_class_index;


    /* Constant pool parsing */
    void parse_constant_pool(const ClassFileStream *const stream,
                             ConstantPool *const cp, const int length);

    /* Field parsing */
    void parse_fields(const ClassFileStream *const stream, ConstantPool *cp);

    /* Method parsing */
    void parse_methods(const ClassFileStream *const stream, ConstantPool *cp);

    Method *parse_method(const ClassFileStream *const stream, ConstantPool *cp);

    /* Classfile attribute parsing */
    void parse_classfile_attributes(const ClassFileStream *const stream);

    void parse_class(const ClassFileStream *const stream);

public:
    ClassFileParser(std::string &file_path, Analyser* analyser, Klass* klass);
    ~ClassFileParser();
};

#endif /* CLASS_FILE_PARSER_HPP */

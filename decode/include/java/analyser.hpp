#ifndef ANALYSER_HPP
#define ANALYSER_HPP

#include "java/klass.hpp"
#include "java/method.hpp"

#include <vector>
#include <map>
#include <string>

class Analyser
{
private:
    static std::map<std::string, Klass *> _klasses;
    static std::map<std::string, Method *> _methods;

    static void parse(const std::vector<std::string> &class_paths);

public:
    static void initialize(const std::vector<std::string> &class_paths);
    static void destroy();

    static void add_klass(Klass *klass);
    static void add_method(Method *method);
    static const Klass *get_klass(const std::string &klassName);
    static const Method *get_method(const std::string &klassName, const std::string &methodName);
};

#endif /* ANALYSER_HPP */

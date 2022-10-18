#ifndef ANALYSER_HPP
#define ANALYSER_HPP

#include "java/klass.hpp"
#include "java/method.hpp"

#include <list>
#include <map>
#include <string>

class Analyser {
private:
    std::map<std::string, Klass *> _klasses;
    std::map<std::string, Method *> _methods;

    void parse(const std::list<std::string>& class_paths);

public:
    Analyser(const std::list<std::string>& class_paths);
    ~Analyser();
    void add_klass(Klass* klass);
    void add_method(Method* method);
    const Klass *get_klass(const std::string &klassName);
    const Method *get_method(const std::string &klassName, const std::string &methodName);
};

#endif // ANALYSER_HPP

#ifndef ANALYSER_HPP
#define ANALYSER_HPP

#include "java/klass.hpp"
#include "java/method.hpp"

#include <list>
#include <string>

using std::pair;
using std::string;
using std::map;
using std::list;

class Klass;
class Method;

class Analyser {
  private:
    map<string, Klass *> _klasses;
    map<string, Method *> _methods;

    void parse(const list<string>& class_paths);

  public:
    Analyser(const list<string>& class_paths);
    ~Analyser();
    void add_klass(Klass* klass);
    void add_method(Method* method);
    const Klass *get_klass(const string &klassName);
    const Method *get_method(const string &klassName, const string &methodName);
};

#endif // ANALYSER_HPP

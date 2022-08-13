#ifndef GrapBC_HPP
#define GrapBC_HPP

#include <string>
#include <list>
#include <map>
#include <unordered_map>

using std::string;
using std::list;
using std::map;
using std::unordered_map;

class Klass;
class Method;
class GrapBC {
  private:
    list<string> _file_path;
    string _logfile;
    string _methodidx;
    string _outputfile;

    map<string, Klass*> _Ks;
    unordered_map<int, const Method*> _methods;

  public:
    GrapBC(list<string> &file_path, string &logfile, string &methodidx, string &outputfile):
          _file_path(file_path), _logfile(logfile), _methodidx(methodidx), _outputfile(outputfile) {}

    ~GrapBC() {};

    int parse_all();
    void output();
};

#endif // JAVA_Modifier_HPP
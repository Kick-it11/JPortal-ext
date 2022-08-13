#include <sys/stat.h>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <list>
#include <string.h>

#include "../include/grapbc.hpp"
#include "../include/classFileParser.hpp"
#include "../include/klass.hpp"
#include "../include/method.hpp"
#include "../include/bytecodes.hpp"

using std::cerr;
using std::cout;
using std::endl;
using std::list;
using std::ifstream;
using std::ofstream;
using std::to_string;

int GrapBC::parse_all() {
    for (auto main_path : _file_path) {
        struct dirent *d_ent = nullptr;
        list<string> packages;
        packages.push_back("");
        if (main_path[main_path.length() - 1] != '/')
            main_path = main_path + "/";
        while(!packages.empty()) {
            string package_name = packages.front();
            packages.pop_front();
            string path = main_path + package_name;
            DIR *dir = opendir(path.c_str());
            if (!dir) {
                cerr << "grapbc: " << path << " is not a directory or not exist!" << endl;
                return -1;
            }

            while ((d_ent = readdir(dir)) != NULL) {
                if ((strcmp(d_ent->d_name, ".") == 0) ||
                        (strcmp(d_ent->d_name, "..") == 0))
                    continue;
                if (d_ent->d_type == DT_DIR) {
                    string sub_package = package_name + string(d_ent->d_name) + "/";
                    packages.push_back(sub_package);
                } else {
                    string name(d_ent->d_name);
                    if (name.length() > 6 &&
                            0 == name.compare(name.length() - 6, 6, ".class")) {
                        string file_path = main_path+ package_name + name;
                        string klass_name = package_name + name.substr(0, name.length()-6);
                        auto ks = _Ks.find(klass_name);
                        if (ks != _Ks.end()) {
                            cerr << "grapbc: " << klass_name << "has multiple definations." << endl;
                            closedir(dir);
                            return -1;
                        }
                        _Ks[klass_name] = new Klass(klass_name, true);
                        ClassFileParser cfp(file_path, *(_Ks[klass_name]));
                        cfp.parse_class();
                    }
                }
            }
            closedir(dir);
        }
    }
    ifstream mi(_methodidx);
    if (!mi.is_open()) {
        cerr << "grapbc: method index file cannot be opened" << endl;
        return 1;
    }
    while (mi.peek() != EOF) {
        string line;
        getline(mi, line);
        int idx1 = line.find('.');
        int idx2 = line.find('#', idx1);
        if (idx1 == string::npos || idx2 == string::npos) {
            cerr << "grapbc: invalid index file line: " << line << endl;
            continue;
        }
        string klass_name = line.substr(0, idx1);
        string method_name = line.substr(idx1+1, idx2-idx1-1);
        int idx = atoi(line.substr(idx2+1, line.size()-idx2-1).c_str());
        if (!_Ks.count(klass_name) || !_Ks[klass_name]->getMethod(method_name)) {
            cerr << "grapbc: klass/method not found: " << line << endl;
            continue;
        }
        if (_methods.count(idx)) {
            cerr << "grapbc: repeated index: " << line << endl;
            continue;
        }
        _methods[idx] = _Ks[klass_name]->getMethod(method_name);
    }
    return 0;
}

void GrapBC::output() {
    ifstream log(_logfile);
    if (!log.is_open()) {
        cerr << "grapbc: log file cannot be opened" << endl;
        return;
    }
    map<int, ofstream*> outputs;
    while (log.peek() != EOF) {
        string line;
        getline(log, line);
        int idx1 = line.find(':');
        int idx2 = line.find(':', idx1+1);
        if (idx1 == string::npos || idx2 == string::npos) {
            cerr << "grapbc: invalid log file line: " << line << endl;
            continue;
        }
        int threads_id = atoi(line.substr(0, idx1).c_str());
        int method_idx = atoi(line.substr(idx1+1, idx2-idx1-1).c_str());
        if (idx2 >= line.size()-1 || line[idx2+1] != '[') continue;
        if (!_methods.count(method_idx)) {
            cerr << "grapbc: method index not found: " << line << endl;
            continue;
        }
        if (!_methods[method_idx]->is_jportal()) {
            cout << "not jportal" << endl;
            continue;
        }
        int idx3 = line.find(",", idx2);
        if (idx3 == string::npos) {
            cerr << "grapbc: invalid log file line: " << line << endl;
            continue;
        }
        int start = atoi(line.substr(idx2+2, idx3-idx2-2).c_str());
        int end = atoi(line.substr(idx3+1, line.size()-2-idx3).c_str());
        if (!outputs.count(threads_id))
            outputs[threads_id] = new ofstream(_outputfile+to_string(threads_id));
        int length = _methods[method_idx]->bctcode_length();
        if (start < 0 || start > length || end < 0 || end > length+1) {
            cerr << "grapbc: invalid log file line start/end: " << line << " " << length << endl;
            continue;
        }
        for (int i = start; i < end; ++i) {
            if (i == 0) continue;
            *outputs[threads_id] << (int)_methods[method_idx]->code_at(i-1) << endl;
        }
    }
    for (auto iter : outputs) delete iter.second;
}

int main(int argc, char **argv) {
    if (argc <= 1) {
        cerr << "grapbc: missing argumen, try grapbc -h." << endl;
        return 1;
    }

    double random_rate = 0.0;
    list<string> file_path;
    string logfile;
    string methodidx;
    string outputfile = "grapbc.output";
    for (int i = 1; i < argc;) {
        string arg = argv[i++];
        if (arg == "-h") {
            cout << endl;
            cout << "  usage: grapbc [--h] [-c classpath]+ -l logfile -o outputfile" << endl;
            cout << "    -h: Display this." << endl;
            cout << "    [-c classpath]+: Specify more than one classfile path." << endl;
            cout << "    -l logfile: Specify logfile." << endl;
            cout << "    -m methodidx: Specify method index file." << endl;
            cout << "    -o outputfile: Specify output file name." << endl;
            return 0;
        }

        if (arg == "-c") {
            if (i >= argc) {
                cerr << "grapbc: missing classpath." << endl;
                return 1;
            }
            file_path.push_back(argv[i++]);
            continue;
        }

        if (arg == "-l") {
            if (i >= argc) {
                cerr << "grapbc: missing logfile." << endl;
                return 1;
            }
            logfile = argv[i++];
            continue;
        }

        if (arg == "-m") {
            if (i >= argc) {
                cerr << "grapbc: missing method index file." << endl;
                return 1;
            }
            methodidx = argv[i++];
            continue;
        }

        if (arg == "-o") {
            if (i >= argc) {
                cerr << "grapbc: missing outputfile." << endl;
                return 1;
            }
            outputfile = argv[i++];
            continue;
        }

        cerr << "grapbc: unknown argument, try grapbc -h." << endl;
        break;
    }

    if (file_path.empty()) {
        cerr << "grapbc: class file path missing." << endl;
        return 1;
    }
    if (logfile.empty()) {
        cerr << "grap: log file path missing." << endl;
        return 1;
    }
    if (methodidx.empty()) {
        cerr << "grap: log file path missing." << endl;
        return 1;
    }

    Bytecodes::initialize();

    GrapBC *gbc = new GrapBC(file_path, logfile, methodidx, outputfile);
    if (gbc->parse_all()) {
        return 1;
    }
    gbc->output();
    return 0;
}
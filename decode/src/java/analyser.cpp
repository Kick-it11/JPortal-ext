#include "java/analyser.hpp"
#include "java/class_file_parser.hpp"

#include <cassert>
#include <cstring>
#include <dirent.h>
#include <iostream>

Analyser::Analyser(const std::list<std::string> &class_paths) {
    parse(class_paths);
}

Analyser::~Analyser() {
    for (auto k : _klasses) {
        delete (k.second);
    }
    for (auto m : _methods) {
        delete (m.second);
    }
}

void Analyser::add_klass(Klass* klass) {
    assert(klass && !_klasses.count(klass->get_name()));
    _klasses[klass->get_name()] = klass;
}

void Analyser::add_method(Method* method) {
    assert(method && method->get_klass() && !_methods.count(method->get_klass()->get_name()+method->get_name()));
    _methods[method->get_klass()->get_name() + method->get_name()] = method;
}

const Klass *Analyser::get_klass(const std::string& klassName) {
    return _klasses.count(klassName)?_klasses[klassName]:nullptr;
}

const Method* Analyser::get_method(const std::string& klassName, const std::string& methodName) {
    return _methods.count(klassName+methodName)?_methods[klassName+methodName]:nullptr;
}

void Analyser::parse(const std::list<std::string>& class_paths) {
    for (auto main_path : class_paths) {
        struct dirent *d_ent = nullptr;
        std::list<std::string> packages;
        packages.push_back("");
        if (main_path[main_path.length() - 1] != '/')
            main_path = main_path + "/";
        while(!packages.empty()) {
            std::string package_name = packages.front();
            packages.pop_front();
            std::string path = main_path + package_name;
            DIR *dir = opendir(path.c_str());
            if (!dir) {
                std::cout << path << " is not a directory or not exist!" << std::endl;
                return;
            }

            while ((d_ent = readdir(dir)) != NULL) {
                if ((strcmp(d_ent->d_name, ".") == 0) ||
                        (strcmp(d_ent->d_name, "..") == 0))
                    continue;
                if (d_ent->d_type == DT_DIR) {
                    std::string sub_package = package_name + std::string(d_ent->d_name) + "/";
                    packages.push_back(sub_package);
                } else {
                    std::string name(d_ent->d_name);
                    if (name.length() > 6 &&
                            0 == name.compare(name.length() - 6, 6, ".class")) {
                        std::string file_path = main_path+ package_name + name;
                        // cout << "parse: " << file_path << endl;
                        std::string klass_name = package_name + name.substr(0, name.length()-6);
                        assert(!_klasses.count(klass_name));
                        Klass* klass = new Klass(klass_name);
                        _klasses[klass_name] = klass;
                        ClassFileParser cfp(file_path, this, klass);
                    }
                }
            }
            closedir(dir);
        }
    }
}

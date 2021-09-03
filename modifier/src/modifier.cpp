#include <sys/stat.h>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <list>

#include "../include/modifier.hpp"

#define JAVA_CLASSFILE_MAGIC 0xCAFEBABE
#define JAVA_MIN_SUPPORTED_VERSION 45
#define JAVA_PREVIEW_MINOR_VERSION 65535

// Used for two backward compatibility reasons:
// - to check for new additions to the class file format in JDK1.5
// - to check for bug fixes in the format checker in JDK1.5
#define JAVA_1_5_VERSION 49

// Used for backward compatibility reasons:
// - to check for javac bug fixes that happened after 1.5
// - also used as the max version when running in jdk6
#define JAVA_6_VERSION 50

// Used for backward compatibility reasons:
// - to disallow argument and require ACC_STATIC for <clinit> methods
#define JAVA_7_VERSION 51

// Extension method support.
#define JAVA_8_VERSION 52

#define JAVA_9_VERSION 53

#define JAVA_10_VERSION 54

#define JAVA_11_VERSION 55

#define JAVA_12_VERSION 56

// Class file format tags
#define TAG_CODE "Code"

#define RAND (double)rand()/((double)RAND_MAX)

Modifier::Modifier(string path, bool ea, bool da, double rate,
              map<string, set<string>> &ef, map<string, set<string>> &df) :
    enable_all(ea), disable_all(da),
    random_rate(rate), enables(nullptr), disables(nullptr) {
    // check if file exists
    if (enable_all && disable_all) {
        cerr << "modifier: Filter conflict" << endl;
        return;
    }
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        // found file, open it
        ifstream file_read(path);
        if (file_read.is_open()) {
            BufferStream stream(st.st_size);
            // read contents into resource array
            file_read.read((char *)stream.get_buffer_begin(), st.st_size);
            assert(file_read.peek() == EOF);
            // close file
            file_read.close();
            parse_class(stream, ef, df);
            if (!stream.get_change())
                return;
            ofstream file_write(path);
            if (file_write.is_open()) {
                file_write.write((char *)stream.get_buffer_begin(), stream.get_size());
                file_write.close();
                return;
            }
        }
    }
    cerr << "modifier: Cannot open file: " << path << endl;
}

void Modifier::parse_class(BufferStream &stream, map<string, set<string>> &ef,
                            map<string, set<string>> &df) {
    // Magic value
    stream.skip_u4(1);

    // Version numbers
    stream.skip_u2(1);
    stream.skip_u2(1);

    u2 cp_size = stream.get_u2();

    ConstantPool cp(cp_size);

    parse_constant_pool(stream, cp, cp_size);

    // Access flags
    stream.skip_u2(1);

    // This class
    u2 this_class_index = stream.get_u2();
    class_name = cp.symbol_at(this_class_index);
    map<string, set<string>>::iterator iter1;
    map<string, set<string>>::iterator iter2;
    if (!enable_all && !disable_all) {
        iter1 = ef.find(class_name);
        if (iter1 != ef.end() && !iter1->second.empty()) {
            enable_all = false;
            enables = &(iter1->second);
        } else if (iter1 != ef.end() && iter1->second.empty()) {
            enable_all = true;
            ef.erase(iter1);
        }
        iter2 = df.find(class_name);
        if (iter2 != df.end() && !iter2->second.empty()) {
            disable_all = false;
            disables = &(iter2->second); 
        } else if (iter2 != df.end() && iter2->second.empty()) {
            disable_all = true;
            df.erase(iter2);
        }
        if (enable_all && disable_all) {
            cerr << "modifier: Filter conflict." << endl;
            return;
        }
    }
    // Super class
    stream.skip_u2(1);

    // Interfaces
    u2 itfs_len = stream.get_u2();
    stream.skip_u2(itfs_len);

    // Skip Fields
    parse_fields(stream, cp);

    // Methods
    parse_methods(stream, cp);

    // Additional attributes/annotations
    parse_classfile_attributes(stream);

    if (enables && enables->empty()) {
        ef.erase(iter1);
        enables = nullptr;
    }
    if (disables && disables->empty()) {
        df.erase(iter2);
        disables = nullptr;
    }
    return;
}

void Modifier::parse_constant_pool(BufferStream &stream, ConstantPool &cp, const int length) {
    int index = 1;
    for (index = 1; index < length; ++index) {
        const u1 tag = stream.get_u1();
        switch (tag) {
        case CONSTANT_Class_info: {
            const u2 name_index = stream.get_u2();
            cp._constants[index] =
                new Constant_Class_info(CONSTANT_Class_info, name_index);
            break;
        }
        case CONSTANT_Fieldref_info: {
            const u2 class_index = stream.get_u2();
            const u2 name_and_type_index = stream.get_u2();
            cp._constants[index] = new Constant_Fieldref_info(
                CONSTANT_Fieldref_info, class_index, name_and_type_index);
            break;
        }
        case CONSTANT_Methodref_info: {
            const u2 class_index = stream.get_u2();
            const u2 name_and_type_index = stream.get_u2();
            cp._constants[index] = new Constant_Methodref_info(
                CONSTANT_Methodref_info, class_index, name_and_type_index);
            break;
        }
        case CONSTANT_InterfaceMethodref_info: {
            const u2 class_index = stream.get_u2();
            const u2 name_and_type_index = stream.get_u2();
            cp._constants[index] = new Constant_InterfaceMethodref_info(
                CONSTANT_InterfaceMethodref_info, class_index,
                name_and_type_index);
            break;
        }
        case CONSTANT_String_info: {
            const u2 string_index = stream.get_u2();
            cp._constants[index] =
                new Constant_String_info(CONSTANT_String_info, string_index);
            break;
        }
        case CONSTANT_MethodHandle_info: {
            const u1 ref_kind = stream.get_u1();
            const u2 method_index = stream.get_u2();
            cp._constants[index] = new Constant_MethodHandle_info(
                CONSTANT_MethodHandle_info, ref_kind, method_index);
            break;
        }
        case CONSTANT_MethodType_info: {
            const u2 signature_index = stream.get_u2();
            cp._constants[index] = new Constant_MethodType_info(
                CONSTANT_MethodType_info, signature_index);
            break;
        }
        case CONSTANT_Dynamic_info: {
            const u2 bootstrap_specifier_index = stream.get_u2();
            const u2 name_and_type_index = stream.get_u2();
            cp._constants[index] = new Constant_Dynamic_info(
                CONSTANT_Dynamic_info, bootstrap_specifier_index,
                name_and_type_index);
            break;
        }
        case CONSTANT_InvokeDynamic_info: {
            const u2 bootstrap_specifier_index = stream.get_u2();
            const u2 name_and_type_index = stream.get_u2();
            cp._constants[index] = new Constant_InvokeDynamic_info(
                CONSTANT_InvokeDynamic_info, bootstrap_specifier_index,
                name_and_type_index);
            break;
        }
        case CONSTANT_Integer_info: {
            const u4 bytes = stream.get_u4();
            cp._constants[index] =
                new Constant_Integer_info(CONSTANT_Integer_info, bytes);
            break;
        }
        case CONSTANT_Float_info: {
            const u4 bytes = stream.get_u4();
            cp._constants[index] =
                new Constant_Float_info(CONSTANT_Float_info, bytes);
            break;
        }
        case CONSTANT_Long_info: {
            const u8 bytes = stream.get_u8();
            cp._constants[index] =
                new Constant_Long_info(CONSTANT_Long_info, bytes);
            ++index;
            break;
        }
        case CONSTANT_Double_info: {
            const u8 bytes = stream.get_u8();
            cp._constants[index] =
                new Constant_Double_info(CONSTANT_Double_info, bytes);
            ++index;
            break;
        }
        case CONSTANT_NameAndType_info: {
            const u2 name_index = stream.get_u2();
            const u2 signature_index = stream.get_u2();
            cp._constants[index] = new Constant_NameAndType_info(
                CONSTANT_NameAndType_info, name_index, signature_index);
            break;
        }
        case CONSTANT_Utf8_info: {
            const u2 str_len = stream.get_u2();
            char *buffer = (char *)stream.current();
            string str(buffer, str_len);
            cp._constants[index] =
                new Constant_Utf8_info(CONSTANT_Utf8_info, str_len, str);
            stream.skip_u1(str_len);
            break;
        }
        case 19:
        case 20: {
            u2 name_index = stream.get_u2();
            cp._constants[index] =
            new Constant_Class_info(CONSTANT_Class_info, name_index);
            break;
        }
        default: {
        }
        }
    }

    for (index = 1; index < length; ++index) {
        auto tag = cp._constants[index]->tag();
        if (CONSTANT_Methodref_info == tag ||
            CONSTANT_InterfaceMethodref_info == tag) {
            auto methodref = (Constant_Methodref_info *)cp._constants[index];
            auto class_info =
                (Constant_Class_info
                     *)(cp._constants[methodref->get_class_index()]);
            string class_name =
                ((Constant_Utf8_info
                      *)(cp._constants[class_info->get_name_index()]))
                    ->str();
            auto name_and_type =
                (Constant_NameAndType_info
                     *)(cp._constants[methodref->get_name_and_type_index()]);
            string name =
                ((Constant_Utf8_info
                      *)(cp._constants[name_and_type->get_name_index()]))
                    ->str();
            string type =
                ((Constant_Utf8_info
                      *)(cp._constants[name_and_type->get_type_index()]))
                    ->str();
        } else if (CONSTANT_Dynamic_info == tag || CONSTANT_InvokeDynamic_info == tag) {
            auto dynamicinfo = (Constant_InvokeDynamic_info*)(cp._constants[index]);
            auto name_and_type =
                (Constant_NameAndType_info
                     *)(cp._constants[dynamicinfo->get_name_and_type_index()]);
            string name =
                ((Constant_Utf8_info
                      *)(cp._constants[name_and_type->get_name_index()]))
                    ->str();
            string type =
                ((Constant_Utf8_info
                      *)(cp._constants[name_and_type->get_type_index()]))
                    ->str();
        } else if (CONSTANT_Long_info == tag || CONSTANT_Double_info == tag) {
            ++index;
        }
    }
}

void Modifier::parse_fields(BufferStream &stream, ConstantPool &cp) {
    const u2 length = stream.get_u2();
    for (int n = 0; n < length; n++) {
        const jint flags = stream.get_u2();

        const u2 name_index = stream.get_u2();
        const u2 signature_index = stream.get_u2();

        u2 attributes_count = stream.get_u2();

        while (attributes_count--) {
            const u2 attribute_name_index = stream.get_u2();
            const u4 attribute_length = stream.get_u4();
            stream.skip_u1(attribute_length);
        }

    }
}

void Modifier::parse_methods(BufferStream &stream, ConstantPool &cp) {
    const u2 length = stream.get_u2();
    for (int index = 0; index < length; index++) {
        parse_method(stream, cp);
    }
}

void Modifier::parse_method(BufferStream &stream, ConstantPool &cp) {
    int offset = stream.get_offset();
    int flags = stream.get_u2();
    const u2 name_index = stream.get_u2();
    const string method_name = cp.symbol_at(name_index);

    const u2 signature_index = stream.get_u2();
    const string method_signature = cp.symbol_at(signature_index);

    bool enable = false;
    if (enable_all) {
        enable = true;
    } else if (disable_all) {
        enable = false;
    } else {
        bool enable_set = false;
        if (enables) {
            auto iter = enables->find(method_name+method_signature);
            if (iter != enables->end()) {
                enable = true;
                enable_set = true;
                enables->erase(iter);
            }
        }
        if (disables && !enable_set) {
            auto iter = disables->find(method_name+method_signature);
            if (iter != disables->end()) {
                enable = false;
                enable_set = true;
                enables->erase(iter);
            }
        }
        if (!enable_set && random_rate != 0.0) {
            if (RAND < random_rate)
                enable = true;
        }
    }
    u2 new_flags = enable?(flags | JPORTAL):(flags & ~JPORTAL);
    if (flags != new_flags) {
        stream.set_u2(offset, new_flags);
        if (enable) cout << "modifier: Enable: ";
        else cout << "modifier: Disable: ";
        cout << class_name << "." << method_name << method_signature << endl;
    }
    u2 method_attributes_count = stream.get_u2();
    while (method_attributes_count--) {
        const u2 method_attribute_name_index = stream.get_u2();
        const u4 method_attribute_length = stream.get_u4();
        // skip
        stream.skip_u1(method_attribute_length);
    }
}

// Classfile attribute parsing
void Modifier::parse_classfile_attributes(BufferStream &stream) {
    u2 attributes_count = stream.get_u2();

    while (attributes_count--) {
        const u2 attribute_name_index = stream.get_u2();
        const u4 attribute_length = stream.get_u4();
        stream.skip_u1(attribute_length); // Skip attribute
    }
}

static void get_filter(string filter, map<string, set<string>> &filters) {
    size_t pos = 0;
    int length = filter.length();
    string class_name;
    vector<string> klasses;

    if (length < 0)
        return;

    int j = 0;
    for (; j < filter.length(); j++) {
        if (filter[j] == ' ') {
            klasses.push_back(filter.substr(pos, j-pos));
            pos = j+1;
        }
    }
    if (pos < j) klasses.push_back(filter.substr(pos, j-pos));

    for (auto&& klass : klasses) {
        string klass_name;
        length = klass.length();
        for (j = 0; j < length; j++) {
            if (klass[j] == ':') {
                klass_name = klass.substr(0, j);
                j++;
                break;
            }
        }
        if (j == 0) continue;
        else if (j == length && klass[j-1] != ':') klass_name = klass.substr(0, j);
        filters[klass_name];
        pos = j;
        for (; j < length; j++) {
            if (klass[j] == ',') {
                filters[klass_name].insert(klass.substr(pos, j-pos));
                pos = j+1;
            }
        }
        if (pos < j) filters[klass_name].insert(klass.substr(pos, j-pos));
    }
}

int main(int argc, char **argv) {
    if (argc <= 1) {
        cerr << "modifer: Missing argumen, try modifier --help." << endl;
        return 1;
    }
    bool enable_all = true;
    bool disable_all = false;
    map<string, set<string>> enables;
    map<string,set<string>> disables;
    double random_rate = 0.0;
    string file_path;
    for (int i = 1; i < argc;) {
        string arg = argv[i++];
        if (arg == "--help") {
            cout << endl;
            cout << "  usage: modifier [--help] [--enable/--diable \"filters\"/all]/[--random rate] path" << endl;
            cout << "    --help: Display this." << endl;
            cout << "    --enable or --disable: Enable or disable certain methods in specified files, \
otherwise enable all. Method with classname[:methodname], classes separated by space,\
methods separated by comma. Classname with zero methodnames specified means enable or diable all. \
If neither of enable/disable/random is set, enable all. [Exclude each other and random]" << endl;
            cout << "    --random: Random enable method with percentage of 1/num. [Exclude with enable/diable]" << endl;
            return 0;
        }

        if (arg == "--enable" || arg == "--disable") {
            if (random_rate != 0.0) {
                cerr << "modifier: Filter conflict." << endl;
                return 1;
            }
            if (!enables.empty() || !disables.empty()) {
                cerr << "modifier: Filter conflict." << endl;
                return 1;
            }
            if (i == argc) {
                cerr << "modifier: Enable/Disable methods missing." << endl;
                return 1;
            }
            string filter = argv[i++];
            if (filter == "all" && arg == "--enable") {
                enable_all = true;
            } else if (filter == "all" && arg == "--disable") {
                enable_all = false;
                disable_all = true;
            } else {
                enable_all = false;
                get_filter(filter, (arg == "--enable")?enables:disables);
            }
            continue;
        }

        if (arg == "--random") {
            if (!enables.empty() || !disables.empty()) {
                cerr << "modifier: Filter conflict.5" << endl;
                return 1; 
            }
            if (i == argc) {
                cerr << "modifier: Random rate missing." << endl;
                return 1;
            }
            int rate = atoi(argv[i++]);
            if (rate <= 0) {
                cerr << "modifier: Random rate set error." << endl;
                return 1;
            }
            random_rate = (double)1/rate;
            enable_all = false;
            srand(time(NULL));
            continue;
        }
        file_path = arg;
        break;
    }
    if (file_path == "") {
        cerr << "modifier: File path missing." << endl;
        return 1;
    }

    struct dirent *d_ent = nullptr;
    list<string> recursive_paths;
    recursive_paths.push_back(file_path);
    while(!recursive_paths.empty()) {
        string path = recursive_paths.front();
        recursive_paths.pop_front();
        DIR *dir = opendir(path.c_str());
        if (!dir && path.length() > 6 &&
                0 == path.compare(path.length() - 6, 6, ".class")) {
            Modifier md(path, enable_all, disable_all, random_rate, enables, disables);
            break;
        } else if (!dir) {
            cerr << "modifier: File not supported: " << path << endl;
            return 1;
        }
        while ((d_ent = readdir(dir)) != NULL) {
            if (string(d_ent->d_name) == "." ||
                    string(d_ent->d_name) == "..")
                continue;
            if (d_ent->d_type == DT_DIR) {
                string sub_path = path + "/" + string(d_ent->d_name);
                recursive_paths.push_back(sub_path);
            } else {
                string name(d_ent->d_name);
                if (name.length() > 6 &&
                      0 == name.compare(name.length() - 6, 6, ".class")) {
                    string klass_path = path + "/" + name;
                    Modifier md(klass_path, enable_all, disable_all, random_rate, enables, disables);
                }
            }
        }
        closedir(dir);
    }

    if (!enables.empty() || !disables.empty()) {
        cerr << "modifier: Cannot find filter:" << endl;
    }
    for (auto&& klass : enables) {
        if (klass.second.empty())
            cerr << klass.first << ";" << endl;
        for (auto&& method : klass.second) {
            cerr << klass.first << ":" << method << ";" << endl;
        }
    }
    for (auto&& klass : disables) {
        if (klass.second.empty())
            cerr << klass.first << ";" << endl;
        for (auto&& method : klass.second) {
            cerr << klass.first << ":" << method << ";" << endl;
        }
    }
    return 0;
}
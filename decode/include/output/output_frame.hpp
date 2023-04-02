#ifndef OUTPUT_FRAME_HPP
#define OUTPUT_FRAME_HPP

#include <java/bytecodes.hpp>

#include <vector>

class Block;
class JitSection;
class Method;
struct PCStackInfo;

class InterFrame
{
private:
    const Method *_method;
    int _bci;
    Block *_block;
    bool _pending; /* indicate an event needed to help forward */
    bool _jit_invoke; /* indicate this frame have a jit call now -> should not use static callee */

public:
    InterFrame(const Method * method, int bci);

    ~InterFrame() {};

    const Method *method() { return _method; }

    Bytecodes::Code code();

    int bci() { return _bci; }

    int next_bci() { return _bci + Bytecodes::length_for(code()); }

    const Method *static_callee();

    bool taken();

    bool not_taken();

    bool switch_case(int idx);

    bool switch_default();

    bool invoke();

    bool straight(int idx);

    bool forward(std::vector<std::pair<int, int>> &ans);

    bool forward(int idx, std::vector<std::pair<int, int>> &ans);

    void set_jit_invoke();
    bool jit_invoke() { return _jit_invoke; }
};

class JitFrame
{
private:
    const JitSection *_section;
    std::vector<std::pair<const Method *, Block*>> _iframes;

public:
    JitFrame(const JitSection *section) : _section(section) {}
    ~JitFrame() {};

    const JitSection *section() { return _section; }

    void entry(bool cfgt, bool mt,
               std::vector<std::pair<int, std::pair<int, int>>> &cfgs,
               std::vector<std::pair<std::string, int>> &methods);

    void clear();

    void jit_code(std::vector<const PCStackInfo *> pcs,
                  bool cfgt, bool mt,
                  std::vector<std::pair<int, std::pair<int, int>>> &cfgs,
                  std::vector<std::pair<std::string, int>> &methods);

    void jit_return(bool cfgt, bool mt,
                    std::vector<std::pair<int, std::pair<int, int>>> &cfgs,
                    std::vector<std::pair<std::string, int>> &methods);
};

#endif /* OUTPUT_FRAME_HPP */

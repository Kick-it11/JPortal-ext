#ifndef OUTPUT_FRAME_HPP
#define OUTPUT_FRAME_HPP

#include <vector>

class Block;
class JitSection;
class Method;
struct PCStackInfo;

/* Java Execution frame */
class OutputFrame {
private:

public:
    virtual bool is_jit_frame() = 0;
    virtual bool is_inter_frame() = 0;
};

class InterFrame : public OutputFrame
{
private:
    const Method *_method;
    Block *_block;
    int _bct;
    bool _use_next_bct;

    void forward(std::vector<uint8_t> &codes);

    void forward(std::vector<uint8_t> &codes, int bct);

public:
    InterFrame(const Method * _method, int bci, bool use_next_bci);
    ~InterFrame();

    virtual bool is_jit_frame() { return false; }
    virtual bool is_inter_frame() { return true; }

    const Method *method() { return _method; }

    bool method_exit(std::vector<uint8_t> &codes);
    bool branch_taken(std::vector<uint8_t> &codes);
    bool branch_not_taken(std::vector<uint8_t> &codes);
    bool switch_case(std::vector<uint8_t> &codes, int index);
    bool switch_default(std::vector<uint8_t> &codes);
    bool invoke_site(std::vector<uint8_t> &codes);
    bool exception_handling(std::vector<uint8_t> &codes, int bci1, int bci2);
};

class JitFrame : public OutputFrame
{
private:
    const JitSection *_section;
    std::vector<std::pair<const Method *, Block*>> _iframes;

public:
    JitFrame(const JitSection *section) : _section(section) {}
    ~JitFrame();

    virtual bool is_jit_frame() { return true; }
    virtual bool is_inter_frame() { return false; }
    const JitSection *section() { return _section; }

    bool jit_code(std::vector<uint8_t> &codes, const JitSection *section,
                  const PCStackInfo **pcs, uint64_t size, bool entry);
    bool jit_return(std::vector<uint8_t> &codes);
};

#endif /* OUTPUT_FRAME_HPP */

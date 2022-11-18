#ifndef OUTPUT_FRAME_HPP
#define OUTPUT_FRAME_HPP

#include <vector>

class JitSection;
class Method;
class Block;

class OutputFrame {
private:
    /* for a Interpreter frame, section is nullptr, and prev_frame.size = 1 */
    const JitSection *section;
    std::vector<std::pair<const Method *, Block *>> prev_frame;

public:

};

#endif /* OUTPUT_FRAME_HPP */

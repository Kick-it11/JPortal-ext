#ifndef DECODE_OUTPUT_HPP
#define DECODE_OUTPUT_HPP

#include "decoder/decode_data.hpp"

#include <map>
#include <vector>

class OutputFrame;

/* This class takes a list of DecodeData and output them to file */
class DecodeOutput
{
private:
    std::map<uint64_t, std::vector<DecodeData::ThreadSplit>> _splits;

    void clear_frames(std::vector<OutputFrame *> frames);

public:
    DecodeOutput(const std::vector<DecodeData *> &data);

    /* output trace data to file with name prefix-thrd1, prefix-thrd*, ... */
    void output(const std::string prefix);

    /* simply print decode data, with no method info */
    void print();
};

#endif /* DECODE_OUTPUT_HPP */

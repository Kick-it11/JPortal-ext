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

    std::vector<OutputFrame *> _frames;

    Analyser *_analyser;

public:
    DecodeOutput(const std::vector<DecodeData *> &data, Analyser *analyser);

    /* output trace data to file with name prefix-thrd1, prefix-thrd*, ... */
    void output_cfg(const std::string prefix);
    void output_func(const std::string prefix);

    /* simply print decode data, with no method info */
    void print();
};

#endif /* DECODE_OUTPUT_HPP */

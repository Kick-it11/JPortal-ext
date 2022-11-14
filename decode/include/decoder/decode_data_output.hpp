#ifndef DECODE_DATA_OUTPUT_HPP
#define DECODE_DATA_OUTPUT_HPP

#include "decoder/decode_data.hpp"

#include <map>
#include <vector>

/* This class takes a list of DecodeData and output them to file */
class DecodeDataOutput
{
private:
    std::map<uint64_t, std::vector<DecodeData::ThreadSplit>> _splits;

public:
    DecodeDataOutput(const std::vector<DecodeData *> &data);

    /* output trace data to file with name prefix-thrd1, prefix-thrd*, ... */
    void output(const std::string prefix);

    /* simply print decode data, with no method info */
    void print();
};

#endif /* DECODE_DATA_OUTPUT_HPP */

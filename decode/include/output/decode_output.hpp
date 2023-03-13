#ifndef DECODE_OUTPUT_HPP
#define DECODE_OUTPUT_HPP

#include "decoder/decode_data.hpp"

class InterFrame;
class JitFrame;

/* This class takes a list of DecodeData and output them to file */
class DecodeOutput
{
private:
    std::map<uint64_t, std::vector<DecodeData::ThreadSplit>> _splits;
    /* for output */
    int _method_id;

    void clear_frames(std::vector<InterFrame *> &inters,
                      std::vector<JitFrame *> &jits);

    bool return_frames(std::ofstream &file,
                       std::vector<InterFrame *> &inters,
                       std::vector<JitFrame *> &jits);

    void find_jit_frame(std::vector<JitFrame *> &jits,
                        const JitSection *section);

    bool check_pre_event(DecodeDataEvent &event, std::ofstream &file,
                         std::vector<InterFrame *> &inters,
                         std::vector<JitFrame *> &jits);

    bool check_post_event(DecodeDataEvent &event, std::ofstream &file,
                          std::vector<InterFrame *> &inters,
                          std::vector<JitFrame *> &jits);

    bool output_jit(DecodeDataEvent &event, std::ofstream &file,
                    std::vector<JitFrame *> &jits);

    bool output(DecodeDataEvent &event, std::ofstream &file);

public:
    DecodeOutput(const std::vector<DecodeData *> &data);

    /* output trace data to file with name prefix-thrd1, prefix-thrd*, ... */
    void output(const std::string prefix);

    /* simply print decode data, with no method info */
    void print();
};

#endif /* DECODE_OUTPUT_HPP */

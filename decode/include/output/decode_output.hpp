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

    void to_method_entry(DecodeDataAccess &access, std::ofstream &file, const Method *method);
    void to_method_exit(DecodeDataAccess &access, std::ofstream &file);
    void to_branch_taken(DecodeDataAccess &access, std::ofstream &file);
    void to_branch_not_taken(DecodeDataAccess &access, std::ofstream &file);
    void to_switch_case(DecodeDataAccess &access, std::ofstream &file, int index);
    void to_switch_default(DecodeDataAccess &access, std::ofstream &file);
    void to_invoke_site(DecodeDataAccess &access, std::ofstream &file);

    void to_exception_handling(DecodeDataAccess &access, std::ofstream &file,
                               const Method *method, int bci, int handler_bci);
    void to_deoptimization(DecodeDataAccess &access, std::ofstream &file, const Method *method,
                           int bci, bool use_next_bci, bool is_bottom_frame);

    void to_jit_code(DecodeDataAccess &access, std::ofstream &file,
                     const JitSection *section, const PCStackInfo **info, uint64_t size,
                     bool entry, bool os_entry);
    void to_jit_return(DecodeDataAccess &access, std::ofstream &file);

    void to_data_loss(DecodeDataAccess &access, std::ofstream &file);

    void to_decode_error(DecodeDataAccess &access, std::ofstream &file);

    void clear_frame();

    void output_codes(std::ofstream &file, std::vector<uint8_t> &codes);

public:
    DecodeOutput(const std::vector<DecodeData *> &data);

    /* output trace data to file with name prefix-thrd1, prefix-thrd*, ... */
    void output(const std::string prefix);

    /* simply print decode data, with no method info */
    void print();
};

#endif /* DECODE_OUTPUT_HPP */

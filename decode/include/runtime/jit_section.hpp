#ifndef JIT_SECTION_HPP
#define JIT_SECTION_HPP

#include "java/definitions.hpp"

#include <atomic>
#include <map>
#include <mutex>
#include <string>

class Method;
/*
 * Record that gives information about the methods on the compile-time
 * stack at a specific pc address of a compiled method. Each element in
 * the methods array maps to same element in the bcis array.
 */
struct PCStackInfo
{
    uint64_t pc = 0;        /* the pc address for this compiled method */
    int numstackframes = 0; /* number of methods on the stack */
    int *methods = nullptr; /* array of numstackframes method ids */
    int *bcis = nullptr;    /* array of numstackframes bytecode indices */
};

/*
 * Record that contains inlining information for each pc address of
 * an nmethod.
 */
class CompiledMethodLoadInlineRecord
{
public:
    int numpcs = 0;                /* number of pc descriptors in this nmethod */
    PCStackInfo *pcinfo = nullptr; /* array of numpcs pc descriptors */
    CompiledMethodLoadInlineRecord(const uint8_t *scopes_pc, uint32_t scopes_pc_size,
                                   const uint8_t *scopes_data, uint32_t scopes_data_size,
                                   uint64_t insts_begin);
    ~CompiledMethodLoadInlineRecord();
};

/* A section of contiguous memory loaded from a file. */
class JitSection
{
private:
    static std::atomic_int JitSectionCounter;

    int _id;

    /* description of jit codes */
    const uint8_t *_code;

    uint64_t _code_begin;

    uint64_t _stub_begin;

    uint64_t _entry_point;

    uint64_t _verified_entry_point;

    uint64_t _osr_entry_point;

    uint32_t _code_size;

    uint32_t _inline_method_cnt;

    std::map<int, const Method *> _methods;

    const Method *_mainm;

    CompiledMethodLoadInlineRecord *_record;

    const std::string _name;

public:
    JitSection(const uint8_t *code, uint64_t code_begin,
               uint64_t stub_begin, uint32_t code_size,
               const uint8_t *scopes_pc, uint32_t scopes_pc_size,
               const uint8_t *scopes_data, uint32_t scopes_data_size,
               uint64_t entry_point,
               uint64_t verified_entry_point,
               uint64_t osr_entry_point,
               uint32_t inline_method_cnt,
               std::map<int, const Method *> &methods,
               const Method *mainm, const std::string &name);

    ~JitSection();

    int id() const { return _id; }

    /* Read memory from a section.
     *
     * Reads at most @size bytes from @section with @vaddr in @buffer.  @section
     * must be mapped.
     *
     * Returns true, 0 otherwise
     *
     * size might be modified
     */
    bool read(uint8_t *buffer, uint8_t *size, uint64_t vaddr);

    /* find index of PCStackInfo, vaddr should be next instruction's addr */
    int find_pc(uint64_t vaddr);
    const PCStackInfo *get_pc(int idx);

    uint32_t code_size() const { return _code_size; }
    uint64_t code_begin() const { return _code_begin; }
    uint64_t entry_point() const { return _entry_point; }
    uint64_t verified_entry_point() const { return _verified_entry_point; }
    uint64_t osr_entry_point() const { return _osr_entry_point; }
    uint64_t stub_begin() const { return _stub_begin; }
    uint32_t inline_method_cnt() const { return _inline_method_cnt; }
    const Method *method(int idx) const
    {
        if (!_methods.count(idx))
            return nullptr;
        return _methods.at(idx);
    }
    const Method *mainm() const { return _mainm; }
    const CompiledMethodLoadInlineRecord *record() const { return _record; }
};

#endif /* JIT_SECTION_HPP */

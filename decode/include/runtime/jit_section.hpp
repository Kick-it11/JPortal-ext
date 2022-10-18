#ifndef JIT_SECTION_HPP
#define JIT_SECTION_HPP

#include "utilities/definitions.hpp"

#include <string>
#include <map>
#include <mutex>

using std::map;
using std::mutex;
using std::string;

class Method;
/*
 * Record that gives information about the methods on the compile-time
 * stack at a specific pc address of a compiled method. Each element in
 * the methods array maps to same element in the bcis array.
 */
struct PCStackInfo {
    address pc = 0;             /* the pc address for this compiled method */
    jint numstackframes = 0;  /* number of methods on the stack */
    jint* methods = nullptr;   /* array of numstackframes method ids */
    jint* bcis = nullptr;           /* array of numstackframes bytecode indices */
};

/*
 * Record that contains inlining information for each pc address of
 * an nmethod.
 */
struct CompiledMethodLoadInlineRecord {
    jint numpcs = 0;          /* number of pc descriptors in this nmethod */
    PCStackInfo* pcinfo = nullptr;  /* array of numpcs pc descriptors */
    CompiledMethodLoadInlineRecord(const uint8_t *scopes_pc, size_t scopes_pc_size,
                                   const uint8_t *scopes_data, size_t scopes_data_size,
                                   size_t insts_begin);
};

class CompiledMethodDesc {
private:
    address _entry_point;
    address _verified_entry_point;
    address _osr_entry_point;
    int _inline_method_cnt;
    const Method* _mainm;
    map<int, const Method*> _methods;
public:
    CompiledMethodDesc(address entry_point, address verified_entry_point,
                       address osr_entry_point, address inline_method_cnt,
                       const Method* mainm, map<int, const Method*>& methods) :
      _entry_point(entry_point),
      _verified_entry_point(verified_entry_point),
      _osr_entry_point(osr_entry_point),
      _inline_method_cnt(inline_method_cnt),
      _mainm(mainm), _methods(methods) {}

    const Method* mainm () const {
        return _mainm;
    }
    const Method* method(int id) const {
        auto iter = _methods.find(id);
        if (iter == _methods.end()) return nullptr;
        return iter->second;
    }
    uint64_t entry_point() const { return _entry_point; }
    uint64_t verified_entry_point() const { return _verified_entry_point; }
    uint64_t osr_entry_point() const { return _osr_entry_point; }
    int inline_method_cnt() const { return _inline_method_cnt; }
};

/* A section of contiguous memory loaded from a file. */
class JitSection {
private:
    /* description of jit codes */
    const uint8_t *_code;

    uint64_t _code_begin;

    uint64_t _code_size;

    CompiledMethodDesc *_cmd;
    CompiledMethodLoadInlineRecord *_record;

    const string _name;

public:
    JitSection(const uint8_t *code, uint64_t code_begin, uint64_t code_size, 
               const uint8_t *scopes_pc, size_t scopes_pc_size,
               const uint8_t *scopes_data, size_t scopes_data_size,
               CompiledMethodDesc *cmd, const string &name);

    ~JitSection();

    /* Read memory from a section.
     *
     * Reads at most @size bytes from @section with @vaddr in @buffer.  @section
     * must be mapped.
     *
     * Returns true, 0 otherwise
     * 
     * size might be modified
     */
    bool read(uint8_t *buffer, uint16_t *size, address vaddr);

    /* find PCStackInfo from _record
     * 
     * Return &pcinfo[i] when pcinfo[i].pc begin to surpass vaddr
     */
    PCStackInfo *find(address vaddr, int &idx);

    uint64_t code_size() const { return _code_size; }
    uint64_t code_begin() const { return _code_begin; }
    const CompiledMethodDesc *cmd() const { return _cmd; }
    const CompiledMethodLoadInlineRecord *record() const { return _record; }
};

#endif
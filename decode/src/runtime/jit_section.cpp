#include "runtime/jit_section.hpp"
#include "runtime/pc_desc.hpp"
#include "runtime/scope_desc.hpp"

#include <cassert>
#include <cstring>

CompiledMethodLoadInlineRecord::CompiledMethodLoadInlineRecord(const uint8_t *scopes_pc,
    uint32_t scopes_pc_size, const uint8_t *scopes_data, uint32_t scopes_data_size, uint64_t insts_begin) {
    if (!scopes_data_size || !scopes_pc_size)
        return;

    for (PcDesc *p = (PcDesc *)scopes_pc;
         p < (PcDesc *)(scopes_pc + scopes_pc_size);
         ++p) {
        /* 0: serialized_null */
        if (p->scope_decode_offset() == 0)
            continue;
        numpcs++;
    }

    pcinfo = new PCStackInfo[numpcs];
    int scope = 0;

    for (PcDesc *p = (PcDesc *)scopes_pc;
         p < (PcDesc *)(scopes_pc + scopes_pc_size); p++) {
        /* 0: serialized_null */
        if (p->scope_decode_offset() == 0)
            continue;
        uint64_t pc_address = p->real_pc(insts_begin);
        pcinfo[scope].pc = pc_address;
        int numstackframes = 0;
        ScopeDesc *sd =
            new ScopeDesc(p->scope_decode_offset(), p->obj_decode_offset(),
                          p->should_reexecute(), p->rethrow_exception(),
                          p->return_oop(), scopes_data);
        while (sd != NULL) {
            ScopeDesc *sd_p = sd->sender();
            delete sd;
            sd = sd_p;
            numstackframes++;
        }
        pcinfo[scope].methods = new jint[numstackframes];
        pcinfo[scope].bcis = new jint[numstackframes];
        pcinfo[scope].numstackframes = numstackframes;
        int stackframe = 0;
        sd = new ScopeDesc(p->scope_decode_offset(), p->obj_decode_offset(),
                           p->should_reexecute(), p->rethrow_exception(),
                           p->return_oop(), scopes_data);
        while (sd != NULL) {
            pcinfo[scope].methods[stackframe] = sd->method_index();
            pcinfo[scope].bcis[stackframe] = sd->bci();
            ScopeDesc *sd_p = sd->sender();
            delete sd;
            sd = sd_p;
            stackframe++;
        }
        scope++;
    }
}

JitSection::JitSection(const uint8_t *code, uint64_t code_begin, uint32_t code_size, 
                       const uint8_t *scopes_pc, uint32_t scopes_pc_size, 
                       const uint8_t *scopes_data, uint32_t scopes_data_size,
                       CompiledMethodDesc *cmd, const std::string &name) :
                       _code(code), _code_begin(code_begin), _code_size(code_size),
                       _cmd(cmd), _name(name) {
    assert(code != nullptr);

    _record = cmd ? new 
                CompiledMethodLoadInlineRecord(scopes_pc, scopes_pc_size,
                                               scopes_data, scopes_data_size,
                                               code_begin)
                  : nullptr;

}

JitSection::~JitSection() {
    delete _record;
    delete _cmd;
}

bool JitSection::read(uint8_t *buffer, uint16_t *size, uint64_t vaddr) {
    uint64_t offset = vaddr - _code_begin;
    uint64_t limit = _code_size;
    if (limit <= offset || !buffer || !size)
        return false;

    /* Truncate if we try to read past the end of the section. */
    uint64_t space = limit - offset;
    if (space < *size)
        *size = (uint16_t)space;

    memcpy(buffer, _code + offset, *size);
    return true;
}

PCStackInfo *JitSection::find(uint64_t vaddr, int &idx) {
    uint64_t begin = _code_begin;
    uint64_t end = begin + _code_size;

    if (vaddr < begin || vaddr >= end)
        return nullptr;

    for (int i = 0; i < _record->numpcs; i++) {
        if (vaddr < _record->pcinfo[i].pc) {
            idx = i;
            return &_record->pcinfo[i];
        }
    }
    return nullptr;
}

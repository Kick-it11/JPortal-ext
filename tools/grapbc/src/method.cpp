#include "../include/method.hpp"
#include "../include/bytecodes.hpp"

#include <string.h>
#include <cassert>

Method::Method(string name_signature, const u2 flags, const u1 *const code_start,
        const u2 code_length, Klass& klass) :
    _name_signature(name_signature), _flags(flags), _code_length(code_length), _klass(klass) {
    _code_start = new u1[_code_length];
    memcpy(_code_start, code_start, _code_length);
    _bctcode = (u1 *)malloc(_code_length * sizeof(u1));
    u1* _bytes_index = _bctcode;
    _bctcode_length = 0;
    for (u4 i = 0; i < _code_length;) {
        u1 ins = *(_code_start + i);
        *_bytes_index = ins;
        ++_bytes_index;
        int len = Bytecodes::length_for((Bytecodes::Code)ins);
        if (len == 0) {
            len = Bytecodes::special_length_at((Bytecodes::Code)ins, _code_start + i,
                                               i + 1);
        }
        i += len;
        ++_bctcode_length;
    }
}

const u1 Method::code_at(int idx) const {
    assert(idx >= 0 && idx < _bctcode_length);
    return *(_bctcode + idx);
}
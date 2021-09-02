#ifndef BUFFER_STREAM_HPP
#define BUFFER_STREAM_HPP

#include "type_defs.hpp"
#include <cassert>

// Read from Big-Endian stream on Little-Endian platform like x86
class BufferStream {
  private:
    u1 *_buffer_begin;
    u1 *_buffer_end;
    mutable u1 *_current;
    bool change;

  public:
    BufferStream(int length) {
        assert(length > 0);
        _buffer_begin = (u1 *)malloc(length);
        assert(_buffer_begin);
        _buffer_end = _buffer_begin + length;
        _current = _buffer_begin;
    }
    ~BufferStream() {
        free(_buffer_begin);
    }
    bool get_change() const { return change; }
    const u1 *current() const { return _current; }
    u1 *get_buffer_begin() { return _buffer_begin; }
    int get_offset() const { return _current - _buffer_begin; }
    // Read u1 from stream;
    u1 get_u1() const {
        u1 uc = *_current++;
        assert(_current <= _buffer_end);
        return uc;
    }

    // Read u2 from Big-Endian stream;
    u2 get_u2() const {
        u1 us[2];
        us[1] = *_current++;
        us[0] = *_current++;
        assert(_current <= _buffer_end);
        return *(u2 *)us;
    }

    // Read u4 from Big-Endian stream;
    u4 get_u4() const {
        u1 ui[4];
        ui[3] = *_current++;
        ui[2] = *_current++;
        ui[1] = *_current++;
        ui[0] = *_current++;
        assert(_current <= _buffer_end);
        return *(u4 *)ui;
    }

    // Read u8 from Big-Endian stream;
    u8 get_u8() const {
        u1 ul[8];
        ul[7] = *_current++;
        ul[6] = *_current++;
        ul[5] = *_current++;
        ul[4] = *_current++;
        ul[3] = *_current++;
        ul[2] = *_current++;
        ul[1] = *_current++;
        ul[0] = *_current++;
        assert(_current <= _buffer_end);
        return *(u8 *)ul;
    }

    // write u1 from stream;
    void set_u1(int offset, u1 uc) {
        u1 *buffer = _buffer_begin + offset + 1;
        assert (buffer > _buffer_begin && buffer < _buffer_end);
        *buffer++ = uc;
        change = true;
    }

    // write u2 from Big-Endian stream;
    void set_u2(int offset, u2 _us) {
        u1 *buffer = _buffer_begin + offset + 2;
        assert (buffer > _buffer_begin && buffer < _buffer_end);
        u1 *us = (u1 *)&_us;
        *buffer++ = us[1];
        *buffer++ = us[0];
        change = true;
    }

    // write u4 from Big-Endian stream;
    void set_u4(int offset, u4 _ui) {
        u1 *buffer = _buffer_begin + offset + 4;
        assert (buffer > _buffer_begin && buffer < _buffer_end);
        u1 *ui = (u1 *)&_ui;
        *buffer++ = ui[3];
        *buffer++ = ui[2];
        *buffer++ = ui[1];
        *buffer++ = ui[0];
        change = true;
    }

    // write u8 from Big-Endian stream;
    void set_u8(int offset, u8 _ul) {
        u1 *buffer = _buffer_begin + offset + 8;
        assert (buffer > _buffer_begin && buffer < _buffer_end);
        u1 *ul = (u1 *)&_ul;
        *buffer++ = ul[7];
        *buffer++ = ul[6];
        *buffer++ = ul[5];
        *buffer++ = ul[4];
        *buffer++ = ul[3];
        *buffer++ = ul[2];
        *buffer++ = ul[1];
        *buffer++ = ul[0];
        change = true;
    }

    // Skip length u1 or u2 elements from stream
    void skip_u1(int length) const { _current += length; }

    void skip_u2(int length) const { _current += 2 * length; }

    void skip_u4(int length) const { _current += 4 * length; }

    // Tells whether eos is reached
    bool at_eos() const { return _current == _buffer_end; }
};

#endif // BUFFER_STREAM_HPP
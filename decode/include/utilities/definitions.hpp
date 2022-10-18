#ifndef DEFINITIONS_HPP
#define DEFINITIONS_HPP

#include <stdint.h>

// Java type definitions

typedef int32_t         jint;
typedef int64_t         jlong;
typedef signed char     jbyte;

typedef unsigned char   jboolean;
typedef unsigned short  jchar;
typedef short           jshort;
typedef float           jfloat;
typedef double          jdouble;

typedef uint8_t         jubyte;
typedef uint16_t        jushort;
typedef uint32_t        juint;
typedef uint64_t        julong;

typedef jubyte          u1;
typedef jushort         u2;
typedef juint           u4;
typedef julong          u8;

typedef uint64_t        address;

typedef unsigned char   u_char;

inline jint alignup(jint offset) {
    jint alignup = offset % 4;
    if (alignup)
        return (4 - alignup);
    return 0;
}

#endif // DEFINITIONS_HPP

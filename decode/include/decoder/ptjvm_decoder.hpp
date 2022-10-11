#ifndef PT_JVM_DECODER_HPP
#define PT_JVM_DECODER_HPP

#include "utilities/definitions.hpp"

#include <list>
#include <map>

using std::list;
using std::map;

class Analyser;
class TraceDataRecord;

struct TracePart {
  bool loss = false;
  uint8_t *pt_buffer = 0;
  size_t pt_size = 0;
  uint8_t *sb_buffer = 0;
  size_t sb_size = 0;
};

extern int ptjvm_decode(TracePart tracepart, TraceDataRecord record,
                          Analyser *analyser);

extern int ptjvm_split(const char *trace_data, map<int, list<TracePart>> &splits);

#endif
#ifndef PT_JVM_DECODER_HPP
#define PT_JVM_DECODER_HPP

#include <list>
using namespace std;

class Analyser;
class TraceDataRecord;

struct TracePart {
  bool loss = false;
  uint8_t *pt_buffer = 0;
  size_t pt_size = 0;
};

extern int ptjvm_decode(TracePart tracepart, bool dump);

extern int ptjvm_split(const char *trace_data, map<int, list<TracePart>> &splits);

#endif
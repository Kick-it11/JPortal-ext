#ifndef TRACE_SPLITTER
#define TRACE_SPLITTER

#include <map>
#include <list>

using std::map;
using std::list;


int ptjvm_split(const char *trace_data, map<int, list<TracePart>> &splits);

#endif

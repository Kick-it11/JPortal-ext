#ifndef _decode_output_hpp
#define _decode_output_hpp

#include <list>
using namespace std;

class Analyser;
class TraceData;

void decode_output(Analyser* analyser, list<TraceData*> &traces);

#endif 

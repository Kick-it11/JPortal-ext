#include <thread>
#include <iostream>
#include <cstring>

#include "thread/thread_pool.hpp"
#include "java/analyser.hpp"
#include "decoder/decode_result.hpp"
#include "decoder/pt_jvm_decoder.hpp"
#include "runtime/jvm_runtime.hpp"
#include "decoder/output_decode.hpp"
#include "decoder/trace_splitter.hpp"

using std::cout;
using std::endl;

static void decode_part(TracePart part, Analyser* analyser, TraceData &trace) {
    PTJVMDecoder decoder(part, analyser);
    TraceDataRecord record(trace);
    decoder.decode(record);
}

static void decode(const char *trace_data, Analyser* analyser, list<TraceData*> &traces) {
    TraceSplitter splitter(trace_data);
    TracePart part;
    ThreadPool pool(16, 32);

    while (splitter.next(part)) {
        TraceData* trace = new TraceData();
        traces.push_back(trace);
        pool.submit(decode_part, part, analyser, *trace);
    }
}

int main(int argc, char **argv) {
    char defualt_trace[20] = "JPortalTrace.data";
    char default_dump[20] = "JPortalDump.data"; 
    char *dump_data = default_dump;
    char *trace_data = defualt_trace;
    list<string> class_paths;
    int errcode, i;
    for (i = 1; i < argc;) {
        char *arg;
        arg = argv[i++];

        if (strcmp(arg, "--trace-data") == 0) {
            if (argc <= i) {
                return -1;
            }
            trace_data = argv[i++];
            continue;
        }

        if (strcmp(arg, "--dump-data") == 0) {
            if (argc <= i) {
                return -1;
            }
            dump_data = argv[i++];
            continue;
        }

        if (strcmp(arg, "--class-path") == 0) {
            if (argc <= i) {
                return -1;
            }
            class_paths.push_back(argv[i++]);
            continue;
        }

        fprintf(stderr, "unknown:%s\n", arg);
        return -1;
    }

    if (!trace_data) {
        fprintf(stderr, "Please specify trace data:--trace-data\n");
        return -1;
    }
    if (!dump_data) {
        fprintf(stderr, "Please specify dump data:--trace-data\n");
        return -1;
    }
    if (class_paths.empty()) {
        fprintf(stderr, "Please specify class path:--class-path\n");
        return -1;
    }
    list<TraceData*> traces;
    ///* Initializing *///
    cout<<"Initializing..."<<endl;
    Bytecodes::initialize();
    Analyser* analyser = new Analyser(class_paths);

    JVMRuntime::initialize(dump_data, analyser);
    cout<<"Initializing completed."<<endl;

    ///* Decoding *///
    cout<<"Decoding..."<<endl;
    decode(trace_data, analyser, traces);
    cout<<"Decoding completed."<<endl;

    ///* Output Decode *///
    output_decode(traces);

    ///* Exit *///
    for (auto trace : traces) {
        delete trace;
    }

    JVMRuntime::destroy();
    delete analyser;

    return 0;
}

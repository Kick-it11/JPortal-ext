#include <thread>
#include <iostream>
#include <cstring>

#include "thread/thread_pool.hpp"
#include "java/analyser.hpp"
#include "decoder/decode_result.hpp"
#include "decoder/pt_jvm_decoder.hpp"
#include "runtime/jvm_runtime.hpp"
#include "decoder/output_decode.hpp"
#include "tracedata/trace_data_parser.hpp"

using std::cout;
using std::endl;

static void decode_part(struct pt_config config, uint32_t cpu, TraceData* trace) {
    PTJVMDecoder decoder(config, *trace, cpu);
    decoder.decode();
    delete[] config.begin;
}

static void decode(const string &file, list<string>& paths) {
    TraceDataParser parser(file);
    list<TraceData*> traces;

    /** Initialize */
    cout << "Initializing..." << endl;
    Analyser* analyser = new Analyser(paths);
    auto jvmdata = parser.jvm_runtime_data();
    auto sideband_data = parser.sideband_data();
    Bytecodes::initialize();
    JVMRuntime::initialize(jvmdata.first, jvmdata.second, analyser);
    Sideband::initialize(sideband_data, parser.sample_type(), parser.time_mult(),
                         parser.time_shift(), parser.time_zero());

    /** Decoding */
    cout << "Decoding..." << endl;
    std::pair<uint8_t*, uint64_t> pt_data;
    uint32_t cpu;
    ThreadPool pool(16, 32);
    while (parser.next_pt_data(pt_data, cpu)) {
        TraceData* trace = new TraceData();
        traces.push_back(trace);
        struct pt_config config;
        parser.init_pt_config_from_trace(config);
        config.begin = pt_data.first;
        config.end = pt_data.first + pt_data.second;
        pool.submit(decode_part, config, cpu, trace);
    }
    pool.shutdown();

    /** Output */
    cout << "Output..." << endl;
    output_decode(traces);

    /* Exit */
    JVMRuntime::destroy();
    Sideband::destroy();
    delete analyser;
    for (auto trace : traces)
        delete trace;
    traces.clear();
}

int main(int argc, char **argv) {
    char defualt_trace[20] = "JPortalTrace.data";
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

    decode(trace_data, class_paths);

    return 0;
}

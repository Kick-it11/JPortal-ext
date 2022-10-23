#include "decoder/output_decode.hpp"
#include "decoder/pt_jvm_decoder.hpp"
#include "java/analyser.hpp"
#include "runtime/jvm_runtime.hpp"
#include "sideband/sideband.hpp"
#include "thread/thread_pool.hpp"
#include "tracedata/trace_data_parser.hpp"

#include <cstring>
#include <iostream>
#include <thread>

static void decode_part(struct pt_config config, uint32_t cpu, TraceData *trace)
{
    PTJVMDecoder decoder(config, *trace, cpu);
    decoder.decode();
    delete[] config.begin;
}

static void decode(const std::string &file, std::list<std::string> &paths)
{
    TraceDataParser parser(file);
    std::list<TraceData *> traces;

    /** Initialize */
    std::cout << "Initializing..." << std::endl;
    Analyser *analyser = new Analyser(paths);
    auto jvmdata = parser.jvm_runtime_data();
    auto sideband_data = parser.sideband_data();
    Bytecodes::initialize();
    JVMRuntime::initialize(jvmdata.first, jvmdata.second, analyser);
    Sideband::initialize(sideband_data, parser.sample_type(), parser.time_mult(),
                         parser.time_shift(), parser.time_zero());

    /** Decoding */
    std::cout << "Decoding..." << std::endl;
    std::pair<uint8_t *, uint64_t> pt_data;
    uint32_t cpu;
    ThreadPool pool(16, 32);
    while (parser.next_pt_data(pt_data, cpu))
    {
        TraceData *trace = new TraceData();
        traces.push_back(trace);
        struct pt_config config;
        parser.init_pt_config_from_trace(config);
        config.begin = pt_data.first;
        config.end = pt_data.first + pt_data.second;
        pool.submit(decode_part, config, cpu, trace);
    }
    pool.shutdown();

    /** Output */
    std::cout << "Output..." << std::endl;
    output_decode(traces);

    /* Exit */
    JVMRuntime::destroy();
    Sideband::destroy();
    delete analyser;
    for (auto trace : traces)
        delete trace;
    traces.clear();
}

static void show(const std::string &file, const std::string &info, std::list<std::string> &paths) {
    TraceDataParser parser(file);
    if (info == "sideband")
    {
        auto sideband_data = parser.sideband_data();
        Sideband::initialize(sideband_data, parser.sample_type(), parser.time_mult(),
                             parser.time_shift(), parser.time_zero());
        Sideband::print();
        Sideband::destroy();
    }
    else if (info == "jvm")
    {
        auto jvmdata = parser.jvm_runtime_data();
        Analyser *analyser = new Analyser(paths);
        JVMRuntime::initialize(jvmdata.first, jvmdata.second, analyser);
        JVMRuntime::print();
        JVMRuntime::destroy();
        delete analyser;
    }
    else
    {
        std::cerr << "decode error: Unknown show info" << std::endl;
    }
}

int main(int argc, char **argv)
{
    std::string trace_data = "JPortalTrace.data";
    std::list<std::string> class_paths;
    for (int i = 1; i < argc;)
    {
        std::string arg = argv[i++];
        if (arg == "-d")
        {
            if (argc <= i)
            {
                std::cerr << "decode error: Missing tracedata -d" << std::endl;
                return -1;
            }
            trace_data = argv[i++];
            continue;
        }

        if (arg == "-c")
        {
            if (argc <= i)
            {
                std::cerr << "decode error: Missing class path -c" << std::endl;
                return -1;
            }
            class_paths.push_back(argv[i++]);
            continue;
        }

        if (arg == "-h")
        {
            std::cout << "decode -d $file [-c $path]+  :decode $file with classfile path $path" << std::endl;
            std::cout << "       -s sideband/jvm       :show sideband/jvm info(-d -c needed before)" << std::endl;
            std::cout << "       -h                    :print this" << std::endl;
            return 0;
        }

        if (arg == "-s")
        {
             if (argc <= i)
            {
                std::cerr << "decode error: Missing s info" << std::endl;
                return -1;
            }
            arg = argv[i++];
            show(trace_data, arg, class_paths);
            return 0;
        }

        std::cerr << "decode error: Unknown arguments" << std::endl;
        return -1;
    }

    decode(trace_data, class_paths);

    return 0;
}

#include <thread>
#include <iostream>
#include <cstring>

#include "task/task.hpp"
#include "task/worker.hpp"
#include "task/task_manager.hpp"
#include "java/analyser.hpp"
#include "decoder/decode_result.hpp"
#include "decoder/ptjvm_decoder.hpp"
#include "runtime/jvm_runtime.hpp"
#include "output/output_decode.hpp"

using std::thread;
using std::cout;
using std::endl;

class DecodeTask: public Task {
private:
    const char *trace_data;
    TracePart tracepart;
    TraceData &trace;
    Analyser *analyser;

public:
    DecodeTask(TracePart _tracepart, TraceData &_trace,
                Analyser *_analyser) :
                    Task(TaskKind::DECODETASK),
                    tracepart(_tracepart),
                    trace(_trace),
                    analyser(_analyser) {}
protected:
    Task* doTask() {
        ptjvm_decode(tracepart, TraceDataRecord(trace), analyser);
        free(tracepart.pt_buffer);
        free(tracepart.sb_buffer);
        return nullptr;
    };
};

void decode(const char *trace_data, Analyser* analyser, list<TraceData*> &traces) {
    if (!trace_data || !analyser)
        return;

    map<int, list<TracePart>> traceparts;
    int errcode;
    errcode = ptjvm_split(trace_data, traceparts);
    if (errcode < 0)
        return;

    const int MaxThreadCount = 8;
    bool ThreadState[MaxThreadCount]{false};
    ///* Create TaskManager *///
    TaskManager* tm = new TaskManager();

    for (auto part1 : traceparts) {
        for (auto part2 : part1.second) {
            TraceData *trace = new TraceData();
            TraceDataRecord record(*trace);
            traces.push_back(trace);
            
            DecodeTask *task = new DecodeTask(part2, *trace, analyser);
            tm->commitTask(task);
        }
    }
    ///* Create Workers *///
    Worker w(&(ThreadState[0]), tm);
    new thread(w);

    while (true) {
        int worker_count = 0;
        for (int i = 0; i < MaxThreadCount; ++i) {
            if (ThreadState[i]) {
                ++worker_count;
            } else if (tm->isNeedMoreWorker()) {
                Worker w(&(ThreadState[i]), tm);
                new thread(w);
                ++worker_count;
            }
        }
        if (worker_count==0)
            break;
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

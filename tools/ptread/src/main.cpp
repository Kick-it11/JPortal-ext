#include <thread>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <map>

#include "../include/ptjvm_decoder.hpp"

void decode(const char *trace_data, bool dump) {
    map<int, list<TracePart>> traceparts;
    int errcode;
    errcode = ptjvm_split(trace_data, traceparts);
    if (errcode < 0)
        return;

    for (auto part1 : traceparts) {
        printf("##############CPU: %d\n", part1.first);
        for (auto part2 : part1.second) {
            printf("************PART:(%d) %ld\n", part2.loss, part2.pt_size);
            ptjvm_decode(part2, dump);
        }
    }
}

int main(int argc, char **argv) {
    char defualt_trace[20] = "JPortalTrace.data";
    char *trace_data = defualt_trace;
    int errcode, i;
    bool dump = false;
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

        if (strcmp(arg, "--dump") == 0) {
            dump = true;
            continue;
        }

        fprintf(stderr, "unknown:%s\n", arg);
        return -1;
    }

    if (!trace_data) {
        fprintf(stderr, "Please specify trace data:--trace-data\n");
        return -1;
    }

    ///* Decoding *///
    decode(trace_data, dump);

    return 0;
}
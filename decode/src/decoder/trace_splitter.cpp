#include "decoder/pt_jvm_decoder.hpp"
#include "decoder/decode_result.hpp"
#include "runtime/jvm_runtime.hpp"
#include "runtime/jit_image.hpp"
#include "runtime/jit_section.hpp"
#include "insn/pt_insn.hpp"
#include "insn/pt_ild.hpp"
#include "decoder/trace_splitter.hpp"
#include "pt/pt.hpp"

#include <iostream>
#include <fstream>

using std::cerr;
using std::endl;
using std::ifstream;
using std::ios;
using std::ios_base;
using std::streampos;

#define PERF_RECORD_AUXTRACE 71

TraceHeader trace_header;

struct AUXTraceEvent
{
    uint64_t size;
    uint64_t offset;
    uint64_t reference;
    uint32_t idx;
    uint32_t tid;
    uint32_t cpu;
    uint32_t reservered__; /* for alignment */
};


void TraceSplitter::parse_sample_size() {
    /** parse perf sample information */
    uint64_t sample_type = trace_header.sample_type;

    if (sample_type & PERF_SAMPLE_TID) {
        sample_size += 8;
    }

    if (sample_type & PERF_SAMPLE_TIME) {
        sample_size += 8;
    }

    if (sample_type & PERF_SAMPLE_ID) {
        sample_size += 8;
    }

    if (sample_type & PERF_SAMPLE_STREAM_ID) {
        sample_size += 8;
    }

    if (sample_type & PERF_SAMPLE_CPU) {
        cpu_offset = sample_size;
        sample_size += 8;
    } else {
        cerr << "TraceSplitter error: Miss perf sample cpu." << endl;
        exit(-1);
    }

    if (sample_type & PERF_SAMPLE_IDENTIFIER) {
        sample_size += 8;
    }
}

void TraceSplitter::parse_sideband_data(map<int, list<pair<uint64_t, uint64_t>>> &offsets) {
    
}

uint64_t TraceSplitter::sync_forward(uint8_t *buffer, uint64_t buffer_size, int &number) {
    struct pt_config config;
    pt_config_init(&config);

    config.cpu.vendor = (pt_cpu_vendor)trace_header.vendor;
    config.cpu.family = trace_header.family;
    config.cpu.model = trace_header.model;
    config.cpu.stepping = trace_header.stepping;
    config.mtc_freq = trace_header.mtc_freq;
    config.nom_freq = trace_header.nom_freq;
    config.cpuid_0x15_eax = trace_header.cpuid_0x15_eax;
    config.cpuid_0x15_ebx = trace_header.cpuid_0x15_ebx;
    config.addr_filter.config.addr_cfg = pt_addr_cfg_filter;
    config.addr_filter.addr0_a = trace_header.addr0_a;
    config.addr_filter.addr0_b = trace_header.addr0_b;
    config.flags.variant.query.keep_tcal_on_ovf = 1;
    if (pt_cpu_errata(&config.errata, &config.cpu) < 0) {
        cerr << "TraceSplitter error: Could not parse cpu errate" << endl;
        exit(-1);
    }

    /** Segment is not a complete trace but totally fine to find sync points */
    config.begin = buffer;
    config.end = buffer + buffer_size;
    struct pt_packet_decoder *pkt = pt_pkt_alloc_decoder(&config);
    if (!pkt) {
        cerr << "TraceSplitter error: Failt to allocate packet decoder" << endl;
        return buffer_size;
    }

    uint64_t offset = buffer_size;
    for (;;) {
        int errcode;
        errcode = pt_pkt_sync_forward(pkt);
        if (errcode < 0) {
            if (errcode = -pte_eos) {
                break;
            }
            cerr << "TraceSplitter error: Fail to sync " << pt_errstr(pt_errcode(errcode)) << endl;
        }

        ++number;
        if (number >= _sync_split_number) {
            errcode = pt_pkt_get_sync_offset(pkt, &offset);
            if (errcode < 0)
                cerr << "TraceSplitter error: Fail to sync " << pt_errstr(pt_errcode(errcode)) << endl;
        }
    }
    return offset;
}

bool TraceSplitter::next(TracePart &part) {
    if (_pt_offsets.empty())
        return false;

    uint32_t cpu = _pt_offsets.begin()->first;
    auto &&offs = _pt_offsets.begin()->second;

    if (offs.empty()) {
        /* Should not reach here, since if empty, shoud be erased before*/
        cerr << "TraceSplitter error: PT data empty" << endl;
        return false;
    }

    int number = 0;
    auto dest = offs.begin();
    uint64_t offset = 0;
    uint64_t size = 0;

    ifstream file(_filename, ios::binary);
    if (!file.is_open()) {
        cerr << "TraceSplitter error: Fail to open tracefile " << _filename << endl;
        exit(-1);
    }

    /** Find _split_sync_number sync points or reach to end */
    for (; dest != offs.end(); ++dest) {
        uint64_t buffer_size = dest->second-dest->first;
        uint8_t* buffer = new uint8_t[buffer_size]();
        file.seekg(dest->first, ios_base::beg);
        file.read((char *)buffer, buffer_size);

        offset = sync_forward(buffer, buffer_size, number);
        size += offset;
        if (number >= _sync_split_number)
            break;

        delete[] buffer;
    }

    /** load splitted pt data to pt_buffer */
    uint8_t* pt_buffer = new uint8_t[size];
    uint8_t* current = pt_buffer;
    auto it = offs.begin();
    while (it != dest) {
        file.seekg(it->first, ios_base::beg);
        file.read((char *)current, it->second);

        current += it->second;
        it = offs.erase(it);
    }
    if (it != offs.end()) {
        file.seekg(it->first, ios_base::beg);
        file.read((char *)current, offset);

        /** last offset */
        current += offset;
        it->first += offset;
    } else {
        _pt_offsets.erase(cpu);
    }

    part.pt = pt_buffer;
    part.pt_size = size;
    if (_sideband_data.count(cpu)) {
        part.sideband_size = _sideband_data[cpu].second;
        part.sideband = new uint8_t[part.sideband_size]();
        memcpy(part.sideband, _sideband_data[cpu].first, part.sideband_size);
    }
    return true;
}

void TraceSplitter::parse() {
    ifstream file(_filename, ios::binary);
    if (!file.is_open()) {
        cerr << "TraceSplitter error: Fail to open tracefile " << _filename << endl;
        exit(-1);
    }

    file.read((char *)&trace_header, sizeof(trace_header));
    if (trace_header.header_size != sizeof(trace_header)) {
        cerr << "TraceSplitter error: Trace Header does not match" << endl;
        exit(-1);
    }

    map<int, list<pair<uint64_t, uint64_t>>> sideband_offsets;
    while (!file.eof()) {
        struct perf_event_header header;
        int errcode;

        streampos begin = file.tellg();

        file.read((char *)&header, sizeof(header));

        if (header.type == PERF_RECORD_AUXTRACE) {
            /** self defined PERF_RECORD_AUXTRACE event for AUX data(PT) */
            struct AUXTraceEvent aux;
            file.read((char *)&aux, sizeof(aux));

            /*PT data begin here */
            begin = file.tellg();

            /** cpu number */
            uint32_t cpu = aux.cpu;

            /** aux file offset */
            streampos pos = file.tellg();

            /* skip aux data */
            file.seekg(aux.size, ios_base::cur);

            _pt_offsets[cpu].push_back({begin, file.tellg()});

        } else {
            uint32_t cpu;
            /* skip data directly to perf_sample_cpu */            
            file.seekg(header.size-sizeof(header)-sample_size+cpu_offset, ios_base::cur);

            /** read cpu number */
            file.read((char*)&cpu, sizeof(cpu));

            /** skip */
            file.seekg(sample_size-sizeof(cpu)-cpu_offset, ios_base::cur);

            sideband_offsets[cpu].push_back({begin, file.tellg()});
        }
    }

    /** parse sideband data */
    for (auto && cpu : sideband_offsets) {
        uint64_t size = 0;
        for (auto & off : cpu.second) {
            size += (off.second - off.first);
        }
        uint8_t *data = new uint8_t[size]();
        uint8_t *buf = data;
        for (auto & off : cpu.second) {
            file.seekg(off.first, ios_base::beg);
            file.read((char *)buf, off.second-off.first);
            buf += (off.second - off.first);
        }
        _sideband_data[cpu.first] = {data, size};
    }
}

TraceSplitter::TraceSplitter(const string &filename)
    : _filename(filename), _sync_split_number(_default_sync_split_number) {
    parse();
}

TraceSplitter::TraceSplitter(const string &filename, const int split_number)
    : _filename(filename), _sync_split_number(split_number) {
    parse();
}

TraceSplitter::~TraceSplitter() {
    for (auto sideband : _sideband_data) {
            delete[] sideband.second.first;
    }
    _sideband_data.clear();
}

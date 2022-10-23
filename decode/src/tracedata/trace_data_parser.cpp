#include "pt/pt.hpp"
#include "tracedata/trace_data_parser.hpp"

#include <linux/perf_event.h>

#include <iostream>
#include <fstream>

#define PERF_RECORD_AUXTRACE 71
#define PERF_RECORD_JVMRUNTIME 72

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

struct JVMRuntimeEvent
{
    uint64_t size;
};

bool TraceDataParser::parse()
{
    std::ifstream file(_filename, std::ios::binary);
    if (!file.is_open())
    {
        std::cerr << "TraceDataParser error: Fail to open tracefile " << _filename << std::endl;
        exit(-1);
    }

    file.read((char *)&_trace_header, sizeof(TraceDataHeader));
    if (_trace_header.header_size != sizeof(TraceDataHeader))
    {
        std::cerr << "TraceDataParser error: Trace Header does not match" << std::endl;
        exit(-1);
    }

    /** parse sample size and cpu_offset */
    uint64_t sample_size = 0ULL;
    uint64_t cpu_offset = 0;
    uint32_t sample_type = _trace_header.sample_type;
    if (sample_type & PERF_SAMPLE_TID)
        sample_size += 8;
    if (sample_type & PERF_SAMPLE_TIME)
        sample_size += 8;
    if (sample_type & PERF_SAMPLE_ID)
        sample_size += 8;
    if (sample_type & PERF_SAMPLE_STREAM_ID)
        sample_size += 8;
    if (sample_type & PERF_SAMPLE_CPU)
    {
        cpu_offset = sample_size;
        sample_size += 8;
    }
    else
    {
        std::cerr << "TraceDataParser error: Miss perf sample cpu" << std::endl;
        exit(-1);
    }
    if (sample_type & PERF_SAMPLE_IDENTIFIER)
        sample_size += 8;

    uint64_t parse_size = sizeof(TraceDataHeader);
    /** parse pt, sideband, jvmruntime, offsets */
    for (;;)
    {
        struct perf_event_header header;
        std::streampos begin = file.tellg();

        if (!file.read((char *)&header, sizeof(header)))
            break;

        if (header.type == PERF_RECORD_AUXTRACE)
        {
            /** self defined PERF_RECORD_AUXTRACE event for AUX data(PT) */
            struct AUXTraceEvent aux;
            if (!file.read((char *)&aux, sizeof(aux)))
                return false;

            /*PT data begin here */
            uint64_t pt_begin = file.tellg();
            if (pt_begin < 0)
                return false;

            /** cpu number */
            uint32_t cpu = aux.cpu;

            /* skip aux data */
            if (!file.seekg(aux.size, std::ios_base::cur))
                return false;

            _pt_offsets[cpu].push_back({pt_begin, file.tellg()});
        }
        else if (header.type == PERF_RECORD_JVMRUNTIME)
        {
            /** self defined PERF_RECORD_JVMRuntime event */
            struct JVMRuntimeEvent jvm;
            if (!file.read((char *)&jvm, sizeof(jvm)))
                return false;

            /*data begin here */
            uint64_t jvm_begin = file.tellg();
            if (jvm_begin < 0)
                return false;

            /* skip*/
            if (!file.seekg(jvm.size, std::ios_base::cur))
                return false;

            _jvm_runtime_offsets.push_back({jvm_begin, file.tellg()});
        }
        else
        {
            uint32_t cpu;
            /* skip data directly to perf_sample_cpu */
            if (!file.seekg(header.size - sizeof(header) - sample_size + cpu_offset, std::ios_base::cur))
                return false;

            /** read cpu number */
            if (!file.read((char *)&cpu, sizeof(cpu)))
                return false;

            /** skip */
            if (!file.seekg(sample_size - sizeof(cpu) - cpu_offset, std::ios_base::cur))
                return false;

            _sideband_offsets[cpu].push_back({begin, file.tellg()});
        }
        parse_size += (file.tellg() - begin);
    }

    std::cout << "TraceDataParser: parse file " << _filename << " " << parse_size << std::endl;

    return true;
}

TraceDataParser::TraceDataParser(const std::string &filename)
    : _filename(filename), _sync_split_number(_default_sync_split_number)
{
    if (!parse())
    {
        std::cerr << "TraceDataParser error: False format" << std::endl;
        exit(-1);
    }
    resplit_pt_data();
}

TraceDataParser::TraceDataParser(const std::string &filename, const int split_number)
    : _filename(filename), _sync_split_number(split_number)
{
    if (!parse())
    {
        std::cerr << "TraceDataParser error: False format" << std::endl;
        exit(-1);
    }
    resplit_pt_data();
}

TraceDataParser::~TraceDataParser()
{
}

std::map<uint32_t, std::pair<uint8_t *, uint64_t>> TraceDataParser::sideband_data()
{
    /** parse sideband data */
    std::ifstream file(_filename, std::ios::binary);
    if (!file.is_open())
    {
        std::cerr << "TraceDataParser error: Fail to open tracefile " << _filename << std::endl;
        exit(-1);
    }
    std::map<uint32_t, std::pair<uint8_t *, uint64_t>> ans;
    for (auto &&cpu : _sideband_offsets)
    {
        uint64_t size = 0;
        for (auto &off : cpu.second)
        {
            size += (off.second - off.first);
        }
        uint8_t *data = new uint8_t[size];
        uint8_t *buf = data;
        for (auto &off : cpu.second)
        {
            file.seekg(off.first, std::ios_base::beg);
            file.read((char *)buf, off.second - off.first);
            buf += (off.second - off.first);
        }
        ans[cpu.first] = {data, size};
    }
    return ans;
}

std::pair<uint8_t *, uint64_t> TraceDataParser::jvm_runtime_data()
{
    /** parse jvm runtime */
    std::ifstream file(_filename, std::ios::binary);
    if (!file.is_open())
    {
        std::cerr << "TraceDataParser error: Fail to open tracefile " << _filename << std::endl;
        exit(-1);
    }
    std::pair<uint8_t *, uint64_t> ans;
    uint64_t size = 0;
    for (auto &&off : _jvm_runtime_offsets)
    {
        size += (off.second - off.first);
    }
    uint8_t *data = new uint8_t[size];
    uint8_t *buf = data;
    for (auto &off : _jvm_runtime_offsets)
    {
        file.seekg(off.first, std::ios_base::beg);
        file.read((char *)buf, off.second - off.first);
        buf += (off.second - off.first);
    }
    return {data, size};
}

std::map<uint32_t, std::pair<uint8_t *, uint64_t>> TraceDataParser::pt_data()
{
    /** parse sideband data */
    std::ifstream file(_filename, std::ios::binary);
    if (!file.is_open())
    {
        std::cerr << "TraceDataParser error: Fail to open tracefile " << _filename << std::endl;
        exit(-1);
    }
    std::map<uint32_t, std::pair<uint8_t *, uint64_t>> ans;
    for (auto &&cpu : _pt_offsets)
    {
        uint64_t size = 0;
        for (auto &off : cpu.second)
        {
            size += (off.second - off.first);
        }
        uint8_t *data = new uint8_t[size];
        uint8_t *buf = data;
        for (auto &off : cpu.second)
        {
            file.seekg(off.first, std::ios_base::beg);
            file.read((char *)buf, off.second - off.first);
            buf += (off.second - off.first);
        }
        ans[cpu.first] = {data, size};
    }
    return ans;
}

void TraceDataParser::resplit_pt_data()
{
    _split_pt_offsets = _pt_offsets;
}

uint64_t TraceDataParser::sync_forward(uint8_t *buffer, uint64_t buffer_size, int &number)
{
    struct pt_config config;

    init_pt_config_from_trace(config);

    /** Segment is not a complete trace but totally fine to find sync points */
    config.begin = buffer;
    config.end = buffer + buffer_size;
    struct pt_packet_decoder *pkt = pt_pkt_alloc_decoder(&config);
    if (!pkt)
    {
        std::cerr << "TraceDataParser error: Failt to allocate packet decoder" << std::endl;
        return buffer_size;
    }

    uint64_t offset = buffer_size;
    for (;;)
    {
        int errcode;
        errcode = pt_pkt_sync_forward(pkt);
        if (errcode < 0)
        {
            if (errcode = -pte_eos)
            {
                break;
            }
            std::cerr << "TraceDataParser error: Fail to sync " << pt_errstr(pt_errcode(errcode)) << std::endl;
        }

        ++number;
        if (number >= _sync_split_number)
        {
            errcode = pt_pkt_get_sync_offset(pkt, &offset);
            if (errcode < 0)
                std::cerr << "TraceDataParser error: Fail to sync " << pt_errstr(pt_errcode(errcode)) << std::endl;
        }
    }

    pt_pkt_free_decoder(pkt);

    return offset;
}

bool TraceDataParser::next_pt_data(std::pair<uint8_t *, uint64_t> &part_data, uint32_t &cpu)
{
    if (_split_pt_offsets.empty())
        return false;

    cpu = _split_pt_offsets.begin()->first;
    auto &&offs = _split_pt_offsets.begin()->second;

    if (offs.empty())
    {
        /* Should not reach here, since if empty, shoud be erased before*/
        std::cerr << "TraceDataParser error: PT data empty" << std::endl;
        return false;
    }

    int number = 0;
    auto dest = offs.begin();
    uint64_t offset = 0;
    uint64_t size = 0;

    std::ifstream file(_filename, std::ios::binary);
    if (!file.is_open())
    {
        std::cerr << "TraceDataParser error: Fail to open tracefile " << _filename << std::endl;
        exit(-1);
    }

    /** Find _split_sync_number sync points or reach to end */
    for (; dest != offs.end(); ++dest)
    {
        uint64_t buffer_size = dest->second - dest->first;
        uint8_t *buffer = new uint8_t[buffer_size];
        file.seekg(dest->first, std::ios_base::beg);
        file.read((char *)buffer, buffer_size);

        offset = sync_forward(buffer, buffer_size, number);
        size += offset;
        if (number >= _sync_split_number)
            break;

        delete[] buffer;
    }

    /** load splitted pt data to pt_buffer */
    uint8_t *pt_buffer = new uint8_t[size];
    uint8_t *current = pt_buffer;
    auto it = offs.begin();
    while (it != dest)
    {
        uint64_t part_size = it->second - it->first;
        file.seekg(it->first, std::ios_base::beg);
        file.read((char *)current, part_size);

        current += part_size;
        it = offs.erase(it);
    }
    if (it != offs.end())
    {
        file.seekg(it->first, std::ios_base::beg);
        file.read((char *)current, offset);

        /** last offset */
        current += offset;
        it->first += offset;
    }
    else
    {
        _split_pt_offsets.erase(cpu);
    }

    part_data.first = pt_buffer;
    part_data.second = size;
    return true;
}

void TraceDataParser::init_pt_config_from_trace(struct pt_config &config)
{
    pt_config_init(&config);

    config.cpu.vendor = (pt_cpu_vendor)_trace_header.vendor;
    config.cpu.family = _trace_header.family;
    config.cpu.model = _trace_header.model;
    config.cpu.stepping = _trace_header.stepping;
    config.mtc_freq = _trace_header.mtc_freq;
    config.nom_freq = _trace_header.nom_freq;
    config.cpuid_0x15_eax = _trace_header.cpuid_0x15_eax;
    config.cpuid_0x15_ebx = _trace_header.cpuid_0x15_ebx;
    config.addr_filter.config.addr_cfg = (pt_addr_cfg)_trace_header.filter;
    config.addr_filter.addr0_a = _trace_header.addr0_a;
    config.addr_filter.addr0_b = _trace_header.addr0_b;
    config.flags.variant.query.keep_tcal_on_ovf = 1;

    if (pt_cpu_errata(&config.errata, &config.cpu) < 0)
    {
        std::cerr << "TraceDataParser error: Could not parse cpu errate" << std::endl;
        exit(-1);
    }
    return;
}

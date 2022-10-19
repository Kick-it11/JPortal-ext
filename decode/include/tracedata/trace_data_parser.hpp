#ifndef TRACE_DATA_PARSER_HPP
#define TRACE_DATA_PARSER_HPP

#include <list>
#include <map>
#include <string>

struct pt_config;

/** Used to parse JPortalTrace.Data
 *  JPortalTrace.data includes PT data, Sideband data, JVM runtime data
 *  It will parse JPortalTrace.data & return relative data
 *  It can do fine PT data split.
 */
class TraceDataParser
{
private:
    /** TraceData header */
    struct TraceDataHeader
    {
        /** Trace header size: in case of wrong trace data */
        uint64_t header_size;

        /** PT CPU configurations: The filter. */
        uint32_t filter; /* 0 for stop, 1 for filter*/

        /** PT CPU configurations: The cpu vendor. */
        uint32_t vendor; /* 0 for pcv_unknown, 1 for pcv_intel*/

        /** PT CPU configurations: The cpu family. */
        uint16_t family;

        /** PT CPU configurations: The cpu model. */
        uint8_t model;

        /** PT CPU configurations: The stepping. */
        uint8_t stepping;

        /** PT CPU configurations: The cpu numbers */
        uint32_t nr_cpus;

        /** PT configurations: mtc frequency */
        uint8_t mtc_freq;

        /** PT configurations: normal frequency */
        uint8_t nom_freq;

        /** Sideband configurations: time shift */
        uint16_t time_shift;

        /** PT configurations: cpuid 15 eax */
        uint32_t cpuid_0x15_eax;

        /** PT configurations: cpuid 15 ebx */
        uint32_t cpuid_0x15_ebx;

        /** Sideband configurations: cpuid 15 eax */
        uint32_t time_mult;

        /** PT configurations: filter address lower bound */
        uint64_t addr0_a;

        /** PT configurations: filter address higher bound */
        uint64_t addr0_b;

        /** Sideband configurations: time zero */
        uint64_t time_zero;

        /** Sideband configurations: sample type */
        uint64_t sample_type;
    };

    const static int _default_sync_split_number = 500;

    /** number of synchronized points before splitting*/
    const int _sync_split_number;

    /** jportal trace file name */
    const std::string _filename;

    TraceDataHeader _trace_header;

    /** PT data list: <cpu, trace data> */
    std::map<uint32_t, std::list<std::pair<uint64_t, uint64_t>>> _pt_offsets;

    /** PT data offsets remaining, while splitting pt data*/
    std::map<uint32_t, std::list<std::pair<uint64_t, uint64_t>>> _split_pt_offsets;

    /** Sideband data list: <cpu, sideband data> */
    std::map<uint32_t, std::list<std::pair<uint64_t, uint64_t>>> _sideband_offsets;

    /** JVM runtime data list */
    std::list<std::pair<uint64_t, uint64_t>> _jvm_runtime_offsets;

    void sample_size(uint64_t sample_type);

    /** parse pt_offsets & sideband_data & jvm runtime offsets */
    bool parse();

    /** Use pt_pkt_decoder to sync forward until @number gets sync_split_number
     *
     *  return last sync_offset
     */
    uint64_t sync_forward(uint8_t *buffer, uint64_t size, int &number);

public:
    /** TraceDataParser constructor: initialized from file name */
    TraceDataParser(const std::string &filename);
    TraceDataParser(const std::string &filename, const int split_number);

    /** TraceDataParser destructor */
    ~TraceDataParser();

    /** return sideband data */
    std::map<uint32_t, std::pair<uint8_t *, uint64_t>> sideband_data();

    /** return jvm runtime data*/
    std::pair<uint8_t *, uint64_t> jvm_runtime_data();

    /** return pt data */
    std::map<uint32_t, std::pair<uint8_t *, uint64_t>> pt_data();

    /** begin to split pt data, set _split_pt_offsets to _pt_offsets*/
    void resplit_pt_data();

    /** get next splitted pt data, return true if we can still get */
    bool next_pt_data(std::pair<uint8_t*, uint64_t>& part_data, uint32_t &cpu);

    uint16_t time_shift()  { return _trace_header.time_shift;  }
    uint32_t time_mult()   { return _trace_header.time_mult;   }
    uint64_t time_zero()   { return _trace_header.time_zero;   }
    uint64_t sample_type() { return _trace_header.sample_type; }

    void init_pt_config_from_trace(struct pt_config &config);
};

#endif /* TRACE_DATA_PARSER_HPP */

#ifndef TRACE_SPLITTER
#define TRACE_SPLITTER

#include <map>
#include <list>
#include <string>

using std::map;
using std::list;
using std::pair;
using std::string;

class ifstream;

struct TraceHeader
{
    /** Trace header size: in case of wrong trace data */
    uint64_t header_size;

    /** PT CPU configurations: The cpu vendor. */
    uint64_t vendor; /*	0 for pcv_unknown, 1 for pcv_intel*/

    /** PT CPU configurations: The cpu family. */
    uint16_t family;

	/** PT CPU configurations: The cpu model. */
	uint8_t model;

	/** PT CPU configurations: The stepping. */
	uint8_t stepping;

    /** PT CPU configurations: The cpu numbers */
    int32_t nr_cpus;

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

extern TraceHeader trace_header;

/** Split results by TraceSplitter */
struct TracePart
{
    /**  for PT tracing data */
    uint8_t *pt;

    /* pt data size */
    uint64_t pt_size;

    /** for sideband data */
    uint8_t *sideband;

    /** sideband data size */
    uint64_t sideband_size;
};

/*
 * Used to split JPortalTrace.Data for parallel decoding
 * Use pt_pkt_decoder from pt/ to do fine split
 */
class TraceSplitter {
private:
    const static int _default_sync_split_number = 500;
    /** number of synchronized points before splitting*/
    const int _sync_split_number;

    /** jportal trace file name */
    const string _filename;

    /** PT data list: <cpu, trace data> */
    map<uint32_t, list<pair<uint64_t, uint64_t>>> _pt_offsets;

    /** Sideband data list, load it in advance
     * Since different thread of the same cpu use the same sideband
     */
    map<uint32_t, pair<uint8_t*, uint64_t>> _sideband_data;

    /** Perf sample size in sideband */
    uint64_t sample_size = 0;

    /** perf_sample_cpu offset in sideband sample */
    uint64_t cpu_offset = 0;

    /** parse sample size & cpu offset */
    void parse_sample_size();

    /** parse sideband data to _sideband_data*/
    void parse_sideband_data(map<int, list<pair<uint64_t, uint64_t>>> &offsets);

    /** parse pt_offsets & sideband_data */
    void parse();

    /** Use pt_pkt_decoder to sync forward until @number gets sync_split_number 
     *  
     *  return last sync_offset
    */
    uint64_t sync_forward(uint8_t* buffer, uint64_t size, int &number);

public:
    /* TraceSplitter constructor: initialized from file name */
    TraceSplitter(const string& filename);
    TraceSplitter(const string& filename, const int split_number);

    /* TraceSplitter destructor */
    ~TraceSplitter();

    bool next(TracePart& part);
};

#endif

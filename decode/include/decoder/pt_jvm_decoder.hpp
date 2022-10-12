#ifndef PT_JVM_DECODER
#define PT_JVM_DECODER

#include <stdint.h>
#include "pt/pt.hpp"
#include "insn/pt_insn.hpp"
#include "sideband/pevent.hpp"
#include "java/bytecodes.hpp"

class JVMRuntime;
class Analyser;
class TraceDataRecord;
class SidebandDecoder;
struct PCStackInfo;
class JitSection;

struct pt_config;

struct TracePart
{
    bool loss = false;
    uint8_t *pt_buffer = 0;
    size_t pt_size = 0;
    uint8_t *sb_buffer = 0;
    size_t sb_size = 0;
};

struct attr_config
{
    struct pt_cpu cpu;
    int nr_cpus;
    uint8_t mtc_freq;
    uint8_t nom_freq;
    uint32_t cpuid_0x15_eax;
    uint32_t cpuid_0x15_ebx;
    uint64_t sample_type;
    uint16_t time_shift;
    uint32_t time_mult;
    uint64_t time_zero;
    uint64_t addr0_a;
    uint64_t addr0_b;
};

extern attr_config attr;


/* PTJVMParser parses Trace Data and
 *   JPortalTrace.data records pt and sideband related data
 *   JPortalDump.data records JVM runtime related data
 * PTJVMParser translates them into parse_result
 *   which includes
 */
class PTJVMDecoder
{
private:
    /* The actual decoder. qry helps insn to decode bytecode*/
    struct pt_query_decoder *_qry;

    /* A collection of decoder-specific flags. */
    struct pt_conf_flags _flags;

    /* The current time */
    uint64_t _time;

    /* The current ip */
    uint64_t _ip;

    /* The current execution mode. */
    enum pt_exec_mode _mode;

    /* The status of the last successful decoder query.
     * Errors are reported directly; the status is always a non-negative
     * pt_status_flag bit-vector.
     */
    int _status;

    /* to indicate data loss */
    bool _loss;

    /* to indicate there is an unresolved ip for query decoder */
    bool _unresolved;

    /* A collection of flags defining how to proceed flow reconstruction:
     * - tracing is enabled.
     */
    uint32_t _enabled : 1;

    /* - process @event. */
    uint32_t _process_event : 1;

    /* - instructions are executed speculatively. */
    uint32_t _speculative : 1;

    /* - process @insn/@iext.
     *   We have started processing events binding to @insn/@iext.  We have
     *   not yet proceeded past it.
     */
    uint32_t _process_insn : 1;

    /* - a paging event has already been bound to @insn/@iext. */
    uint32_t _bound_paging : 1;

    /* - a vmcs event has already been bound to @insn/@iext. */
    uint32_t _bound_vmcs : 1;

    /* - a ptwrite event has already been bound to @insn/@iext. */
    uint32_t _bound_ptwrite : 1;

    /* The current address space. */
    struct pt_asid _asid;

    /* The current event. */
    struct pt_event _event;

    /* pt config */
    struct pt_config _config;

    /* instruction */
    struct pt_insn _insn;

    /* instruction ext */
    struct pt_insn_ext _iext;

    /* The current thread id */
    long _tid;

    /* Jvm dump infomation decoder */
    JVMRuntime *_jvm;

    /* The perf event sideband decoder configuration. */
    SidebandDecoder *_sideband;

    /* for decoding sideband infomation */
    struct pev_config _pevent;

    Analyser *_analyser;

    PCStackInfo *_last_pcinfo = nullptr;
    int _pcinfo_tow = 0;
    uint64_t _last_ip = 0;

    int pt_insn_decode(struct pt_insn *insn, struct pt_insn_ext *iext);
    int pt_insn_decode_retry(struct pt_insn *insn, struct pt_insn_ext *iext);
    int pt_insn_range_is_contiguous(uint64_t begin, uint64_t end,
				                    enum pt_exec_mode mode, size_t steps);
    void ptjvm_sb_event(TraceDataRecord &record);
    int check_erratum_skd022();
    int handle_erratum_skd022();
    int pt_insn_at_skl014(const struct pt_event *ev,
                          const struct pt_insn *insn,
                          const struct pt_insn_ext *iext,
                          const struct pt_config *config);
    int pt_insn_at_disabled_event(const struct pt_event *ev,
                                  const struct pt_insn *insn,
                                  const struct pt_insn_ext *iext,
                                  const struct pt_config *config);
    int event_pending();
    int pt_insn_status(int flags);
    int handle_erratum_bdm64(const struct pt_event *ev,
                             const struct pt_insn *insn,
                             const struct pt_insn_ext *iext);
    int pt_insn_postpone_tsx(const struct pt_insn *insn,
                             const struct pt_insn_ext *iext,
                             const struct pt_event *ev);
    int pt_insn_check_ip_event(const struct pt_insn *insn,
                               const struct pt_insn_ext *iext);
    int pt_insn_postpone(const struct pt_insn *insn,
                         const struct pt_insn_ext *iext);
    int pt_insn_check_insn_event(const struct pt_insn *insn,
                                 const struct pt_insn_ext *iext);
    int pt_insn_clear_postponed();
    int pt_insn_indirect_branch(uint64_t *ip);
    int pt_insn_cond_branch(int *taken);
    int pt_insn_proceed(const struct pt_insn *insn,
                        const struct pt_insn_ext *iext);
    int pt_insn_proceed_postponed();
    int pt_insn_process_enabled();
    int pt_insn_process_disabled();
    int pt_insn_process_async_branch();
    int pt_insn_process_paging();
    int pt_insn_process_overflow();
    int pt_insn_process_exec_mode();
    int pt_insn_process_tsx();
    int pt_insn_process_stop();
    int pt_insn_process_vmcs();
    int pt_insn_event();
    int drain_insn_events(int status);
    int handle_compiled_code_result(TraceDataRecord &record,
                                    JitSection *section);
    int pt_insn_reset();
    int pt_insn_start();
    int handle_compiled_code(TraceDataRecord &record, const char *prog);
    int handle_bytecode(TraceDataRecord &record, Bytecodes::Code bytecode,
                        const char *prog);
    int ptjvm_result_decode(TraceDataRecord &record, const char *prog);
    int drain_qry_events();

    void reset_decoder();

    void decode(TraceDataRecord record, const char *prog);

    int alloc_decoder(const struct pt_config *conf, const char *prog);

public:
    PTJVMDecoder(TracePart tracepart, TraceDataRecord record,
                 Analyser *analyser);
    ~PTJVMDecoder();
};

#endif

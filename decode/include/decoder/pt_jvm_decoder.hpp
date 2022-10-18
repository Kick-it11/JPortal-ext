#ifndef PT_JVM_DECODER_HPP
#define PT_JVM_DECODER_HPP

#include "decoder/decode_result.hpp"
#include "insn/pt_insn.hpp"
#include "java/bytecodes.hpp"
#include "pt/pt.hpp"

class JVMRuntime;
class JitSection;
class Sideband;
struct PCStackInfo;

/* PTJVMDEcoder decode JPortal data form Trace data and Dump data
 *   JPortalTrace.data records pt and sideband related data
 *                     and splitted to TraceParts by TraceSplitter
 *                     for parallel decoding
 *   JPortalDump.data records JVM runtime related data
 *                     analysed by JVMRuntime
 * PTJVMDecoder translates them into parse_result
 */
class PTJVMDecoder
{
private:
    /* The actual decoder. qry helps insn to decode bytecode*/
    struct pt_query_decoder *_qry;

    /* pt config */
    const struct pt_config &_config;

    /* Trace Data Record*/
    TraceDataRecord _record;

    /* Jvm dump infomation decoder */
    JVMRuntime *_jvm;

    /* The perf event sideband decoder configuration. */
    Sideband *_sideband;

    /* The current thread id */
    long _tid;

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

    /* instruction */
    struct pt_insn _insn;

    /* instruction ext */
    struct pt_insn_ext _iext;

    PCStackInfo *_last_pcinfo = nullptr;
    int _pcinfo_tow = 0;
    uint64_t _last_ip = 0;


private:
    /* private functions from libipt/pt_insn_decoder
     * Complete insn decoding for jitted codes
     * 
     * For a instruction pointer, decoder will first do a codelet match.
     * If it does not match any codelet,
     * decoder will try to find it in a jit_section
     * And will continue until it jumps out of jitted code.
     */

    int pt_insn_decode(struct pt_insn *insn, struct pt_insn_ext *iext);
    int pt_insn_decode_retry(struct pt_insn *insn, struct pt_insn_ext *iext);
    int pt_insn_range_is_contiguous(uint64_t begin, uint64_t end,
                                    enum pt_exec_mode mode, size_t steps);

    int check_erratum_skd022();
    int handle_erratum_skd022();
    int pt_insn_at_skl014(const struct pt_event *ev, const struct pt_insn *insn,
                          const struct pt_insn_ext *iext, const struct pt_config *config);
    int pt_insn_at_disabled_event(const struct pt_event *ev, const struct pt_insn *insn,
                                  const struct pt_insn_ext *iext, const struct pt_config *config);
    int pt_insn_status(int flags);
    int handle_erratum_bdm64(const struct pt_event *ev, const struct pt_insn *insn,
                             const struct pt_insn_ext *iext);
    int pt_insn_postpone_tsx(const struct pt_insn *insn, const struct pt_insn_ext *iext,
                             const struct pt_event *ev);
    int pt_insn_check_ip_event(const struct pt_insn *insn, const struct pt_insn_ext *iext);
    int pt_insn_postpone(const struct pt_insn *insn, const struct pt_insn_ext *iext);
    int pt_insn_check_insn_event(const struct pt_insn *insn, const struct pt_insn_ext *iext);
    int pt_insn_clear_postponed();
    int pt_insn_indirect_branch(uint64_t *ip);
    int pt_insn_cond_branch(int *taken);
    int pt_insn_proceed(const struct pt_insn *insn, const struct pt_insn_ext *iext);
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
    int pt_insn_reset();
    int pt_insn_start();

private:
    int event_pending();
    int drain_insn_events(int status);
    int handle_compiled_code_result(JitSection *section);
    int handle_compiled_code();
    int handle_bytecode(Bytecodes::Code bytecode);
    int ptjvm_result_decode();
    int drain_qry_events();

    void reset_decoder();

    void time_change();

public:

    PTJVMDecoder(const struct pt_config &config, TraceData &trace, uint32_t cpu);
    ~PTJVMDecoder();

    void decode();

};

#endif // PT_JVM_DECODER_HPP

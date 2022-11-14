#ifndef PT_JVM_DECODER_HPP
#define PT_JVM_DECODER_HPP

#include "decoder/decode_data.hpp"
#include "insn/pt_insn.hpp"
#include "insn/pt_retstack.hpp"
#include "java/bytecodes.hpp"
#include "pt/pt.hpp"

class JVMRuntime;
class JitImage;
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

    /* Jvm dump infomation decoder */
    JVMRuntime *_jvm;

    /* The perf event sideband decoder configuration. */
    Sideband *_sideband;

    /* JitImage */
    JitImage *_image;

    /* pt config */
    struct pt_config _config;

    /* Trace Data Record*/
    DecodeDataRecord _record;

    /* Map between <section, source ip> to <dest ip>, since inline cache */
    std::map<std::pair<JitSection *, uint64_t>, uint64_t> _ic_map;

    /* The current thread id : java tid not system tid */
    uint64_t _tid;

    /* The current time, start time and end time
     *   Current time is changing time while decoding PT data
     *   Start time and End time is acquired while Splitting PT data,
     *     This helps ignore sideband, jvm events before start time
     *     And handle all sideband jvm events before end time.
     * Time Information
     */
    uint64_t _time, _start_time, _end_time;

    /* The current ip */
    uint64_t _ip;

    /* The status of the last successful decoder query.
     * Errors are reported directly; the status is always a non-negative
     * pt_status_flag bit-vector.
     */
    int _status;

    /* The current execution mode. */
    enum pt_exec_mode _mode;

    /** pt ret stack*/
    struct pt_retstack _retstack;

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

private:
    /* functions tha are used both by jit & normal*/

    /*reset all info at the start of re-sync */
    void decoder_reset();

    /* query an indirect branch */
    int decoder_indirect_branch(uint64_t *ip);

    /* query a conditional branch */
    int decoder_cond_branch(int *taken);

    /* check if there is a event pending*/
    int decoder_event_pending();

    /* process all jvm runtime events */
    void decoder_drain_jvm_events();

    /* process all sideband events */
    void decoder_drain_sideband_events();

    /* check if time changes & process jvm runtime or sideband event */
    void decoder_time_change();

    /* process a specific event*/

    int decoder_process_enabled();
    int decoder_process_disabled();
    int decoder_process_async_branch();
    int decoder_process_paging();
    int decoder_process_overflow();
    int decoder_process_exec_mode();
    int decoder_process_tsx();
    int decoder_process_stop();
    int decoder_process_vmcs();

private:
    enum
    {
        /* The maximum number of steps to take when determining whether the
         * event location can be reached.
         */
        bdm64_max_steps = 0x100
    };

    /**
     * From pt_insn.c in libipt
     * Since we provide different pt_image, pt_section for jit
     * Abstact it here
     */

    int pt_insn_decode(struct pt_insn *insn, struct pt_insn_ext *iext);
    int pt_insn_decode_retry(struct pt_insn *insn, struct pt_insn_ext *iext);
    int pt_insn_range_is_contiguous(uint64_t begin, uint64_t end,
                                    enum pt_exec_mode mode, uint64_t steps);

private:
    /* private functions from libipt/pt_insn_decoder(for jit)
     * Complete insn decoding for jitted codes
     *
     * For a instruction pointer, decoder will first do a codelet match.
     * If it does not match any codelet,
     * decoder will try to find it in a jit_section
     * And will continue until it jumps out of jitted code.
     */

    /* process an ip or an insn event */

    int pt_insn_check_erratum_skd022();
    int pt_insn_handle_erratum_skd022();
    int pt_insn_at_skl014(const struct pt_event *ev, const struct pt_insn *insn,
                          const struct pt_insn_ext *iext, const struct pt_config *config);
    int pt_insn_handle_erratum_bdm64(const struct pt_event *ev, const struct pt_insn *insn,
                                     const struct pt_insn_ext *iext);

    int pt_insn_at_disabled_event(const struct pt_event *ev, const struct pt_insn *insn,
                                  const struct pt_insn_ext *iext, const struct pt_config *config);

    int pt_insn_postpone(const struct pt_insn *insn, const struct pt_insn_ext *iext);
    int pt_insn_clear_postponed();

    int pt_insn_postpone_tsx(const struct pt_insn *insn, const struct pt_insn_ext *iext,
                             const struct pt_event *ev);

    int pt_insn_proceed_postponed();

    /* check insn event */
    int pt_insn_check_insn_event(const struct pt_insn *insn, const struct pt_insn_ext *iext);

    /* check ip event, return containing event pending */
    int pt_insn_check_ip_event(const struct pt_insn *insn, const struct pt_insn_ext *iext);

    /** reset insn-related-only info */
    void pt_insn_reset();

    /* return current status that will influence instruction */
    int pt_insn_status(int flags);

    /* At the start of insn decoding,
     * reset related info,
     * return containing status to process initial event
     */
    int pt_insn_start();

    /* proceed instruction to next */
    int pt_insn_proceed(const struct pt_insn *insn, const struct pt_insn_ext *iext);

    /* drain all unrelevant events before proceed */
    int pt_insn_drain_events(int status);

    /* to next insn , return containing status */
    int pt_insn_next(JitSection *&section, struct pt_insn *insn, struct pt_insn_ext *iext);

private:
    /* process all events before entering decoder_record_result */
    int decoder_drain_events();

    int decoder_sync_forward();

    int decoder_record_jitcode(JitSection *section, struct pt_insn *insn);

    int decoder_process_jitcode(JitSection *section);

    int decoder_record_bytecode(Bytecodes::Code bytecode);

    void decoder_process_ip();

public:
    PTJVMDecoder(const struct pt_config *config, DecodeData *const data, uint32_t cpu,
                 std::pair<uint64_t, uint64_t> time);
    ~PTJVMDecoder();

    void decode();
};

#endif /* PT_JVM_DECODER_HPP */

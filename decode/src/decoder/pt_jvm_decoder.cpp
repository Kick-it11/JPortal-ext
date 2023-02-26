#include "decoder/pt_jvm_decoder.hpp"
#include "insn/pt_ild.hpp"
#include "insn/pt_retstack.hpp"
#include "java/method.hpp"
#include "runtime/jit_image.hpp"
#include "sideband/sideband.hpp"

#include <iostream>

void PTJVMDecoder::decoder_reset()
{
    _mode = ptem_unknown;
    _ip = 0UL;
    _status = 0;
    _speculative = 0;
    _process_event = 0;
    _process_insn = 0;
    _bound_paging = 0;
    _bound_vmcs = 0;
    _bound_ptwrite = 0;
    _enabled = 0;

    pt_asid_init(&_asid);
    pt_retstack_init(&_retstack);
}

/* Query an indirect branch.
 * Can be used by jit and normal
 * Returns zero on success, a negative error code otherwise.
 * Store current status in _status
 */
int PTJVMDecoder::decoder_indirect_branch(uint64_t *ip)
{
    uint64_t evip;
    int status;

    status = pt_qry_indirect_branch(_qry, ip);
    if (status < 0)
        return status;

    _status = status;

    /** do not generate tick event, if time has changed call time_change */
    decoder_time_change();

    return status;
}

/* Query a conditional branch.
 * Can be used by jit and normal
 * Returns zero on success, a negative error code otherwise.
 * Store current status in _status
 */
int PTJVMDecoder::decoder_cond_branch(int *taken)
{
    int status;

    status = pt_qry_cond_branch(_qry, taken);
    if (status < 0)
        return status;

    _status = status;

    decoder_time_change();

    return status;
}

/* Move forward to next synchronized point
 * Store current status in _status
 */
int PTJVMDecoder::decoder_sync_forward()
{
    int status;

    decoder_reset();

    status = pt_qry_sync_forward(_qry, &_ip);

    if (status < 0)
        return status;

    _status = status;

    decoder_time_change();

    return status;
}

/* return 1 if an event pending to be processed
 * 1. has queried, _process_event is 1
 * 2. _status = 1, query here
 */
int PTJVMDecoder::decoder_event_pending()
{
    int status;

    if (_process_event)
        return 1;

    status = _status;
    if (!(status & pts_event_pending))
        return 0;

    status = pt_qry_event(_qry, &_event, sizeof(_event));
    if (status < 0)
        return status;

    _process_event = 1;

    _status = status;

    decoder_time_change();

    return 1;
}

void PTJVMDecoder::decoder_drain_jvm_events(uint64_t time)
{
    for (;;)
    {
        const uint8_t *data;
        int status = _jvm->event(time, &data);
        if (status < 0)
        {
            if (status == -pte_eos)
                break;
            std::cerr << "PTJVMDecoder error: jvm event " << pt_errstr(pt_errcode(status)) << std::endl;
            return;
        }

        if (!(status && pts_event_pending))
        {
            break;
        }

        const JVMRuntime::DumpInfo *info = (const JVMRuntime::DumpInfo *)data;
        data += sizeof(JVMRuntime::DumpInfo);

        /* since RuntimeInfo might record something, we should check thread here */
        decoder_drain_sideband_events(info->time);

        switch (info->type)
        {
        default:
        {
            /* unknown dump type */
            std::cerr << "PTJVMDecoder error: unknown dump type" << std::endl;
            exit(1);
        }
        case JVMRuntime::_method_info:
        case JVMRuntime::_branch_not_taken_info:
        case JVMRuntime::_branch_taken_info:
        case JVMRuntime::_bci_table_stub_info:
        case JVMRuntime::_switch_table_stub_info:
        case JVMRuntime::_switch_default_info:
        case JVMRuntime::_deoptimization_info:
        {
            /* do not need to handle */
            break;
        }
        case JVMRuntime::_compiled_method_load_info:
        {
            data += info->size - sizeof(struct JVMRuntime::DumpInfo);
            JitSection *section = JVMRuntime::jit_section(data);
            if (!section)
            {
                std::cerr << "PTJVMDecoder error: JitSection cannot be found" << std::endl;
            }
            else
            {
                _image->add(section);
            }
            break;
        }
        case JVMRuntime::_compiled_method_unload_info:
        {
            const JVMRuntime::CompiledMethodUnloadInfo *cmui = (const JVMRuntime::CompiledMethodUnloadInfo *)data;
            _image->remove(cmui->code_begin);
            break;
        }
        case JVMRuntime::_thread_start_info:
        {
            /* Do not need to handle */
            break;
        }
        case JVMRuntime::_inline_cache_add_info:
        {
            const JVMRuntime::InlineCacheAddInfo *icai = (const JVMRuntime::InlineCacheAddInfo *)data;
            JitSection *section = _image->find(icai->src);
            _ic_map[{section, icai->src}] = icai->dest;
            break;
        }
        case JVMRuntime::_inline_cache_clear_info:
        {
            const JVMRuntime::InlineCacheClearInfo *icci = (const JVMRuntime::InlineCacheClearInfo *)data;
            JitSection *section = _image->find(icci->src);
            _ic_map.erase({section, icci->src});
            break;
        }
        }
    }
}

void PTJVMDecoder::decoder_drain_sideband_events(uint64_t time)
{
    struct pev_event event;
    bool data_loss = false;

    for (;;)
    {
        /* data loss record, might have duplicated or missing record */
        if (data_loss)
            _record.record_data_loss();

        int status = _sideband->event(time, &event);
        if (status < 0)
        {
            if (status == -pte_eos)
                break;
            std::cerr << "PTJVMDecoder error: sideband event " << pt_errstr(pt_errcode(status)) << std::endl;
            return;
        }

        if (!(status && pts_event_pending))
        {
            break;
        }

        /* handle sideband event */
        switch (event.type)
        {
        default:
            break;

        case PERF_RECORD_COMM:
        case PERF_RECORD_EXIT:
        case PERF_RECORD_THROTTLE:
        case PERF_RECORD_UNTHROTTLE:
        case PERF_RECORD_FORK:
        case PERF_RECORD_MMAP2:
            break;

        case PERF_RECORD_AUX:
            if (event.sample.tsc > _start_time && event.record.aux->flags & PERF_AUX_FLAG_TRUNCATED)
            {
                std::cerr << "PTJVMDecoder error: data loss around time " << event.sample.tsc << std::endl;
                data_loss = true;
            }
            break;

        case PERF_RECORD_ITRACE_START:
            _tid = JVMRuntime::java_tid(*event.sample.tid);
            _record.switch_in(_tid, event.sample.tsc);
            break;
        case PERF_RECORD_LOST_SAMPLES:
            std::cerr << "PTJVMDecoder error: perf record lost samples" << std::endl;
            break;
        case PERF_RECORD_SWITCH:
            if (event.misc & PERF_RECORD_MISC_SWITCH_OUT)
            {
                _record.switch_out(event.sample.tsc);
                _tid = 0;
            }
            else
            {
                _tid = JVMRuntime::java_tid(*event.sample.tid);
                if (_tid == 0)
                {
                    std::cerr << "PTJVMDecoder error: Fail to get java tid " << *event.sample.tid << std::endl;
                }
                _record.switch_in(_tid, event.sample.tsc);
            }
            break;
        case PERF_RECORD_SWITCH_CPU_WIDE:
            if (event.misc & PERF_RECORD_MISC_SWITCH_OUT)
            {
                _tid = JVMRuntime::java_tid(event.record.switch_cpu_wide->next_prev_tid);
            }
            else
            {
                _tid = JVMRuntime::java_tid(*event.sample.tid);
            }
            if (_tid == 0)
            {
                std::cerr << "PTJVMDecoder error: Fail to get java tid " << *event.sample.tid << std::endl;
            }
            _record.switch_in(_tid, event.sample.tsc);
            break;
        }
    }
}

void PTJVMDecoder::decoder_time_change()
{
    uint64_t tsc;

    pt_qry_time(_qry, &tsc, NULL, NULL);

    if (tsc == _time)
        return;

    _time = tsc;

    decoder_drain_jvm_events(_time);

    decoder_drain_sideband_events(_time);
}

int PTJVMDecoder::decoder_process_enabled()
{
    struct pt_event *ev;

    ev = &_event;

    /* Use status update events to diagnose inconsistencies. */
    if (ev->status_update)
    {
        if (!_enabled)
            return -pte_bad_status_update;

        return 0;
    }

    /* We must have an IP in order to start decoding. */
    if (ev->ip_suppressed)
        return -pte_noip;

    /* We must currently be disabled. */
    if (_enabled)
        return -pte_bad_context;

    _ip = ev->variant.enabled.ip;
    _enabled = 1;

    return 0;
}

int PTJVMDecoder::decoder_process_disabled()
{
    struct pt_event *ev;

    ev = &_event;

    /* Use status update events to diagnose inconsistencies. */
    if (ev->status_update)
    {
        if (_enabled)
            return -pte_bad_status_update;

        return 0;
    }

    /* We must currently be enabled. */
    if (!_enabled)
        return -pte_bad_context;

    /* We preserve @_ip.  This is where we expect tracing to resume
     * and we'll indicate that on the subsequent enabled event if tracing
     * actually does resume from there.
     */
    _enabled = 0;

    return 0;
}

int PTJVMDecoder::decoder_process_async_branch()
{
    struct pt_event *ev;

    ev = &_event;

    /* This event can't be a status update. */
    if (ev->status_update)
        return -pte_bad_context;

    /* Tracing must be enabled in order to make sense of the event. */
    if (!_enabled)
        return -pte_bad_context;

    _ip = ev->variant.async_branch.to;

    return 0;
}

int PTJVMDecoder::decoder_process_paging()
{
    uint64_t cr3;
    int errcode;

    cr3 = _event.variant.paging.cr3;
    if (_asid.cr3 != cr3)
    {
        /** errcode = pt_msec_cache_invalidate(&decoder->scache);
         *  if (errcode < 0)
         *   return errcode;
         */

        _asid.cr3 = cr3;
    }

    return 0;
}

int PTJVMDecoder::decoder_process_overflow()
{
    struct pt_event *ev;

    ev = &_event;

    /* This event can't be a status update. */
    if (ev->status_update)
        return -pte_bad_context;

    /* If the IP is suppressed, the overflow resolved while tracing was
     * disabled.  Otherwise it resolved while tracing was enabled.
     */
    if (ev->ip_suppressed)
    {
        /* Tracing is disabled.
         *
         * It doesn't make sense to preserve the previous IP.  This will
         * just be misleading.  Even if tracing had been disabled
         * before, as well, we might have missed the re-enable in the
         * overflow.
         */
        _enabled = 0;
        _ip = 0ull;
    }
    else
    {
        /* Tracing is enabled and we're at the IP at which the overflow
         * resolved.
         */
        _ip = ev->variant.overflow.ip;
        _enabled = 1;
    }

    /* We don't know the TSX state.  Let's assume we execute normally.
     *
     * We also don't know the execution mode.  Let's keep what we have
     * in case we don't get an update before we have to decode the next
     * instruction.
     */
    _speculative = 0;

    return 0;
}

int PTJVMDecoder::decoder_process_exec_mode()
{
    enum pt_exec_mode mode;
    struct pt_event *ev;

    ev = &_event;
    mode = ev->variant.exec_mode.mode;

    /* Use status update events to diagnose inconsistencies. */
    if (ev->status_update && _enabled &&
        _mode != ptem_unknown && _mode != mode)
        return -pte_bad_status_update;

    _mode = mode;

    return 0;
}

int PTJVMDecoder::decoder_process_tsx()
{

    _speculative = _event.variant.tsx.speculative;

    return 0;
}

int PTJVMDecoder::decoder_process_stop()
{
    struct pt_event *ev;

    ev = &_event;

    /* This event can't be a status update. */
    if (ev->status_update)
        return -pte_bad_context;

    /* Tracing is always disabled before it is stopped. */
    if (_enabled)
        return -pte_bad_context;

    return 0;
}

int PTJVMDecoder::decoder_process_vmcs()
{
    uint64_t vmcs;
    int errcode;

    vmcs = _event.variant.vmcs.base;
    if (_asid.vmcs != vmcs)
    {
        /* errcode = pt_msec_cache_invalidate(&decoder->scache);
         * if (errcode < 0)
         *     return errcode;
         */

        _asid.vmcs = vmcs;
    }
    return 0;
}

/* Retry decoding an instruction after a preceding decode error.
 *
 * Instruction length decode typically fails due to 'not enough
 * bytes'.
 *
 * This may be caused by partial updates of text sections
 * represented via new image sections overlapping the original
 * text section's image section.  We stop reading memory at the
 * end of the section so we do not read the full instruction if
 * parts of it have been overwritten by the update.
 *
 * Try to read the remaining bytes and decode the instruction again.  If we
 * succeed, set @insn->truncated to indicate that the instruction is truncated
 * in @insn->isid.
 *
 * Returns zero on success, a negative error code otherwise.
 * Returns -pte_bad_insn if the instruction could not be decoded.
 */
int PTJVMDecoder::pt_insn_decode_retry(struct pt_insn *insn, struct pt_insn_ext *iext)
{
    int size, errcode, isid;
    uint8_t isize;
    uint8_t remaining;

    if (!insn)
        return -pte_internal;

    isize = insn->size;
    remaining = sizeof(insn->raw) - isize;

    /* We failed for real if we already read the maximum number of bytes for
     * an instruction.
     */
    if (!remaining)
        return -pte_bad_insn;

    /* Read the remaining bytes from the image. */
    JitSection *section = _image->find(insn->ip + isize);
    if (!section)
        return -pte_bad_insn;

    if (!section->read(&insn->raw[isize], &remaining, insn->ip + isize))
    {
        /* We should have gotten an error if we were not able to read at
         * least one byte.  Check this to guarantee termination.
         */
        return -pte_bad_insn;
    }

    /* Add the newly read bytes to the instruction's size. */
    insn->size += remaining;

    /* Store the new size to avoid infinite recursion in case instruction
     * decode fails after length decode, which would set @insn->size to the
     * actual length.
     */
    size = insn->size;

    /* Try to decode the instruction again.
     *
     * If we fail again, we recursively retry again until we either fail to
     * read more bytes or reach the maximum number of bytes for an
     * instruction.
     */
    errcode = pt_ild_decode(insn, iext);
    if (errcode < 0)
    {
        if (errcode != -pte_bad_insn)
            return errcode;

        /* If instruction length decode already determined the size,
         * there's no point in reading more bytes.
         */
        if (insn->size != (uint8_t)size)
            return errcode;

        return pt_insn_decode_retry(insn, iext);
    }

    /* We succeeded this time, so the instruction crosses image section
     * boundaries.
     *
     * This poses the question which isid to use for the instruction.
     *
     * To reconstruct exactly this instruction at a later time, we'd need to
     * store all isids involved together with the number of bytes read for
     * each isid.  Since @insn already provides the exact bytes for this
     * instruction, we assume that the isid will be used solely for source
     * correlation.  In this case, it should refer to the first byte of the
     * instruction - as it already does.
     */
    insn->truncated = 1;

    return errcode;
}

int PTJVMDecoder::pt_insn_decode(struct pt_insn *insn, struct pt_insn_ext *iext)
{
    int errcode;

    if (!insn)
        return -pte_internal;

    JitSection *section = _image->find(insn->ip);
    if (!section)
        return -pte_bad_insn;

    uint8_t size = sizeof(insn->raw);
    if (!section->read(insn->raw, &size, insn->ip))
    {
        /* We should have gotten an error if we were not able to read at
         * least one byte.  Check this to guarantee termination.
         */
        return -pte_bad_insn;
    }
    /* We initialize @insn->size to the maximal possible size.  It will be
     * set to the actual size during instruction decode.
     */
    insn->size = (uint8_t)size;

    errcode = pt_ild_decode(insn, iext);
    if (errcode < 0)
    {
        if (errcode != -pte_bad_insn)
            return errcode;

        /* If instruction length decode already determined the size,
         * there's no point in reading more bytes.
         */
        if (insn->size != (uint8_t)size)
            return errcode;

        return pt_insn_decode_retry(insn, iext);
    }

    return errcode;
}

int PTJVMDecoder::pt_insn_range_is_contiguous(uint64_t begin, uint64_t end,
                                              enum pt_exec_mode mode, uint64_t steps)
{
    struct pt_insn_ext iext;
    struct pt_insn insn;

    memset(&insn, 0, sizeof(insn));

    insn.mode = mode;
    insn.ip = begin;

    while (insn.ip != end)
    {
        int errcode;

        if (!steps--)
            return 0;

        errcode = pt_insn_decode(&insn, &iext);
        if (errcode < 0)
            return errcode;

        errcode = pt_insn_next_ip(&insn.ip, &insn, &iext);
        if (errcode < 0)
            return errcode;
    }

    return 1;
}

int PTJVMDecoder::pt_insn_check_erratum_skd022()
{
    struct pt_insn_ext iext;
    struct pt_insn insn;
    int errcode;

    insn.mode = _mode;
    insn.ip = _ip;

    errcode = pt_insn_decode(&insn, &iext);
    if (errcode < 0)
        return 0;

    switch (iext.iclass)
    {
    default:
        return 0;

    case PTI_INST_VMLAUNCH:
    case PTI_INST_VMRESUME:
        return 1;
    }
}

int PTJVMDecoder::pt_insn_handle_erratum_skd022()
{
    struct pt_event *ev;
    uint64_t ip;
    int errcode;

    errcode = pt_insn_check_erratum_skd022();
    if (errcode <= 0)
        return errcode;

    /* We turn the async disable into a sync disable.  It will be processed
     * after decoding the instruction.
     */
    ev = &_event;

    ip = ev->variant.async_disabled.ip;

    ev->type = ptev_disabled;
    ev->variant.disabled.ip = ip;

    return 1;
}

int PTJVMDecoder::pt_insn_at_skl014(const struct pt_event *ev,
                                    const struct pt_insn *insn,
                                    const struct pt_insn_ext *iext,
                                    const struct pt_config *config)
{
    uint64_t ip;
    int status;

    if (!ev || !insn || !iext || !config)
        return -pte_internal;

    if (!ev->ip_suppressed)
        return 0;

    switch (insn->iclass)
    {
    case ptic_call:
    case ptic_jump:
        /* The erratum only applies to unconditional direct branches. */
        if (!iext->variant.branch.is_direct)
            break;

        /* Check the filter against the branch target. */
        ip = insn->ip;
        ip += insn->size;
        ip += (uint64_t)(int64_t)iext->variant.branch.displacement;

        status = pt_filter_addr_check(&config->addr_filter, ip);
        if (status <= 0)
        {
            if (status < 0)
                return status;

            return 1;
        }
        break;

    default:
        break;
    }

    return 0;
}

/* Try to work around erratum BDM64.
 *
 * If we got a transaction abort immediately following a branch that produced
 * trace, the trace for that branch might have been corrupted.
 *
 * Returns a positive integer if the erratum was handled.
 * Returns zero if the erratum does not seem to apply.
 * Returns a negative error code otherwise.
 */
int PTJVMDecoder::pt_insn_handle_erratum_bdm64(const struct pt_event *ev,
                                               const struct pt_insn *insn,
                                               const struct pt_insn_ext *iext)
{
    int status;

    if (!ev || !insn || !iext)
        return -pte_internal;

    /* This only affects aborts. */
    if (!ev->variant.tsx.aborted)
        return 0;

    /* This only affects branches. */
    if (!pt_insn_is_branch(insn, iext))
        return 0;

    /* Let's check if we can reach the event location from here.
     *
     * If we can, let's assume the erratum did not hit.  We might still be
     * wrong but we're not able to tell.
     */
    status = pt_insn_range_is_contiguous(_ip, ev->variant.tsx.ip, _mode, bdm64_max_steps);
    if (status > 0)
        return 0;

    /* We can't reach the event location.  This could either mean that we
     * stopped too early (and status is zero) or that the erratum hit.
     *
     * We assume the latter and pretend that the previous branch brought us
     * to the event location, instead.
     */
    _ip = ev->variant.tsx.ip;

    return 1;
}

int PTJVMDecoder::pt_insn_at_disabled_event(const struct pt_event *ev,
                                            const struct pt_insn *insn,
                                            const struct pt_insn_ext *iext,
                                            const struct pt_config *config)
{
    if (!ev || !insn || !iext || !config)
        return -pte_internal;

    if (ev->ip_suppressed)
    {
        if (pt_insn_is_far_branch(insn, iext) ||
            pt_insn_changes_cpl(insn, iext) ||
            pt_insn_changes_cr3(insn, iext))
            return 1;

        /* If we don't have a filter configuration we assume that no
         * address filters were used and the erratum does not apply.
         *
         * We might otherwise disable tracing too early.
         */
        if (config->addr_filter.config.addr_cfg &&
            config->errata.skl014 &&
            pt_insn_at_skl014(ev, insn, iext, config))
            return 1;
    }
    else
    {
        switch (insn->iclass)
        {
        case ptic_ptwrite:
        case ptic_other:
            break;

        case ptic_call:
        case ptic_jump:
            /* If we got an IP with the disabled event, we may
             * ignore direct branches that go to a different IP.
             */
            if (iext->variant.branch.is_direct)
            {
                uint64_t ip;

                ip = insn->ip;
                ip += insn->size;
                ip += (uint64_t)(int64_t)iext->variant.branch.displacement;

                if (ip != ev->variant.disabled.ip)
                    break;
            }

            /* fallthrough; */

        case ptic_return:
        case ptic_far_call:
        case ptic_far_return:
        case ptic_far_jump:
        case ptic_indirect:
        case ptic_cond_jump:
            return 1;

        case ptic_unknown:
            return -pte_bad_insn;
        }
    }

    return 0;
}

/* Postpone proceeding past @insn/@iext and indicate a pending event.
 *
 * There may be further events pending on @insn/@iext.  Postpone proceeding past
 * @insn/@iext until we processed all events that bind to it.
 *
 * Returns a non-negative pt_status_flag bit-vector indicating a pending event
 * on success, a negative pt_error_code otherwise.
 */
int PTJVMDecoder::pt_insn_postpone(const struct pt_insn *insn,
                                   const struct pt_insn_ext *iext)
{
    if (!insn || !iext)
        return -pte_internal;

    if (!_process_insn)
    {
        _process_insn = 1;
        _insn = *insn;
        _iext = *iext;
    }

    return pt_insn_status(pts_event_pending);
}

/* Remove any postponed instruction from @decoder.
 *
 * Returns zero on success, a negative pt_error_code otherwise.
 */
int PTJVMDecoder::pt_insn_clear_postponed()
{
    _process_insn = 0;
    _bound_paging = 0;
    _bound_vmcs = 0;
    _bound_ptwrite = 0;

    return 0;
}

/* Proceed past a postponed instruction.
 *
 * Returns zero on success, a negative pt_error_code otherwise.
 */
int PTJVMDecoder::pt_insn_proceed_postponed()
{
    int status;

    if (!_process_insn)
        return -pte_internal;

    /* There's nothing to do if tracing got disabled. */
    if (!_enabled)
        return pt_insn_clear_postponed();

    status = pt_insn_proceed(&_insn, &_iext);
    if (status < 0)
        return status;

    return pt_insn_clear_postponed();
}

int PTJVMDecoder::pt_insn_postpone_tsx(const struct pt_insn *insn,
                                       const struct pt_insn_ext *iext,
                                       const struct pt_event *ev)
{
    int status;

    if (!ev)
        return -pte_internal;

    if (ev->ip_suppressed)
        return 0;

    if (insn && iext)
    {
        if (_config.errata.bdm64)
        {
            status = pt_insn_handle_erratum_bdm64(ev, insn, iext);
            if (status < 0)
                return status;
        }
    }

    if (_ip != ev->variant.tsx.ip)
        return 1;

    return 0;
}

int PTJVMDecoder::pt_insn_check_insn_event(const struct pt_insn *insn,
                                           const struct pt_insn_ext *iext)
{
    struct pt_event *ev;
    int status;

    status = decoder_event_pending();
    if (status <= 0)
        return status;

    ev = &_event;
    switch (ev->type)
    {
    case ptev_enabled:
    case ptev_overflow:
    case ptev_async_paging:
    case ptev_async_vmcs:
    case ptev_async_disabled:
    case ptev_async_branch:
    case ptev_exec_mode:
    case ptev_tsx:
    case ptev_stop:
    case ptev_exstop:
    case ptev_mwait:
    case ptev_pwre:
    case ptev_pwrx:
    case ptev_tick:
    case ptev_cbr:
    case ptev_mnt:
        /* We're only interested in events that bind to instructions. */
        return 0;

    case ptev_disabled:
        if (ev->status_update)
            return 0;

        status = pt_insn_at_disabled_event(ev, insn, iext, &_config);
        if (status <= 0)
            return status;

        /* We're at a synchronous disable event location.
         *
         * Let's determine the IP at which we expect tracing to resume.
         */
        status = pt_insn_next_ip(&_ip, insn, iext);
        if (status < 0)
        {
            /* We don't know the IP on error. */
            _ip = 0ull;

            /* For indirect calls, assume that we return to the next
             * instruction.
             *
             * We only check the instruction class, not the
             * is_direct property, since direct calls would have
             * been handled by pt_insn_nex_ip() or would have
             * provoked a different error.
             */
            if (status != -pte_bad_query)
                return status;

            switch (insn->iclass)
            {
            case ptic_call:
            case ptic_far_call:
                _ip = insn->ip + insn->size;
                break;

            default:
                break;
            }
        }

        break;

    case ptev_paging:
        /* We bind at most one paging event to an instruction. */
        if (_bound_paging)
            return 0;

        if (!pt_insn_binds_to_pip(insn, iext))
            return 0;

        /* We bound a paging event.  Make sure we do not bind further
         * paging events to this instruction.
         */
        _bound_paging = 1;

        return pt_insn_postpone(insn, iext);

    case ptev_vmcs:
        /* We bind at most one vmcs event to an instruction. */
        if (_bound_vmcs)
            return 0;

        if (!pt_insn_binds_to_vmcs(insn, iext))
            return 0;

        /* We bound a vmcs event.  Make sure we do not bind further vmcs
         * events to this instruction.
         */
        _bound_vmcs = 1;

        return pt_insn_postpone(insn, iext);

    case ptev_ptwrite:
        /* We bind at most one ptwrite event to an instruction. */
        if (_bound_ptwrite)
            return 0;

        if (ev->ip_suppressed)
        {
            if (!pt_insn_is_ptwrite(insn, iext))
                return 0;

            /* Fill in the event IP.  Our users will need them to
             * make sense of the PTWRITE payload.
             */
            ev->variant.ptwrite.ip = _ip;
            ev->ip_suppressed = 0;
        }
        else
        {
            /* The ptwrite event contains the IP of the ptwrite
             * instruction (CLIP) unlike most events that contain
             * the IP of the first instruction that did not complete
             * (NLIP).
             *
             * It's easier to handle this case here, as well.
             */
            if (_ip != ev->variant.ptwrite.ip)
                return 0;
        }

        /* We bound a ptwrite event.  Make sure we do not bind further
         * ptwrite events to this instruction.
         */
        _bound_ptwrite = 1;

        return pt_insn_postpone(insn, iext);
    }

    return pt_insn_status(pts_event_pending);
}

/* Check for events that bind to an IP.
 *
 * Check whether an event is pending that binds to @decoder->ip, and, if that is
 * the case, indicate the event by setting pt_pts_event_pending.
 *
 * Returns a non-negative pt_status_flag bit-vector on success, a negative error
 * code otherwise.
 */
int PTJVMDecoder::pt_insn_check_ip_event(const struct pt_insn *insn,
                                         const struct pt_insn_ext *iext)
{
    struct pt_event *ev;
    int status;

    status = decoder_event_pending();
    if (status <= 0)
    {
        if (status < 0)
            return status;

        return pt_insn_status(0);
    }

    ev = &_event;
    switch (ev->type)
    {
    case ptev_disabled:
        if (ev->status_update)
            return pt_insn_status(pts_event_pending);

        break;

    case ptev_enabled:
        return pt_insn_status(pts_event_pending);

    case ptev_async_disabled:
    {
        int errcode;

        if (ev->variant.async_disabled.at != _ip)
            break;

        if (_config.errata.skd022)
        {
            errcode = pt_insn_handle_erratum_skd022();
            if (errcode != 0)
            {
                if (errcode < 0)
                    return errcode;

                /* If the erratum applies, we postpone the
                 * modified event to the next call to
                 * pt_insn_next().
                 */
                break;
            }
        }

        return pt_insn_status(pts_event_pending);
    }

    case ptev_tsx:
        status = pt_insn_postpone_tsx(insn, iext, ev);
        if (status != 0)
        {
            if (status < 0)
                return status;

            break;
        }

        return pt_insn_status(pts_event_pending);

    case ptev_async_branch:
        if (ev->variant.async_branch.from != _ip)
            break;

        return pt_insn_status(pts_event_pending);

    case ptev_overflow:
        return pt_insn_status(pts_event_pending);

    case ptev_exec_mode:
        if (!ev->ip_suppressed && ev->variant.exec_mode.ip != _ip)
            break;

        return pt_insn_status(pts_event_pending);

    case ptev_paging:
        if (_enabled)
            break;

        return pt_insn_status(pts_event_pending);

    case ptev_async_paging:
        if (!ev->ip_suppressed && ev->variant.async_paging.ip != _ip)
            break;

        return pt_insn_status(pts_event_pending);

    case ptev_vmcs:
        if (_enabled)
            break;

        return pt_insn_status(pts_event_pending);

    case ptev_async_vmcs:
        if (!ev->ip_suppressed && ev->variant.async_vmcs.ip != _ip)
            break;

        return pt_insn_status(pts_event_pending);

    case ptev_stop:
        return pt_insn_status(pts_event_pending);

    case ptev_exstop:
        if (!ev->ip_suppressed && _enabled &&
            _ip != ev->variant.exstop.ip)
            break;

        return pt_insn_status(pts_event_pending);

    case ptev_mwait:
        if (!ev->ip_suppressed && _enabled &&
            _ip != ev->variant.mwait.ip)
            break;

        return pt_insn_status(pts_event_pending);

    case ptev_pwre:
    case ptev_pwrx:
        return pt_insn_status(pts_event_pending);

    case ptev_ptwrite:
        /* Any event binding to the current PTWRITE instruction is
         * handled in pt_insn_check_insn_event().
         *
         * Any subsequent ptwrite event binds to a different instruction
         * and must wait until the next iteration - as long as tracing
         * is enabled.
         *
         * When tracing is disabled, we forward all ptwrite events
         * immediately to the user.
         */
        if (_enabled)
            break;

        return pt_insn_status(pts_event_pending);

    case ptev_tick:
    case ptev_cbr:
    case ptev_mnt:
        return pt_insn_status(pts_event_pending);
    }

    return pt_insn_status(0);
}

void PTJVMDecoder::pt_insn_reset()
{
    /* _mode, _ip, _status, _speculative, _asid */
    _process_insn = 0;
    _bound_paging = 0;
    _bound_vmcs = 0;
    _bound_ptwrite = 0;

    pt_retstack_init(&_retstack);
    return;
}

/* return current status that will influence instruction */
int PTJVMDecoder::pt_insn_status(int flags)
{
    int status;

    status = _status;

    /* Indicate whether tracing is disabled or enabled.
     *
     * This duplicates the indication in struct pt_insn and covers the case
     * where we indicate the status after synchronizing.
     */
    if (!_enabled)
        flags |= pts_ip_suppressed;

    /* Forward end-of-trace indications.
     *
     * Postpone it as long as we're still processing events, though.
     */
    if ((status & pts_eos) && !_process_event)
        flags |= pts_eos;

    return flags;
}

/* At the start of insn decoding,
 * reset related info,
 * return containing status to process initial event
 */
int PTJVMDecoder::pt_insn_start()
{
    pt_insn_reset();

    if (!(_status & pts_ip_suppressed))
        _enabled = 1;

    /* Process any initial events.
     *
     * Some events are processed after proceeding to the next IP in order to
     * indicate things like tracing disable or trace stop in the preceding
     * instruction.  Those events will be processed without such an
     * indication before decoding the current instruction.
     *
     * We do this already here so we can indicate user-events that precede
     * the first instruction.
     */
    return pt_insn_check_ip_event(NULL, NULL);
}

/* proceed instruction to next */
int PTJVMDecoder::pt_insn_proceed(const struct pt_insn *insn,
                                  const struct pt_insn_ext *iext)
{
    if (!insn || !iext)
        return -pte_internal;

    /* Branch displacements apply to the next instruction. */
    _ip += insn->size;

    /* We handle non-branches, non-taken conditional branches, and
     * compressed returns directly in the switch and do some pre-work for
     * calls.
     *
     * All kinds of branches are handled below the switch.
     */
    switch (insn->iclass)
    {
    case ptic_ptwrite:
    case ptic_other:
        return 0;

    case ptic_cond_jump:
    {
        int status, taken;

        status = decoder_cond_branch(&taken);
        if (status < 0)
            return status;

        if (!taken)
            return 0;

        break;
    }

    case ptic_call:
        /* Log the call for return compression.
         *
         * Unless this is a call to the next instruction as is used
         * for position independent code.
         */
        if (iext->variant.branch.displacement ||
            !iext->variant.branch.is_direct)
            pt_retstack_push(&_retstack, _ip);

        break;

    case ptic_return:
    {
        int taken, status;

        /* Check for a compressed return. */
        status = decoder_cond_branch(&taken);
        if (status >= 0)
        {
            /* A compressed return is indicated by a taken
             * conditional branch.
             */
            if (!taken)
                return -pte_bad_retcomp;

            return pt_retstack_pop(&_retstack, &_ip);
        }

        break;
    }

    case ptic_jump:
    case ptic_far_call:
    case ptic_far_return:
    case ptic_far_jump:
    case ptic_indirect:
        break;

    case ptic_unknown:
        return -pte_bad_insn;
    }

    /* Process a direct or indirect branch.
     *
     * This combines calls, uncompressed returns, taken conditional jumps,
     * and all flavors of far transfers.
     */
    if (iext->variant.branch.is_direct)
        _ip += (uint64_t)(int64_t)iext->variant.branch.displacement;
    else
    {
        int status;

        status = decoder_indirect_branch(&_ip);

        if (status < 0)
            return status;

        /* We do need an IP to proceed. */
        if (status & pts_ip_suppressed)
            return -pte_noip;
    }

    return 0;
}

int PTJVMDecoder::pt_insn_drain_events(int status)
{
    while (status && pts_event_pending)
    {
        /* We must currently process an event. */
        if (!_process_event)
            return -pte_bad_query;

        switch (_event.type)
        {
        default:
            /* This is not a user event.
             *
             * We either indicated it wrongly or the user called
             * pt_insn_event() without a pts_event_pending indication.
             */
            return -pte_bad_query;

        case ptev_enabled:
            /* Indicate that tracing resumes from the IP at which tracing
             * had been disabled before (with some special treatment for
             * calls).
             */
            if (_ip == _event.variant.enabled.ip)
                _event.variant.enabled.resumed = 1;

            status = decoder_process_enabled();
            if (status < 0)
                return status;

            break;

        case ptev_async_disabled:
            if (!_event.ip_suppressed && _ip != _event.variant.async_disabled.at)
                return -pte_bad_query;

            /* fallthrough */

        case ptev_disabled:
            status = decoder_process_disabled();
            if (status < 0)
                return status;

            break;

        case ptev_async_branch:
            if (_ip != _event.variant.async_branch.from)
                return -pte_bad_query;

            status = decoder_process_async_branch();
            if (status < 0)
                return status;

            break;

        case ptev_async_paging:
            if (!_event.ip_suppressed && _ip != _event.variant.async_paging.ip)
                return -pte_bad_query;

            /* faillthrough */

        case ptev_paging:
            status = decoder_process_paging();
            if (status < 0)
                return status;

            break;

        case ptev_async_vmcs:
            if (!_event.ip_suppressed && _ip != _event.variant.async_vmcs.ip)
                return -pte_bad_query;

        case ptev_vmcs:
            status = decoder_process_vmcs();
            if (status < 0)
                return status;

            break;

        case ptev_overflow:
            status = decoder_process_overflow();
            if (status < 0)
                return status;

            break;

        case ptev_exec_mode:
            status = decoder_process_exec_mode();
            if (status < 0)
                return status;

            break;

        case ptev_tsx:
            status = decoder_process_tsx();
            if (status < 0)
                return status;

            break;

        case ptev_stop:
            status = decoder_process_stop();
            if (status < 0)
                return status;

            break;

        case ptev_exstop:
            if (!_event.ip_suppressed && _enabled &&
                _ip != _event.variant.exstop.ip)
                return -pte_bad_query;

            break;

        case ptev_mwait:
            if (!_event.ip_suppressed && _enabled &&
                _ip != _event.variant.mwait.ip)
                return -pte_bad_query;

            break;

        case ptev_pwre:
        case ptev_pwrx:
        case ptev_ptwrite:
        case ptev_tick:
        case ptev_cbr:
        case ptev_mnt:
            break;
        }

        /* This completes processing of the current event. */
        _process_event = 0;

        /* If we just handled an instruction event, check for further events
         * that bind to this instruction.
         *
         * If we don't have further events, proceed beyond the instruction so we
         * can check for IP events, as well.
         */
        if (_process_insn)
        {
            status = pt_insn_check_insn_event(&_insn, &_iext);

            if (status != 0)
            {
                if (status < 0)
                    return status;

                if (status & pts_event_pending)
                    continue;
            }

            /* Proceed to the next instruction. */
            status = pt_insn_proceed_postponed();
            if (status < 0)
                return status;
        }

        /* Indicate further events that bind to the same IP. */
        status = pt_insn_check_ip_event(NULL, NULL);
        if (status < 0)
            return status;
    }

    return status;
}

int PTJVMDecoder::decoder_record_jitcode(JitSection *section, struct pt_insn *insn)
{
    if (!section || !insn)
        return -pte_internal;

    if (insn->ip >= section->stub_begin())
    {
        return 0;
    }

    if (insn->ip == section->osr_entry_point())
    {
        if (!_record.record_jit_osr_entry(section->id()))
            return -pte_bad_context;
    }
    else if (insn->ip == section->verified_entry_point())
    {
        if (!_record.record_jit_entry(section->id()))
            return -pte_bad_context;
    }

    int idx = section->find_pc(insn->ip + insn->size);
    if (idx >= 0)
    {
        if (!_record.record_jit_code(section->id(), idx))
            return -pte_bad_context;
    }

    if (insn->iclass == ptic_return || insn->iclass == ptic_far_return)
    {
        if (!_record.record_jit_return())
            return -pte_bad_context;
    }

    return 0;
}

int PTJVMDecoder::pt_insn_next(JitSection *&section, struct pt_insn *insn, struct pt_insn_ext *iext)
{
    int status, isid;

    if (!section || !insn || !iext)
        return -pte_internal;

    /* Tracing must be enabled.
     *
     * If it isn't we should be processing events until we either run out of
     * trace or process a tracing enabled event.
     */
    if (!_enabled)
    {
        if (_status & pts_eos)
            return -pte_eos;

        return -pte_no_enable;
    }

    /* Zero-initialize the instruction in case of error returns. */
    memset(insn, 0, sizeof(*insn));

    /* Fill in a few things from the current decode state.
     *
     * This reflects the state of the last pt_insn_next(), pt_insn_event()
     * or pt_insn_start() call.
     */
    if (_speculative)
        insn->speculative = 1;
    insn->ip = _ip;
    insn->mode = _mode;
    insn->size = sizeof(insn->raw);

    if (!section->read(insn->raw, &insn->size, _ip))
    {
        if (!(section = _image->find(_ip)))
            return -pte_nomap;

        insn->size = sizeof(insn->raw);
        if (!section->read(insn->raw, &insn->size, _ip))
            return -pte_bad_insn;
    }

    if (pt_ild_decode(insn, iext) < 0)
        return -pte_bad_insn;

    /* Check for events that bind to the current instruction.
     *
     * If an event is indicated, we're done.
     */
    status = pt_insn_check_insn_event(insn, iext);
    if (status != 0)
    {
        if (status < 0)
            return status;

        if (status & pts_event_pending)
            return status;
    }

    /* Determine the next instruction's IP. */
    if (_ic_map.count({section, _ip}))
    {
        _ip = _ic_map[{section, _ip}];
    }
    else
    {
        status = pt_insn_proceed(insn, iext);
        if (status < 0)
            return status;
    }

    /* Indicate events that bind to the new IP.
     *
     * Although we only look at the IP for binding events, we pass the
     * decoded instruction in order to handle errata.
     */
    return pt_insn_check_ip_event(insn, iext);
}

int PTJVMDecoder::decoder_process_jitcode(JitSection *section)
{
    int status;
    PCStackInfo *info = nullptr;
    bool tow = false;

    status = pt_insn_start();
    if (status != 0)
    {
        /* errcode < 0 indicates error */
        if (status < 0)
        {
            std::cerr << "PTJVMDecoder error: Insn start " << pt_errstr(pt_errcode(status)) << std::endl;
            return status;
        }

        /* event if decoder._status might have event pending
         * it might not be handled this time
         */
        if (status & pts_event_pending)
        {
            status = pt_insn_drain_events(status);
            if (status < 0)
            {
                std::cerr << "PTJVMDecoder error: Drain insn events " << pt_errstr(pt_errcode(status)) << std::endl;
                return status;
            }
        }
    }

    for (;;)
    {
        struct pt_insn insn;
        struct pt_insn_ext iext;

        status = pt_insn_next(section, &insn, &iext);
        if (status < 0)
        {
            if (status == -pte_eos || status == -pte_nomap)
                break;

            std::cerr << "PTJVMDecoder error: Next insn " << pt_errstr(pt_errcode(status)) << std::endl;
            break;
        }

        /* event if decoder._status might have event pending
         * it might not be handled this time
         */
        if (status & pts_event_pending)
        {
            status = pt_insn_drain_events(status);
            if (status < 0)
            {
                if (status = -pte_eos)
                    break;

                std::cerr << "PTJVMDecoder error: Drain insn events " << pt_errstr(pt_errcode(status)) << std::endl;
                break;
            }
        }

        status = decoder_record_jitcode(section, &insn);
        if (status < 0)
        {
            std::cerr << "PTJVMDecoder error: Record Jit " << pt_errstr(pt_errcode(status)) << std::endl;
            break;
        }
    }

    return status;
}

int PTJVMDecoder::decoder_record_intercode(uint64_t ip)
{
    bool recorded = true;
    if (const Method *method = JVMRuntime::method_entry(ip))
    {
        recorded = _record.record_method_entry(method->id());
    }
    else if (const Method *method = JVMRuntime::method_exit(ip))
    {
        recorded = _record.record_method_exit(method->id());
    }
    else if (const Method *method = JVMRuntime::method_point(ip))
    {
        recorded = _record.record_method_point(method->id());
    }
    else if (JVMRuntime::not_taken_branch(_ip))
    {
        recorded = _record.record_branch_not_taken();
    }
    else if (JVMRuntime::taken_branch(_ip))
    {
        recorded = _record.record_branch_taken();
    }
    else if (JVMRuntime::in_bci_table(_ip))
    {
        recorded = _record.record_bci(JVMRuntime::bci(ip));
    }
    else if (JVMRuntime::in_switch_table(_ip))
    {
        recorded =  _record.record_switch_case(JVMRuntime::switch_case(ip));
    }
    else if (JVMRuntime::switch_default(_ip))
    {
        recorded = _record.record_switch_default();
    }
    else if (JVMRuntime::ret_code(ip))
    {
        recorded = _record.record_ret_code();
    }
    else if (JVMRuntime::deoptimization(_ip))
    {
        recorded = _record.record_deoptimization();
    }
    else if (JVMRuntime::throw_exception(ip))
    {
        recorded = _record.record_throw_exception();
    }
    else if (JVMRuntime::pop_frame(ip))
    {
        recorded = _record.record_pop_frame();
    }
    else if (JVMRuntime::earlyret(ip))
    {
        recorded = _record.record_earlyret();
    }
    else if (JVMRuntime::non_invoke_ret(ip))
    {
        recorded = _record.record_non_invoke_ret();
    }
    else if (JVMRuntime::java_call_begin(ip))
    {
        recorded = _record.record_java_call_begin();
    }
    else if (JVMRuntime::java_call_end(ip))
    {
        recorded = _record.record_java_call_end();
    }
    else
    {
        return -pte_nomap;
    }
    if (!recorded)
    {
        return -pte_bad_context;
    }
    return 0;
}

void PTJVMDecoder::decoder_process_ip()
{
    if (JitSection *section = _image->find(_ip))
    {
        int errcode = decoder_process_jitcode(section);
        if (errcode < 0 && errcode != -pte_eos && errcode != -pte_nomap)
        {
            /* error happens while decoding, jit process will print this error */
            _record.record_decode_error();
        }

        /* while processing jit, decoder might query a non-compiled-code ip */
        errcode = decoder_record_intercode(_ip);
        if (errcode < 0 && errcode != -pte_nomap)
        {
            /* error happens while decoding */
            std::cerr << "PTJVMDecoder error: Fail to record Inter codes" << std::endl;
            _record.record_decode_error();
        }
    }
    else
    {
        int errcode = decoder_record_intercode(_ip);
        if (errcode < 0)
        {
            /* error happens while decoding */
            std::cerr << "PTJVMDecoder error: Fail to record Inter codes" << std::endl;
            _record.record_decode_error();
        }
    }
}

int PTJVMDecoder::decoder_drain_events()
{
    int errcode = 0;
    bool unresolved = false;

    uint64_t disabled_ip = 0ul;

    while (decoder_event_pending())
    {
        if (!_process_event)
            return -pte_bad_query;

        switch (_event.type)
        {
        default:
            return -pte_bad_query;

        case ptev_enabled:
            errcode = decoder_process_enabled();
            if (errcode < 0)
                return errcode;

            /** If tracing was disabled & enabled asynchronously, ignore */
            if (_ip != disabled_ip)
                unresolved = true;

            break;

        case ptev_async_disabled:
            disabled_ip = _event.variant.async_disabled.at;

            errcode = decoder_process_disabled();
            if (errcode < 0)
                return errcode;
            
            break;

        case ptev_disabled:
            disabled_ip = 0ul;

            errcode = decoder_process_disabled();
            if (errcode < 0)
                return errcode;

            break;

        case ptev_async_branch:
            errcode = decoder_process_async_branch();
            if (errcode < 0)
                return errcode;

            unresolved = true;
            break;

        case ptev_async_paging:
        case ptev_paging:
            errcode = decoder_process_paging();
            if (errcode < 0)
                return errcode;

            break;

        case ptev_async_vmcs:
        case ptev_vmcs:
            errcode = decoder_process_vmcs();
            if (errcode < 0)
                return errcode;

            break;

        case ptev_overflow:
            errcode = decoder_process_overflow();
            if (errcode < 0)
                return errcode;

            unresolved = true;

            break;

        case ptev_exec_mode:
            errcode = decoder_process_exec_mode();
            if (errcode < 0)
                return errcode;

            break;

        case ptev_tsx:
            errcode = decoder_process_tsx();
            if (errcode < 0)
                return errcode;

            break;

        case ptev_stop:
            errcode = decoder_process_stop();
            if (errcode < 0)
                return errcode;

            break;

        case ptev_exstop:
        case ptev_mwait:
        case ptev_pwre:
        case ptev_pwrx:
        case ptev_ptwrite:
        case ptev_tick:
        case ptev_cbr:
        case ptev_mnt:
            break;
        }

        _process_event = 0;

        /* This completes processing of the current event. */
        if (unresolved)
        {
            decoder_process_ip();
            unresolved = false;
        }
    }

    return errcode;
}

void PTJVMDecoder::decode()
{
    int status, taken;
    for (;;)
    {
        status = decoder_sync_forward();
        if (status < 0)
        {
            if (status == -pte_eos)
                break;

            std::cerr << "PTJVMDecoder error: Sync forward " << pt_errstr(pt_errcode(status)) << std::endl;
            continue;
        }

        for (;;)
        {
            status = decoder_drain_events();
            if (status < 0)
                break;

            status = decoder_cond_branch(&taken);
            if (status < 0)
            {
                status = decoder_indirect_branch(&_ip);
                if (status < 0)
                    break;

                if (status & pts_ip_suppressed)
                    continue;

                decoder_process_ip();
            }
        }

        if (!status)
            status = -pte_internal;

        /* We're done when we reach the end of the trace stream. */
        if (status == -pte_eos)
            break;
        else
            std::cerr << "PTJVMDecoder error: Loop " << pt_errstr(pt_errcode(status)) << std::endl;
    }

    /* Decode at end, mark record end, and drain all events before _end_time */
    assert(_time < _end_time);
    _time = _end_time;
    decoder_drain_jvm_events(_end_time);
    decoder_drain_sideband_events(_end_time);
    _record.switch_out(_end_time);
    return;
}

PTJVMDecoder::PTJVMDecoder(const struct pt_config *config, DecodeData *const data,
                           uint32_t cpu, std::pair<uint64_t, uint64_t> &time)
    : _record(data), _tid(0), _start_time(time.first), _end_time(time.second)
{
    if (pt_config_from_user(&_config, config) < 0)
    {
        std::cerr << "PTJVMDecoder error: Init pt config" << std::endl;
        exit(-1);
    }

    _sideband = new Sideband(cpu);

    _jvm = new JVMRuntime();

    _image = new JitImage("jitted-code");

    if ((_qry = pt_qry_alloc_decoder(&_config)) == nullptr)
    {
        std::cerr << "PTJVMDecoder error: Allocate query decoder" << std::endl;
        exit(-1);
    }
}

PTJVMDecoder::~PTJVMDecoder()
{
    delete _sideband;
    _sideband = nullptr;

    delete _jvm;
    _jvm = nullptr;

    delete _image;
    _image = nullptr;

    pt_qry_free_decoder(_qry);
    _qry = nullptr;
}

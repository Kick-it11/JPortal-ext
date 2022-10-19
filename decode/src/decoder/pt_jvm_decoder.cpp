#include "decoder/pt_jvm_decoder.hpp"
#include "insn/pt_ild.hpp"
#include "insn/pt_retstack.hpp"
#include "runtime/jit_image.hpp"
#include "sideband/sideband.hpp"

#include <iostream>

int PTJVMDecoder::decoder_process_enabled()
{
    struct pt_event *ev;

    ev = &_event;

    /* Use status update events to diagnose inconsistencies. */
    if (ev->status_update) {
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
    if (ev->status_update) {
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

/* Query an indirect branch.
 * Can be used by jit and normal
 * Returns zero on success, a negative error code otherwise.
 */
int PTJVMDecoder::decoder_indirect_branch(uint64_t *ip)
{
    uint64_t evip;
    int status, errcode;

    status = pt_qry_indirect_branch(_qry, ip);
    if (status < 0)
        return status;

    /** do not generate tick event, if time has changed call time_change */
    decoder_time_change();

    _status = status;

    return status;
}

/* Query a conditional branch.
 * Can be used by jit and normal
 * Returns zero on success, a negative error code otherwise.
 */
int PTJVMDecoder::decoder_cond_branch(int *taken)
{
    int status, errcode;

    status = pt_qry_cond_branch(_qry, taken);
    if (status < 0)
        return status;

    _status = status;

    decoder_time_change();

    return status;
}

/* Move forward to next synchronized point */
int PTJVMDecoder::decoder_sync_forward() {
    int status;

    pt_insn_reset();

    status = pt_qry_sync_forward(_qry, &_ip);

    if (status < 0)
        return status;

    _status = status;

    decoder_time_change();

    return status;
}

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

    decoder_time_change();

    _process_event = 1;

    _status = status;

    return 1;
}

void PTJVMDecoder::decoder_time_change()
{
    uint64_t tsc;

    pt_qry_time(_qry, &tsc, NULL, NULL);

    if (tsc == _time)
        return;

    _time = tsc;

    _jvm->move_on(_time);

    /** data loss, if loss set, do not try to change it. */
    bool loss = false;

    /** iterate all sideband ( perf event )*/
    while (_sideband->event(_time)) {
        uint32_t sideband_tid = _sideband->tid();
        if (_sideband->loss())
            loss = true;

        uint32_t java_tid = _jvm->get_java_tid(sideband_tid);

        if (loss || _tid != java_tid) {
            _tid = java_tid;
            _record.switch_out(loss);
            _record.switch_in(_tid, _time, loss);
        }
    }
    _record.switch_in(_tid, _time, loss);
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
    JitSection* section = _jvm->image()->find(insn->ip + isize);
    if (!section)
        return -pte_bad_insn;

    if (!section->read(&insn->raw[isize], &remaining, insn->ip + isize)) {
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
    if (errcode < 0) {
        if (errcode != -pte_bad_insn)
            return errcode;

        /* If instruction length decode already determined the size,
         * there's no point in reading more bytes.
         */
        if (insn->size != (uint8_t) size)
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
    uint8_t size;

    if (!insn)
        return -pte_internal;

    JitSection* section = _jvm->image()->find(insn->ip);
    if (!section)
        return -pte_bad_insn;

    if (!section->read(insn->raw, &size, insn->ip)) {
        /* We should have gotten an error if we were not able to read at
         * least one byte.  Check this to guarantee termination.
         */
        return -pte_bad_insn;
    }
    /* We initialize @insn->size to the maximal possible size.  It will be
     * set to the actual size during instruction decode.
     */
    insn->size = (uint8_t) size;

    errcode = pt_ild_decode(insn, iext);
    if (errcode < 0) {
        if (errcode != -pte_bad_insn)
            return errcode;

        /* If instruction length decode already determined the size,
         * there's no point in reading more bytes.
         */
        if (insn->size != (uint8_t) size)
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

    while (insn.ip != end) {
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

int PTJVMDecoder::pt_insn_reset()
{
    /* _mode, _ip, _status, _speculative, _asid */
    _process_insn = 0;
    _bound_paging = 0;
    _bound_vmcs = 0;
    _bound_ptwrite = 0;

    pt_retstack_init(&_retstack);
    return 0;
}

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

int PTJVMDecoder::pt_insn_start()
{
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

    case ptic_return: {
        int taken, status;

        /* Check for a compressed return. */
        status = decoder_cond_branch(&taken);
        if (status >= 0) {
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
        if (_config.errata.bdm64) {
            status = pt_insn_handle_erratum_bdm64(ev, insn, iext);
            if (status < 0)
                return status;
        }
    }

    if (_ip != ev->variant.tsx.ip)
        return 1;

    return 0;
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

    case ptev_async_disabled: {
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

int PTJVMDecoder::pt_insn_drain_events()
{
    int errcode = _status;

    while (_status & pts_event_pending) {

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

            errcode = decoder_process_enabled();
            if (errcode < 0)
                return errcode;

            break;

        case ptev_async_disabled:
            if (!_event.ip_suppressed && _ip != _event.variant.async_disabled.at)
                return -pte_bad_query;

            /* fallthrough */

        case ptev_disabled:
            errcode = decoder_process_disabled();
            if (errcode < 0)
                return errcode;

            break;

        case ptev_async_branch:
            if (_ip != _event.variant.async_branch.from)
                return -pte_bad_query;

            errcode = decoder_process_async_branch();
            if (errcode < 0)
                return errcode;

            break;

        case ptev_async_paging:
            if (!_event.ip_suppressed && _ip != _event.variant.async_paging.ip)
                return -pte_bad_query;

            /* faillthrough */

        case ptev_paging:
            errcode = decoder_process_paging();
            if (errcode < 0)
                return errcode;

            break;

        case ptev_async_vmcs:
            if (!_event.ip_suppressed && _ip != _event.variant.async_vmcs.ip)
                return -pte_bad_query;

        case ptev_vmcs:
            errcode = decoder_process_vmcs();
            if (errcode < 0)
                return errcode;

            break;

        case ptev_overflow:
            errcode = decoder_process_overflow();
            if (errcode < 0)
                return errcode;

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
            errcode = pt_insn_check_insn_event(&_insn, &_iext);

            if (errcode != 0)
            {
                if (errcode < 0)
                    return errcode;

                if (_status & pts_event_pending)
                    continue;
            }

            /* Proceed to the next instruction. */
            errcode = pt_insn_proceed_postponed();
            if (errcode < 0)
                return errcode;
        }

        /* Indicate further events that bind to the same IP. */
        errcode = pt_insn_check_ip_event(NULL, NULL);
        if (errcode < 0)
            return errcode;
    }

    return _status;
}

int PTJVMDecoder::decoder_record_jitcode(JitSection *section, PCStackInfo* &info, bool &tow)
{
    if (!section)
        return -pte_internal;

    int idx = -1;
    struct PCStackInfo *cur = section->find(_ip, idx);
    if (!cur)
        return 0;
    else if (cur != info)
    {
        if (idx == 0) _record.add_jitcode(_time, section, cur, _ip);
        else if (_ip == section->record()->pcinfo[idx - 1].pc)
            tow = true;
    }
    else if (tow)
    {
        _record.add_jitcode(_time, section, info, _ip);
        tow = false;
    }
    info = cur;
    return 0;
}

int PTJVMDecoder::pt_insn_next(JitSection* &section, struct pt_insn &insn) {
    struct pt_insn_ext iext;
    int status, isid;
    uint8_t insn_size;

    /* Tracing must be enabled.
     *
     * If it isn't we should be processing events until we either run out of
     * trace or process a tracing enabled event.
     */
    if (!_enabled) {
        if (_status & pts_eos)
            return -pte_eos;

        return -pte_no_enable;
    }

    /* Zero-initialize the instruction in case of error returns. */
    memset(&insn, 0, sizeof(insn));

    /* Fill in a few things from the current decode state.
     *
     * This reflects the state of the last pt_insn_next(), pt_insn_event()
     * or pt_insn_start() call.
     */
    if (_speculative)
        insn.speculative = 1;
    insn.ip = _ip;
    insn.mode = _mode;
    insn_size = sizeof(insn.raw);

    if (!section || !section->read(insn.raw, &insn_size, _ip)) {
        if (!(section = _jvm->image()->find(_ip)))
            return -pte_nomap;

        insn_size = sizeof(insn.raw);
        if (!section->read(insn.raw, &insn_size, _ip))
        {
            std::cerr << "PTJVMDecoder error: compiled code's section" << std::endl;
            return -pte_bad_insn;
        }
    }

    if (pt_ild_decode(&insn, &iext) < 0)
    {
        std::cerr << "PTJVMDecoder error: compiled code's ild" << std::endl; 
        return -pte_bad_insn;
    }

    /* Check for events that bind to the current instruction.
     *
     * If an event is indicated, we're done.
     */
    status = pt_insn_check_insn_event(&insn, &iext);
    if (status != 0) {
        if (status < 0)
            return status;

        if (status & pts_event_pending)
            return status;
    }

    /* Determine the next instruction's IP. */
    uint64_t ip_from_ic = _ip;
    if (_jvm->get_ic(ip_from_ic, section)) {
        _ip = ip_from_ic;
    } else {
        status = pt_insn_proceed(&insn, &iext);
        if (status < 0)
            return status;
    }

    /* Indicate events that bind to the new IP.
     *
     * Although we only look at the IP for binding events, we pass the
     * decoded instruction in order to handle errata.
     */
    return pt_insn_check_ip_event(&insn, &iext);
}

int PTJVMDecoder::decoder_process_jitcode()
{
    int errcode;
    struct pt_insn insn;
    JitSection *section = nullptr;
    PCStackInfo* info = nullptr;
    bool tow = false;

    errcode = pt_insn_reset();
    if (errcode < 0)
        return errcode;

    errcode = pt_insn_start();
    if (errcode != 0)
    {
        /* errcode < 0 indicates error */
        if (errcode < 0)
            return errcode;

        errcode = pt_insn_drain_events();
    }

    for (;;)
    {
        errcode = pt_insn_next(section, insn);
        if (errcode < 0) {
            if (errcode = -pte_eos)
                break;

            std::cerr << "PTJVMDecoder error: jitcode next" << std::endl;
            break;
        }

        errcode = decoder_record_jitcode(section, info, tow);
        if (errcode < 0)
        {
            std::cerr << "PTJVMDecoder error: jitcode result" << std::endl;
            break;
        }

        errcode = pt_insn_drain_events();
        if (errcode < 0) {
            if (errcode = -pte_eos)
                break;

            std::cerr << "PTJVMDecoder error: jitcode result" << std::endl;
            break;
        }
    }

    return errcode;
}

int PTJVMDecoder::decoder_record_bytecode(Bytecodes::Code bytecode)
{
    Bytecodes::Code java_code = bytecode;
    Bytecodes::Code follow_code;
    int errcode;
    Bytecodes::java_bytecode(java_code, follow_code);
    _record.add_bytecode(_time, java_code);
    if (follow_code != Bytecodes::_illegal)
    {
        _record.add_bytecode(_time, follow_code);
    }
    return 0;
}

int PTJVMDecoder::decoder_process_ip()
{
    int errcode = 0;
    Bytecodes::Code bytecode;
    CodeletsEntry::Codelet codelet = CodeletsEntry::entry_match(_ip, bytecode);
    switch (codelet)
    {
    case (CodeletsEntry::_illegal):
    {
        errcode = decoder_process_jitcode();
        if (errcode < 0)
            return errcode;

        /* while processing jit, decoder might query a non-compiled-code ip */
        codelet = CodeletsEntry::entry_match(_ip, bytecode);
        if (codelet != CodeletsEntry::_illegal)
            return decoder_process_ip();

        return errcode;
    }
    case (CodeletsEntry::_bytecode):
    {
        return decoder_record_bytecode(bytecode);
    }
    default:
        _record.add_codelet(codelet);
        return errcode;
    }
}

int PTJVMDecoder::decoder_drain_events()
{
    int errcode = 0;
    bool unresolved = false;

    uint64_t async_disabled_ip = 0ul;

    while (_status & pts_event_pending)
    {
        if (!_process_event)
            return -pte_bad_query;

        errcode = decoder_event_pending();
        if (errcode < 0)
            return errcode;

        switch (_event.type)
        {
        default:
            return -pte_bad_query;

        case ptev_enabled:
            errcode = decoder_process_enabled();
            if (errcode < 0)
                return errcode;

            /** If tracing was disabled asynchronously, ignore */
            if (_ip != async_disabled_ip)
                unresolved = true;
            
            break;

        case ptev_async_disabled:
            async_disabled_ip = _event.variant.async_disabled.at;

            /* fallthrough */

        case ptev_disabled:
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

        /* This completes processing of the current event. */
        if (unresolved) {
            errcode = decoder_process_ip();
            if (errcode < 0)
                return errcode;
            unresolved = false;
        }

        _process_event = 0;
    }

    return errcode;
}

void PTJVMDecoder::decode()
{
    int errcode, taken;
    for (;;)
    {
        errcode = decoder_sync_forward();
        if (errcode < 0)
        {
            if (errcode == -pte_eos)
                break;

            std::cerr << "PTJVMDecoder error: " << pt_errstr(pt_errcode(errcode)) << std::endl;
            continue;
        }

        for (;;)
        {
            errcode = decoder_drain_events();
            if (errcode < 0)
                break;

            errcode = decoder_cond_branch(&taken);
            if (errcode < 0)
            {
                errcode = decoder_indirect_branch(&_ip);
                if (errcode < 0)
                    break;

                errcode = decoder_process_ip();
                if (errcode < 0)
                    break;
            }
        }

        if (!errcode)
            errcode = -pte_internal;

        /* We're done when we reach the end of the trace stream. */
        if (errcode == -pte_eos)
            break;
        else
            std::cerr << "PTJVMDecoder error: " << pt_errstr(pt_errcode(errcode))
                      << " " << _time << std::endl;
    }
}

PTJVMDecoder::PTJVMDecoder(const struct pt_config &config, TraceData &trace, uint32_t cpu)
        : _record(trace), _tid(-1)
{
    pt_config_from_user(&_config, &config);

    _sideband = new Sideband(cpu);

    _jvm = new JVMRuntime();

    if ((_qry = pt_qry_alloc_decoder(&_config)) == nullptr)
    {
        std::cerr << "PTJVMDecoder: fail to allocate query decoder." << std::endl;
        exit(-1);
    }
}

PTJVMDecoder::~PTJVMDecoder() {
    delete _sideband;
    _sideband = nullptr;

    delete _jvm;
    _jvm = nullptr;

    pt_qry_free_decoder(_qry);
    _qry = nullptr;
}

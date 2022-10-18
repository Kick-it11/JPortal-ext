#include "decoder/pt_jvm_decoder.hpp"
#include "decoder/decode_result.hpp"
#include "runtime/jvm_runtime.hpp"
#include "runtime/jit_image.hpp"
#include "runtime/jit_section.hpp"
#include "insn/pt_insn.hpp"
#include "insn/pt_ild.hpp"
#include "pt/pt.hpp"

#include <iostream>

using std::cerr;
using std::endl;

void PTJVMDecoder::reset_decoder()
{
    _mode = ptem_unknown;
    _ip = 0ull;
    _status = 0;
    _enabled = 0;
    _process_event = 0;
    _speculative = 0;
    _process_insn = 0;
    _bound_paging = 0;
    _bound_vmcs = 0;
    _bound_ptwrite = 0;

    pt_asid_init(&_asid);
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
	uint16_t isize, remaining;

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
	insn->size += (uint8_t) remaining;

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
    uint16_t size;

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
				enum pt_exec_mode mode, size_t steps)
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


void PTJVMDecoder::time_change()
{
    _jvm->move_on(_time);

    /** data loss, if loss set, do not try to change it. */
    bool loss = false;

    /** iterate all sideband ( perf event )*/
    while (_sideband->event(_time)) {
        long sideband_tid = _sideband->tid();
        if (_sideband->loss())
            loss = true;

        long java_tid = _jvm->get_java_tid(sideband_tid);

        if (loss || _tid != java_tid) {
            _tid = java_tid;
            _record.switch_out(loss);
            _record.switch_in(_tid, _time, loss);
        }
    }
    _record.switch_in(_tid, _time, loss);
}

int PTJVMDecoder::check_erratum_skd022()
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

int PTJVMDecoder::handle_erratum_skd022()
{
    struct pt_event *ev;
    uint64_t ip;
    int errcode;

    errcode = check_erratum_skd022();
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

int PTJVMDecoder::pt_insn_at_disabled_event(const struct pt_event *ev,
                                            const struct pt_insn *insn,
                                            const struct pt_insn_ext *iext,
                                            const struct pt_config *config)
{
    if (!ev || !insn || !iext || !config)
        return -pte_internal;

    if (ev->ip_suppressed)
    {
        if (pt_insn_is_far_branch(insn, iext) || pt_insn_changes_cpl(insn, iext) ||
            pt_insn_changes_cr3(insn, iext))
            return 1;

        /* If we don't have a filter configuration we assume that no
         * address filters were used and the erratum does not apply.
         *
         * We might otherwise disable tracing too early.
         */
        if (config->addr_filter.config.addr_cfg && config->errata.skl014 &&
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

int PTJVMDecoder::event_pending()
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
    return 1;
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

enum
{
    /* The maximum number of steps to take when determining whether the
     * event location can be reached.
     */
    bdm64_max_steps = 0x100
};

/* Try to work around erratum BDM64.
 *
 * If we got a transaction abort immediately following a branch that produced
 * trace, the trace for that branch might have been corrupted.
 *
 * Returns a positive integer if the erratum was handled.
 * Returns zero if the erratum does not seem to apply.
 * Returns a negative error code otherwise.
 */
int PTJVMDecoder::handle_erratum_bdm64(const struct pt_event *ev,
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

    if (insn && iext && _config.errata.bdm64)
    {
        status = handle_erratum_bdm64(ev, insn, iext);
        if (status < 0)
            return status;
    }

    if (_ip != ev->variant.tsx.ip)
        return 1;

    return 0;
}

int PTJVMDecoder::pt_insn_check_ip_event(const struct pt_insn *insn,
                                         const struct pt_insn_ext *iext)
{
    struct pt_event *ev;
    int status;

    status = event_pending();
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
        break;

    case ptev_enabled:
        return pt_insn_status(pts_event_pending);

    case ptev_async_disabled:
        if (ev->variant.async_disabled.at != _ip)
            break;

        if (_config.errata.skd022)
        {
            int errcode;

            errcode = handle_erratum_skd022();
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

int PTJVMDecoder::pt_insn_check_insn_event(const struct pt_insn *insn,
                                           const struct pt_insn_ext *iext)
{
    struct pt_event *ev;
    int status;

    status = event_pending();
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

int PTJVMDecoder::pt_insn_clear_postponed()
{
    _process_insn = 0;
    _bound_paging = 0;
    _bound_vmcs = 0;
    _bound_ptwrite = 0;

    return 0;
}

/* Query an indirect branch.
 *
 * Returns zero on success, a negative error code otherwise.
 */
int PTJVMDecoder::pt_insn_indirect_branch(uint64_t *ip)
{
    uint64_t evip;
    int status, errcode;

    evip = _ip;

    status = pt_qry_indirect_branch(_qry, ip);
    if (status < 0)
        return status;

    return status;
}

/* Query a conditional branch.
 *
 * Returns zero on success, a negative error code otherwise.
 */
int PTJVMDecoder::pt_insn_cond_branch(int *taken)
{
    int status, errcode;

    status = pt_qry_cond_branch(_qry, taken);
    if (status < 0)
        return status;

    return status;
}

int PTJVMDecoder::pt_insn_proceed(const struct pt_insn *insn,
                                  const struct pt_insn_ext *iext)
{
    if (!insn || !iext)
        return -pte_internal;

    _ip += insn->size;
    switch (insn->iclass)
    {
    case ptic_ptwrite:
    case ptic_other:
        return 0;

    case ptic_cond_jump:
    {
        int status, taken;

        status = pt_insn_cond_branch(&taken);
        if (status < 0)
            return status;

        _status = status;
        if (!taken)
            return 0;

        break;
    }

    case ptic_call:
        break;

    /* return compression is disabled */
    case ptic_return:
        break;

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

        status = pt_insn_indirect_branch(&_ip);

        if (status < 0)
            return status;

        _status = status;

        /* We do need an IP to proceed. */
        if (status & pts_ip_suppressed)
            return -pte_noip;
    }

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

int PTJVMDecoder::pt_insn_process_enabled()
{
    struct pt_event *ev;

    ev = &_event;

    /* This event can't be a status update. */
    if (ev->status_update)
        return -pte_bad_context;

    /* We must have an IP in order to start decoding. */
    if (ev->ip_suppressed)
        return -pte_noip;

    // /* We must currently be disabled. */
    // if (_enabled)
    //   return -pte_bad_context;

    _ip = ev->variant.enabled.ip;
    _enabled = 1;

    return 0;
}

int PTJVMDecoder::pt_insn_process_disabled()
{
    struct pt_event *ev;

    ev = &_event;

    /* This event can't be a status update. */
    if (ev->status_update)
        return -pte_bad_context;

    // /* We must currently be enabled. */
    // if (!_enabled)
    //   return -pte_bad_context;

    /* We preserve @_ip.  This is where we expect tracing to resume
     * and we'll indicate that on the subsequent enabled event if tracing
     * actually does resume from there.
     */
    _enabled = 0;

    return 0;
}

int PTJVMDecoder::pt_insn_process_async_branch()
{
    struct pt_event *ev;

    ev = &_event;

    /* This event can't be a status update. */
    if (ev->status_update)
        return -pte_bad_context;

    // /* Tracing must be enabled in order to make sense of the event. */
    // if (!_enabled)
    //   return -pte_bad_context;

    _ip = ev->variant.async_branch.to;

    return 0;
}

int PTJVMDecoder::pt_insn_process_paging()
{
    uint64_t cr3;
    int errcode;

    cr3 = _event.variant.paging.cr3;
    if (_asid.cr3 != cr3)
    {
        _asid.cr3 = cr3;
    }

    return 0;
}

int PTJVMDecoder::pt_insn_process_overflow()
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

int PTJVMDecoder::pt_insn_process_exec_mode()
{
    enum pt_exec_mode mode;
    struct pt_event *ev;

    ev = &_event;
    mode = ev->variant.exec_mode.mode;

    /* Use status update events to diagnose inconsistencies. */
    if (ev->status_update && _enabled && _mode != ptem_unknown &&
        _mode != mode)
        return -pte_bad_status_update;

    _mode = mode;

    return 0;
}

int PTJVMDecoder::pt_insn_process_tsx()
{

    _speculative = _event.variant.tsx.speculative;

    return 0;
}

int PTJVMDecoder::pt_insn_process_stop()
{
    struct pt_event *ev;

    ev = &_event;

    /* This event can't be a status update. */
    if (ev->status_update)
        return -pte_bad_context;

    // /* Tracing is always disabled before it is stopped. */
    // if (_enabled)
    //   return -pte_bad_context;

    return 0;
}

int PTJVMDecoder::pt_insn_process_vmcs()
{
    uint64_t vmcs;
    int errcode;

    vmcs = _event.variant.vmcs.base;
    if (_asid.vmcs != vmcs)
    {
        _asid.vmcs = vmcs;
    }
    return 0;
}

int PTJVMDecoder::pt_insn_event()
{
    struct pt_event *ev;
    int status;

    /* We must currently process an event. */
    if (!_process_event)
        return -pte_bad_query;

    ev = &_event;
    switch (ev->type)
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
        if (_ip == ev->variant.enabled.ip)
            ev->variant.enabled.resumed = 1;

        status = pt_insn_process_enabled();
        if (status < 0)
            return status;

        break;

    case ptev_async_disabled:
        if (!ev->ip_suppressed && _ip != ev->variant.async_disabled.at)
            return -pte_bad_query;

    case ptev_disabled:
        status = pt_insn_process_disabled();
        if (status < 0)
            return status;

        break;

    case ptev_async_branch:
        if (_ip != ev->variant.async_branch.from)
            return -pte_bad_query;

        status = pt_insn_process_async_branch();
        if (status < 0)
            return status;

        break;

    case ptev_async_paging:
        if (!ev->ip_suppressed && _ip != ev->variant.async_paging.ip)
            return -pte_bad_query;

    case ptev_paging:
        status = pt_insn_process_paging();
        if (status < 0)
            return status;

        break;

    case ptev_async_vmcs:
        if (!ev->ip_suppressed && _ip != ev->variant.async_vmcs.ip)
            return -pte_bad_query;

    case ptev_vmcs:
        status = pt_insn_process_vmcs();
        if (status < 0)
            return status;

        break;

    case ptev_overflow:
        status = pt_insn_process_overflow();
        if (status < 0)
            return status;

        break;

    case ptev_exec_mode:
        status = pt_insn_process_exec_mode();
        if (status < 0)
            return status;

        break;

    case ptev_tsx:
        status = pt_insn_process_tsx();
        if (status < 0)
            return status;

        break;

    case ptev_stop:
        status = pt_insn_process_stop();
        if (status < 0)
            return status;

        break;

    case ptev_exstop:
        if (!ev->ip_suppressed && _enabled &&
            _ip != ev->variant.exstop.ip)
            return -pte_bad_query;

        break;

    case ptev_mwait:
        if (!ev->ip_suppressed && _enabled &&
            _ip != ev->variant.mwait.ip)
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
                return status;
        }

        /* Proceed to the next instruction. */
        status = pt_insn_proceed_postponed();
        if (status < 0)
            return status;
    }

    /* Indicate further events that bind to the same IP. */
    return pt_insn_check_ip_event(NULL, NULL);
}

int PTJVMDecoder::drain_insn_events(int status)
{
    while (status & pts_event_pending)
    {
        status = pt_insn_event();
        if (status < 0)
            return status;
    }
    return status;
}

int PTJVMDecoder::handle_compiled_code_result(JitSection *section)
{
    if (!section)
        return -pte_internal;

    int idx = -1;
    PCStackInfo *pcinfo = section->find(_ip, idx);
    if (!pcinfo)
    {
        return 0;
    }
    else if (pcinfo != _last_pcinfo)
    {
        if (idx == 0 || _ip == section->record()->pcinfo[idx - 1].pc)
            _pcinfo_tow = 1;
    }
    else if (_pcinfo_tow)
    {
        const CompiledMethodDesc *cmd = section->cmd();
        if (!cmd)
            return 0;
        _record.add_jitcode(_time, section, pcinfo, _last_ip);
        _pcinfo_tow = 0;
    }
    _last_pcinfo = pcinfo;
    _last_ip = _ip;
    return 0;
}

int PTJVMDecoder::pt_insn_reset()
{
    _process_insn = 0;
    _bound_paging = 0;
    _bound_vmcs = 0;
    _bound_ptwrite = 0;

    return 0;
}

int PTJVMDecoder::pt_insn_start()
{
    int status = _status;

    if (!(status & pts_ip_suppressed))
        _enabled = 1;

    return pt_insn_check_ip_event(NULL, NULL);
}

int PTJVMDecoder::handle_compiled_code()
{
    int status;
    int errcode;
    struct pt_insn insn;
    struct pt_insn_ext iext;
    JitSection *section = nullptr;

    errcode = pt_insn_reset();
    if (errcode < 0)
        return errcode;

    status = pt_insn_start();
    if (status != 0)
    {
        if (status < 0)
            return status;

        if (status & pts_event_pending)
        {
            status = drain_insn_events(status);

            if (status < 0)
                return status;
        }
    }

    for (;;)
    {
        memset(&insn, 0, sizeof(insn));

        if (_speculative)
            insn.speculative = 1;
        insn.mode = _mode;
        insn.ip = _ip;

        uint16_t insn_size = sizeof(insn.raw);
        if (!section || !section->read(insn.raw, &insn_size, _ip))
        {
            if (!(section = _jvm->image()->find(_ip)))
            {
                break;
            }

            insn_size = sizeof(insn.raw);
            if (!section->read(insn.raw, &insn_size, _ip))
            {
                cerr << "PTJVMDecoder error: compiled code's section" << endl;
                break;
            }
        }

        insn.size += insn_size;
        errcode = pt_ild_decode(&insn, &iext);
        if (errcode < 0)
        {
            cerr << "PTJVMDecoder error: compiled code's ild" << endl; 
            break;
        }
        uint64_t ip = _ip;

        errcode = handle_compiled_code_result(section);
        if (errcode < 0)
        {
            cerr << "PTJVMDecoder error: compiled code's result" << endl;
            break;
        }

        status = pt_insn_check_insn_event(&insn, &iext);
        if (status != 0)
        {
            if (status < 0)
                break;
            if (status & pts_event_pending)
            {
                status = drain_insn_events(status);
                if (status < 0)
                    break;
                continue;
            }
        }

        pt_qry_time(_qry, &_time, NULL, NULL);
        time_change();

        if (_jvm->get_ic(ip, section) && ip != _ip) {
          _ip = ip;
          status = pt_insn_check_ip_event(&insn, &iext);
          if (status != 0) {
            if (status < 0)
              break;
            if (status & pts_event_pending) {
              status = drain_insn_events(status);

              if (status < 0)
                break;
            }
          }
          continue;
        }

        errcode = pt_insn_proceed(&insn, &iext);
        if (errcode < 0)
        {
            if (_process_event && (_event.type == ptev_disabled || _event.type == ptev_tsx))
            {
                cerr << "PTJVMDecoder error: disable" << endl;
            }
            else if (errcode != -pte_eos)
            {
                cerr << "PTJVMDecoder error: proceed" << endl;
            }
            break;
        }
        else if (insn.iclass != ptic_cond_jump &&
                 iext.variant.branch.is_direct &&
                 insn.ip == _ip)
        {
            break;
        }

        status = pt_insn_check_ip_event(&insn, &iext);
        if (status != 0)
        {
            if (status < 0)
                break;
            if (status & pts_event_pending)
            {
                status = drain_insn_events(status);
                if (status < 0)
                    break;
            }
        }
    }

    return status;
}

int PTJVMDecoder::handle_bytecode(Bytecodes::Code bytecode)
{

    int status = _status;
    Bytecodes::Code java_code = bytecode;
    Bytecodes::Code follow_code;
    int errcode;
    Bytecodes::java_bytecode(java_code, follow_code);
    _record.add_bytecode(_time, java_code);
    if (follow_code != Bytecodes::_illegal)
    {
        _record.add_bytecode(_time, follow_code);
    }
    return status;
}

int PTJVMDecoder::ptjvm_result_decode()
{
    int status = 0;
    Bytecodes::Code bytecode;
    CodeletsEntry::Codelet codelet =
        CodeletsEntry::entry_match(_ip, bytecode);
    switch (codelet)
    {
    case (CodeletsEntry::_illegal):
    {
        status = handle_compiled_code();
        if (status < 0)
        {
            if (status != -pte_eos)
            {
                _record.switch_out(true);
                cerr << "PTJVMDecoder error: compiled code decode" << status << endl;
            }
            return 0;
        }
        /* might query a non-compiled-code ip */
        codelet = CodeletsEntry::entry_match(_ip, bytecode);
        if (codelet != CodeletsEntry::_illegal)
            return ptjvm_result_decode();
        return status;
    }
    case (CodeletsEntry::_bytecode):
    {
        status = handle_bytecode(bytecode);
        if (status < 0)
        {
            if (status != -pte_eos)
            {
                _record.switch_out(true);
                cerr << "PTJVMDecoder error: bytecode " << status << endl;
            }
            return 0;
        }

        pt_qry_time(_qry, &_time, NULL, NULL);
        time_change();

        if (_unresolved)
        {
            _unresolved = false;
            status = ptjvm_result_decode();
            if (status < 0)
                return status;
        }
        return status;
    }
    default:
        _record.add_codelet(codelet);
        return status;
    }
}

int PTJVMDecoder::drain_qry_events()
{
    int status = _status;
    _unresolved = false;

    uint64_t async_disabled_ip = -1ul;
    while (status & pts_event_pending)
    {
        status = event_pending();
        if (status < 0)
            return status;

        switch (_event.type)
        {
        default:
            return -pte_bad_query;

        case ptev_enabled:
            status = pt_insn_process_enabled();
            if (status < 0)
                return status;
            if (_ip == _event.variant.enabled.ip &&
                _ip == async_disabled_ip)
            {
                _event.variant.enabled.resumed = 1;
                break;
            }
            _unresolved = true;
            _process_event = 0;
            return status;

        case ptev_async_disabled:
            async_disabled_ip = _event.variant.async_disabled.at;
        case ptev_disabled:
            status = pt_insn_process_disabled();
            if (status < 0)
                return status;
            break;

        case ptev_async_branch:
            status = pt_insn_process_async_branch();
            if (status < 0)
                return status;
            _unresolved = true;
            _process_event = 0;
            return status;

        case ptev_async_paging:
        case ptev_paging:
            status = pt_insn_process_paging();
            if (status < 0)
                return status;
            break;

        case ptev_async_vmcs:
        case ptev_vmcs:
            status = pt_insn_process_vmcs();
            if (status < 0)
                return status;
            break;

        case ptev_overflow:
            status = pt_insn_process_overflow();
            if (status < 0)
                return status;
            _unresolved = true;
            _process_event = 0;
            return status;

        case ptev_exec_mode:
            status = pt_insn_process_exec_mode();
            if (status < 0)
                return status;
            break;

        case ptev_tsx:
            status = pt_insn_process_tsx();
            if (status < 0)
                return status;
            break;

        case ptev_stop:
            status = pt_insn_process_stop();
            if (status < 0)
                return status;
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
        _process_event = 0;
        status = _status;
    }

    return status;
}

void PTJVMDecoder::decode()
{
    int status, taken, errcode;
    for (;;)
    {
        reset_decoder();
        status = pt_qry_sync_forward(_qry, &_ip);
        if (status < 0)
        {
            if (status == -pte_eos)
            {
                break;
            }
            cerr << "PTJVMDecoder error: " << pt_errstr(pt_errcode(status)) << endl;
            _record.switch_out(true);
            return;
        }

        _status = status;

        for (;;)
        {
            status = drain_qry_events();
            if (status < 0)
                break;

            pt_qry_time(_qry, &_time, NULL, NULL);
            time_change();

            if (_unresolved)
            {
                _unresolved = false;
                status = ptjvm_result_decode();
                if (status < 0)
                    break;
                continue;
            }

            status = pt_qry_cond_branch(_qry, &taken);
            if (status < 0)
            {
                status = pt_qry_indirect_branch(_qry, &_ip);
                if (status < 0)
                    break;
                _status = status;
                status = ptjvm_result_decode();
                if (status < 0)
                    break;
            }
            else
            {
                _ip = -1ul;
                _status = status;
            }
        }

        if (!status)
            status = -pte_internal;

        /* We're done when we reach the end of the trace stream. */
        if (status == -pte_eos)
            break;
        else
        {
            cerr << "PTJVMDecoder error: " << status << " " << _time << endl;
            _record.switch_out(true);
            return;
        }
    }

    _record.switch_out(false);
}

PTJVMDecoder::PTJVMDecoder(const struct pt_config &config, TraceData &trace, uint32_t cpu)
        : _config(config), _record(trace), _tid(-1)
{
    _sideband = new Sideband(cpu);

    _jvm = new JVMRuntime();

    if ((_qry = pt_qry_alloc_decoder(&_config)) == nullptr)
    {
        cerr << "PTJVMDecoder: fail to allocate query decoder." << endl;
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

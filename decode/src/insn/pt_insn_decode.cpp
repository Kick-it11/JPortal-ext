#include "insn/pt_insn_decode.hpp"

int pt_insn_at_skl014(const struct pt_event *ev,
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

int pt_insn_at_disabled_event(const struct pt_event *ev,
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

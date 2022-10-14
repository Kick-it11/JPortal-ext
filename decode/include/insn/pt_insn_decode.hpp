#ifndef PT_INSN_DECODE_HPP
#define PT_INSN_DECODE_HPP

#include "insn/pt_insn.hpp"

int pt_insn_at_skl014(const struct pt_event *ev,
                      const struct pt_insn *insn,
                      const struct pt_insn_ext *iext,
                      const struct pt_config *config);

int pt_insn_at_disabled_event(const struct pt_event *ev,
                                            const struct pt_insn *insn,
                                            const struct pt_insn_ext *iext,
                                            const struct pt_config *config);
#endif

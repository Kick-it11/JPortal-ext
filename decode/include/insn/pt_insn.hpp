/*
 * Copyright (c) 2016-2022, Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef PT_INSN_H
#define PT_INSN_H

#include "pt/pt.hpp"

#include <inttypes.h>

/** The instruction class.
 *  *
 *   * We provide only a very coarse classification suitable for reconstructing
 *    * the execution flow.
 *     */
enum pt_insn_class {
    /* The instruction has not been classified. */
    ptic_unknown,

    /* The instruction is something not listed below. */
    ptic_other,

    /* The instruction is a near (function) call. */
    ptic_call,

    /* The instruction is a near (function) return. */
    ptic_return,

    /* The instruction is a near unconditional jump. */
    ptic_jump,

    /* The instruction is a near conditional jump. */
    ptic_cond_jump,

    /* The instruction is a call-like far transfer.
 *      * E.g. SYSCALL, SYSENTER, or FAR CALL.
 *           */
    ptic_far_call,

    /* The instruction is a return-like far transfer.
 *      * E.g. SYSRET, SYSEXIT, IRET, or FAR RET.
 *           */
    ptic_far_return,

    /* The instruction is a jump-like far transfer.
 *      * E.g. FAR JMP.
 *           */
    ptic_far_jump,

    /* The instruction is a PTWRITE. */
    ptic_ptwrite,

    /* The instruction is an indirect jump or a far transfer. */
    ptic_indirect
};

/** The maximal size of an instruction. */
enum {
    pt_max_insn_size    = 15
};

/** A single traced instruction. */
struct pt_insn {
    /** The virtual address in its process. */
    uint64_t ip;

    /** The image section identifier for the section containing this
 *      * instruction.
 *           *
 *                * A value of zero means that the section did not have an identifier.
 *                     * The section was not added via an image section cache or the memory
 *                          * was read via the read memory callback.
 *                               */
    int isid;

    /** The execution mode. */
    enum pt_exec_mode mode;

    /** A coarse classification. */
    enum pt_insn_class iclass;

    /** The raw bytes. */
    uint8_t raw[pt_max_insn_size];

    /** The size in bytes. */
    uint8_t size;

    /** A collection of flags giving additional information:
 *      *
 *           * - the instruction was executed speculatively.
 *                */
    uint32_t speculative:1;

    /** - this instruction is truncated in its image section.
 *      *
 *           *    It starts in the image section identified by \@isid and continues
 *                *    in one or more other sections.
 *                     */
    uint32_t truncated:1;
};

/* A finer-grain classification of instructions used internally. */
typedef enum {
    PTI_INST_INVALID,

    PTI_INST_CALL_9A,
    PTI_INST_CALL_FFr3,
    PTI_INST_CALL_FFr2,
    PTI_INST_CALL_E8,
    PTI_INST_INT,

    PTI_INST_INT3,
    PTI_INST_INT1,
    PTI_INST_INTO,
    PTI_INST_IRET,    /* includes IRETD and IRETQ (EOSZ determines) */

    PTI_INST_JMP_E9,
    PTI_INST_JMP_EB,
    PTI_INST_JMP_EA,
    PTI_INST_JMP_FFr5,    /* REXW? */
    PTI_INST_JMP_FFr4,
    PTI_INST_JCC,
    PTI_INST_JrCXZ,
    PTI_INST_LOOP,
    PTI_INST_LOOPE,    /* aka Z */
    PTI_INST_LOOPNE,    /* aka NE */

    PTI_INST_MOV_CR3,

    PTI_INST_RET_C3,
    PTI_INST_RET_C2,
    PTI_INST_RET_CB,
    PTI_INST_RET_CA,

    PTI_INST_SYSCALL,
    PTI_INST_SYSENTER,
    PTI_INST_SYSEXIT,
    PTI_INST_SYSRET,

    PTI_INST_VMLAUNCH,
    PTI_INST_VMRESUME,
    PTI_INST_VMCALL,
    PTI_INST_VMPTRLD,

    PTI_INST_PTWRITE,

    PTI_INST_UIRET,

    PTI_INST_LAST
} pti_inst_enum_t;

/* Information about an instruction we need internally in addition to the
 * information provided in struct pt_insn.
 */
struct pt_insn_ext {
    /* A more detailed instruction class. */
    pti_inst_enum_t iclass;

    /* Instruction-specific information. */
    union {
        /* For branch instructions. */
        struct {
            /* The branch displacement.
             *
             * This is only valid for direct calls/jumps.
             *
             * The displacement is applied to the address of the
             * instruction following the branch.
             */
            int32_t displacement;

            /* A flag saying whether the branch is direct.
             *
             *   non-zero: direct
             *   zero:     indirect
             *
             * This is expected to go away someday when we extend
             * enum pt_insn_class to distinguish direct and indirect
             * branches.
             */
            uint8_t is_direct;
        } branch;
    } variant;
};


/* Check if the instruction @insn/@iext changes the current privilege level.
 *
 * Returns non-zero if it does, zero if it doesn't (or @insn/@iext is NULL).
 */
extern int pt_insn_changes_cpl(const struct pt_insn *insn,
                               const struct pt_insn_ext *iext);

/* Check if the instruction @insn/@iext changes CR3.
 *
 * Returns non-zero if it does, zero if it doesn't (or @insn/@iext is NULL).
 */
extern int pt_insn_changes_cr3(const struct pt_insn *insn,
                               const struct pt_insn_ext *iext);

/* Check if the instruction @insn/@iext is a (near or far) branch.
 *
 * Returns non-zero if it is, zero if it isn't (or @insn/@iext is NULL).
 */
extern int pt_insn_is_branch(const struct pt_insn *insn,
                             const struct pt_insn_ext *iext);

/* Check if the instruction @insn/@iext is a far branch.
 *
 * Returns non-zero if it is, zero if it isn't (or @insn/@iext is NULL).
 */
extern int pt_insn_is_far_branch(const struct pt_insn *insn,
                                 const struct pt_insn_ext *iext);

/* Check if the instruction @insn/@iext binds to a PIP packet.
 *
 * Returns non-zero if it does, zero if it doesn't (or @insn/@iext is NULL).
 */
extern int pt_insn_binds_to_pip(const struct pt_insn *insn,
                                const struct pt_insn_ext *iext);

/* Check if the instruction @insn/@iext binds to a VMCS packet.
 *
 * Returns non-zero if it does, zero if it doesn't (or @insn/@iext is NULL).
 */
extern int pt_insn_binds_to_vmcs(const struct pt_insn *insn,
                                 const struct pt_insn_ext *iext);

/* Check if the instruction @insn/@iext is a ptwrite instruction.
 *
 * Returns non-zero if it is, zero if it isn't (or @insn/@iext is NULL).
 */
extern int pt_insn_is_ptwrite(const struct pt_insn *insn,
                              const struct pt_insn_ext *iext);

/* Determine the IP of the next instruction.
 *
 * Tries to determine the IP of the next instruction without using trace and
 * provides it in @ip unless @ip is NULL.
 *
 * Returns zero on success, a negative error code otherwise.
 * Returns -pte_bad_query if the IP can't be determined.
 * Returns -pte_internal if @insn or @iext is NULL.
 */
extern int pt_insn_next_ip(uint64_t *ip, const struct pt_insn *insn,
                           const struct pt_insn_ext *iext);

#endif /* PT_INSN_HPP */

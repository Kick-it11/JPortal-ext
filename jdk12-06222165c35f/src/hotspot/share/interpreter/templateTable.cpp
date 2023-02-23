/*
 * Copyright (c) 1997, 2015, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "precompiled.hpp"
#include "gc/shared/barrierSet.hpp"
#include "interpreter/interp_masm.hpp"
#include "interpreter/templateTable.hpp"
#include "runtime/timerTrace.hpp"

#ifdef CC_INTERP

void templateTable_init() {
}

#else

//----------------------------------------------------------------------------------------------------
// Implementation of Template


void Template::initialize(int flags, TosState tos_in, TosState tos_out, generator gen, int arg) {
  _flags   = flags;
  _tos_in  = tos_in;
  _tos_out = tos_out;
  _gen     = gen;
  _arg     = arg;
}


Bytecodes::Code Template::bytecode() const {
  int i = this - TemplateTable::_template_table;
  if (i < 0 || i >= Bytecodes::number_of_codes) i = this - TemplateTable::_template_table_wide;
#ifdef JPORTAL_ENABLE
  if (JPortal) {
    if (i < 0 || i >= Bytecodes::number_of_codes) i = this - TemplateTable::_jportal_template_table;
    if (i < 0 || i >= Bytecodes::number_of_codes) i = this - TemplateTable::_jportal_template_table_wide;
  }
#endif
  return Bytecodes::cast(i);
}


void Template::generate(InterpreterMacroAssembler* masm) {
  // parameter passing
  TemplateTable::_desc = this;
  TemplateTable::_masm = masm;
  // code generation
  _gen(_arg);
  masm->flush();
}


//----------------------------------------------------------------------------------------------------
// Implementation of TemplateTable: Platform-independent helper routines

void TemplateTable::call_VM(Register oop_result, address entry_point) {
  assert(_desc->calls_vm(), "inconsistent calls_vm information");
  _masm->call_VM(oop_result, entry_point);
}


void TemplateTable::call_VM(Register oop_result, address entry_point, Register arg_1) {
  assert(_desc->calls_vm(), "inconsistent calls_vm information");
  _masm->call_VM(oop_result, entry_point, arg_1);
}


void TemplateTable::call_VM(Register oop_result, address entry_point, Register arg_1, Register arg_2) {
  assert(_desc->calls_vm(), "inconsistent calls_vm information");
  _masm->call_VM(oop_result, entry_point, arg_1, arg_2);
}


void TemplateTable::call_VM(Register oop_result, address entry_point, Register arg_1, Register arg_2, Register arg_3) {
  assert(_desc->calls_vm(), "inconsistent calls_vm information");
  _masm->call_VM(oop_result, entry_point, arg_1, arg_2, arg_3);
}


void TemplateTable::call_VM(Register oop_result, Register last_java_sp, address entry_point) {
  assert(_desc->calls_vm(), "inconsistent calls_vm information");
  _masm->call_VM(oop_result, last_java_sp, entry_point);
}


void TemplateTable::call_VM(Register oop_result, Register last_java_sp, address entry_point, Register arg_1) {
  assert(_desc->calls_vm(), "inconsistent calls_vm information");
  _masm->call_VM(oop_result, last_java_sp, entry_point, arg_1);
}


void TemplateTable::call_VM(Register oop_result, Register last_java_sp, address entry_point, Register arg_1, Register arg_2) {
  assert(_desc->calls_vm(), "inconsistent calls_vm information");
  _masm->call_VM(oop_result, last_java_sp, entry_point, arg_1, arg_2);
}


void TemplateTable::call_VM(Register oop_result, Register last_java_sp, address entry_point, Register arg_1, Register arg_2, Register arg_3) {
  assert(_desc->calls_vm(), "inconsistent calls_vm information");
  _masm->call_VM(oop_result, last_java_sp, entry_point, arg_1, arg_2, arg_3);
}


//----------------------------------------------------------------------------------------------------
// Implementation of TemplateTable: Platform-independent bytecodes

void TemplateTable::float_cmp(int unordered_result) {
  transition(ftos, itos);
  float_cmp(true, unordered_result);
}


void TemplateTable::double_cmp(int unordered_result) {
  transition(dtos, itos);
  float_cmp(false, unordered_result);
}


void TemplateTable::_goto(bool jportal) {
  transition(vtos, vtos);
  branch(false, false, jportal);
}


void TemplateTable::goto_w(bool jportal) {
  transition(vtos, vtos);
  branch(false, true, jportal);
}


void TemplateTable::jsr_w(bool jportal) {
  transition(vtos, vtos);       // result is not an oop, so do not transition to atos
  branch(true, true, jportal);
}


void TemplateTable::jsr(bool jportal) {
  transition(vtos, vtos);       // result is not an oop, so do not transition to atos
  branch(true, false, jportal);
}



//----------------------------------------------------------------------------------------------------
// Implementation of TemplateTable: Debugging

void TemplateTable::transition(TosState tos_in, TosState tos_out) {
  assert(_desc->tos_in()  == tos_in , "inconsistent tos_in  information");
  assert(_desc->tos_out() == tos_out, "inconsistent tos_out information");
}


//----------------------------------------------------------------------------------------------------
// Implementation of TemplateTable: Initialization

bool                       TemplateTable::_is_initialized = false;
Template                   TemplateTable::_template_table     [Bytecodes::number_of_codes];
Template                   TemplateTable::_template_table_wide[Bytecodes::number_of_codes];

Template*                  TemplateTable::_desc;
InterpreterMacroAssembler* TemplateTable::_masm;
BarrierSet*                TemplateTable::_bs;

#ifdef JPORTAL_ENABLE
Template                   TemplateTable::_jportal_template_table     [Bytecodes::number_of_codes];
Template                   TemplateTable::_jportal_template_table_wide[Bytecodes::number_of_codes];
#endif

void TemplateTable::def(Bytecodes::Code code, int flags, TosState in, TosState out, void (*gen)(), char filler, bool jportal) {
  assert(filler == ' ', "just checkin'");
  def(code, flags, in, out, (Template::generator)gen, 0, jportal);
}


void TemplateTable::def(Bytecodes::Code code, int flags, TosState in, TosState out, void (*gen)(int arg), int arg, bool jportal) {
  // should factor out these constants
  const int ubcp = 1 << Template::uses_bcp_bit;
  const int disp = 1 << Template::does_dispatch_bit;
  const int clvm = 1 << Template::calls_vm_bit;
  const int iswd = 1 << Template::wide_bit;
  // determine which table to use
  bool is_wide = (flags & iswd) != 0;
  // make sure that wide instructions have a vtos entry point
  // (since they are executed extremely rarely, it doesn't pay out to have an
  // extra set of 5 dispatch tables for the wide instructions - for simplicity
  // they all go with one table)
  assert(in == vtos || !is_wide, "wide instructions have vtos entry point only");
  Template* t = is_wide ? template_for_wide(code, jportal) : template_for(code, jportal);
  // setup entry
  t->initialize(flags, in, out, gen, arg);
  assert(t->bytecode() == code, "just checkin'");
}


void TemplateTable::def(Bytecodes::Code code, int flags, TosState in, TosState out, void (*gen)(Operation op), Operation op, bool jportal) {
  def(code, flags, in, out, (Template::generator)gen, (int)op, jportal);
}


void TemplateTable::def(Bytecodes::Code code, int flags, TosState in, TosState out, void (*gen)(bool arg    ), bool arg, bool jportal) {
  def(code, flags, in, out, (Template::generator)gen, (int)arg, jportal);
}


void TemplateTable::def(Bytecodes::Code code, int flags, TosState in, TosState out, void (*gen)(TosState tos), TosState tos, bool jportal) {
  def(code, flags, in, out, (Template::generator)gen, (int)tos, jportal);
}


void TemplateTable::def(Bytecodes::Code code, int flags, TosState in, TosState out, void (*gen)(Condition cc), Condition cc, bool jportal) {
  def(code, flags, in, out, (Template::generator)gen, (int)cc, jportal);
}

#if defined(TEMPLATE_TABLE_BUG)
//
// It appears that gcc (version 2.91) generates bad code for the template
// table init if this macro is not defined.  My symptom was an assertion
// assert(Universe::heap()->is_in(obj), "sanity check") in handles.cpp line 24.
// when called from interpreterRuntime.resolve_invoke().
//
  #define iload  TemplateTable::iload
  #define lload  TemplateTable::lload
  #define fload  TemplateTable::fload
  #define dload  TemplateTable::dload
  #define aload  TemplateTable::aload
  #define istore TemplateTable::istore
  #define lstore TemplateTable::lstore
  #define fstore TemplateTable::fstore
  #define dstore TemplateTable::dstore
  #define astore TemplateTable::astore
#endif // TEMPLATE_TABLE_BUG

void TemplateTable::initialize() {
  if (_is_initialized) return;

  // Initialize table
  TraceTime timer("TemplateTable initialization", TRACETIME_LOG(Info, startuptime));

  _bs = BarrierSet::barrier_set();

  // For better readability
  const char _    = ' ';
  const int  ____ = 0;
  const int  ubcp = 1 << Template::uses_bcp_bit;
  const int  disp = 1 << Template::does_dispatch_bit;
  const int  clvm = 1 << Template::calls_vm_bit;
  const int  iswd = 1 << Template::wide_bit;
  //                                    interpr. templates
  // Java spec bytecodes                ubcp|disp|clvm|iswd  in    out   generator             argument
  def(Bytecodes::_nop                 , ____|____|____|____, vtos, vtos, nop                 ,  _           );
  def(Bytecodes::_aconst_null         , ____|____|____|____, vtos, atos, aconst_null         ,  _           );
  def(Bytecodes::_iconst_m1           , ____|____|____|____, vtos, itos, iconst              , -1           );
  def(Bytecodes::_iconst_0            , ____|____|____|____, vtos, itos, iconst              ,  0           );
  def(Bytecodes::_iconst_1            , ____|____|____|____, vtos, itos, iconst              ,  1           );
  def(Bytecodes::_iconst_2            , ____|____|____|____, vtos, itos, iconst              ,  2           );
  def(Bytecodes::_iconst_3            , ____|____|____|____, vtos, itos, iconst              ,  3           );
  def(Bytecodes::_iconst_4            , ____|____|____|____, vtos, itos, iconst              ,  4           );
  def(Bytecodes::_iconst_5            , ____|____|____|____, vtos, itos, iconst              ,  5           );
  def(Bytecodes::_lconst_0            , ____|____|____|____, vtos, ltos, lconst              ,  0           );
  def(Bytecodes::_lconst_1            , ____|____|____|____, vtos, ltos, lconst              ,  1           );
  def(Bytecodes::_fconst_0            , ____|____|____|____, vtos, ftos, fconst              ,  0           );
  def(Bytecodes::_fconst_1            , ____|____|____|____, vtos, ftos, fconst              ,  1           );
  def(Bytecodes::_fconst_2            , ____|____|____|____, vtos, ftos, fconst              ,  2           );
  def(Bytecodes::_dconst_0            , ____|____|____|____, vtos, dtos, dconst              ,  0           );
  def(Bytecodes::_dconst_1            , ____|____|____|____, vtos, dtos, dconst              ,  1           );
  def(Bytecodes::_bipush              , ubcp|____|____|____, vtos, itos, bipush              ,  _           );
  def(Bytecodes::_sipush              , ubcp|____|____|____, vtos, itos, sipush              ,  _           );
  def(Bytecodes::_ldc                 , ubcp|____|clvm|____, vtos, vtos, ldc                 ,  false       );
  def(Bytecodes::_ldc_w               , ubcp|____|clvm|____, vtos, vtos, ldc                 ,  true        );
  def(Bytecodes::_ldc2_w              , ubcp|____|clvm|____, vtos, vtos, ldc2_w              ,  _           );
  def(Bytecodes::_iload               , ubcp|____|clvm|____, vtos, itos, iload               ,  _           );
  def(Bytecodes::_lload               , ubcp|____|____|____, vtos, ltos, lload               ,  _           );
  def(Bytecodes::_fload               , ubcp|____|____|____, vtos, ftos, fload               ,  _           );
  def(Bytecodes::_dload               , ubcp|____|____|____, vtos, dtos, dload               ,  _           );
  def(Bytecodes::_aload               , ubcp|____|clvm|____, vtos, atos, aload               ,  _           );
  def(Bytecodes::_iload_0             , ____|____|____|____, vtos, itos, iload               ,  0           );
  def(Bytecodes::_iload_1             , ____|____|____|____, vtos, itos, iload               ,  1           );
  def(Bytecodes::_iload_2             , ____|____|____|____, vtos, itos, iload               ,  2           );
  def(Bytecodes::_iload_3             , ____|____|____|____, vtos, itos, iload               ,  3           );
  def(Bytecodes::_lload_0             , ____|____|____|____, vtos, ltos, lload               ,  0           );
  def(Bytecodes::_lload_1             , ____|____|____|____, vtos, ltos, lload               ,  1           );
  def(Bytecodes::_lload_2             , ____|____|____|____, vtos, ltos, lload               ,  2           );
  def(Bytecodes::_lload_3             , ____|____|____|____, vtos, ltos, lload               ,  3           );
  def(Bytecodes::_fload_0             , ____|____|____|____, vtos, ftos, fload               ,  0           );
  def(Bytecodes::_fload_1             , ____|____|____|____, vtos, ftos, fload               ,  1           );
  def(Bytecodes::_fload_2             , ____|____|____|____, vtos, ftos, fload               ,  2           );
  def(Bytecodes::_fload_3             , ____|____|____|____, vtos, ftos, fload               ,  3           );
  def(Bytecodes::_dload_0             , ____|____|____|____, vtos, dtos, dload               ,  0           );
  def(Bytecodes::_dload_1             , ____|____|____|____, vtos, dtos, dload               ,  1           );
  def(Bytecodes::_dload_2             , ____|____|____|____, vtos, dtos, dload               ,  2           );
  def(Bytecodes::_dload_3             , ____|____|____|____, vtos, dtos, dload               ,  3           );
  def(Bytecodes::_aload_0             , ubcp|____|clvm|____, vtos, atos, aload_0             ,  _           );
  def(Bytecodes::_aload_1             , ____|____|____|____, vtos, atos, aload               ,  1           );
  def(Bytecodes::_aload_2             , ____|____|____|____, vtos, atos, aload               ,  2           );
  def(Bytecodes::_aload_3             , ____|____|____|____, vtos, atos, aload               ,  3           );
  def(Bytecodes::_iaload              , ____|____|____|____, itos, itos, iaload              ,  _           );
  def(Bytecodes::_laload              , ____|____|____|____, itos, ltos, laload              ,  _           );
  def(Bytecodes::_faload              , ____|____|____|____, itos, ftos, faload              ,  _           );
  def(Bytecodes::_daload              , ____|____|____|____, itos, dtos, daload              ,  _           );
  def(Bytecodes::_aaload              , ____|____|____|____, itos, atos, aaload              ,  _           );
  def(Bytecodes::_baload              , ____|____|____|____, itos, itos, baload              ,  _           );
  def(Bytecodes::_caload              , ____|____|____|____, itos, itos, caload              ,  _           );
  def(Bytecodes::_saload              , ____|____|____|____, itos, itos, saload              ,  _           );
  def(Bytecodes::_istore              , ubcp|____|clvm|____, itos, vtos, istore              ,  _           );
  def(Bytecodes::_lstore              , ubcp|____|____|____, ltos, vtos, lstore              ,  _           );
  def(Bytecodes::_fstore              , ubcp|____|____|____, ftos, vtos, fstore              ,  _           );
  def(Bytecodes::_dstore              , ubcp|____|____|____, dtos, vtos, dstore              ,  _           );
  def(Bytecodes::_astore              , ubcp|____|clvm|____, vtos, vtos, astore              ,  _           );
  def(Bytecodes::_istore_0            , ____|____|____|____, itos, vtos, istore              ,  0           );
  def(Bytecodes::_istore_1            , ____|____|____|____, itos, vtos, istore              ,  1           );
  def(Bytecodes::_istore_2            , ____|____|____|____, itos, vtos, istore              ,  2           );
  def(Bytecodes::_istore_3            , ____|____|____|____, itos, vtos, istore              ,  3           );
  def(Bytecodes::_lstore_0            , ____|____|____|____, ltos, vtos, lstore              ,  0           );
  def(Bytecodes::_lstore_1            , ____|____|____|____, ltos, vtos, lstore              ,  1           );
  def(Bytecodes::_lstore_2            , ____|____|____|____, ltos, vtos, lstore              ,  2           );
  def(Bytecodes::_lstore_3            , ____|____|____|____, ltos, vtos, lstore              ,  3           );
  def(Bytecodes::_fstore_0            , ____|____|____|____, ftos, vtos, fstore              ,  0           );
  def(Bytecodes::_fstore_1            , ____|____|____|____, ftos, vtos, fstore              ,  1           );
  def(Bytecodes::_fstore_2            , ____|____|____|____, ftos, vtos, fstore              ,  2           );
  def(Bytecodes::_fstore_3            , ____|____|____|____, ftos, vtos, fstore              ,  3           );
  def(Bytecodes::_dstore_0            , ____|____|____|____, dtos, vtos, dstore              ,  0           );
  def(Bytecodes::_dstore_1            , ____|____|____|____, dtos, vtos, dstore              ,  1           );
  def(Bytecodes::_dstore_2            , ____|____|____|____, dtos, vtos, dstore              ,  2           );
  def(Bytecodes::_dstore_3            , ____|____|____|____, dtos, vtos, dstore              ,  3           );
  def(Bytecodes::_astore_0            , ____|____|____|____, vtos, vtos, astore              ,  0           );
  def(Bytecodes::_astore_1            , ____|____|____|____, vtos, vtos, astore              ,  1           );
  def(Bytecodes::_astore_2            , ____|____|____|____, vtos, vtos, astore              ,  2           );
  def(Bytecodes::_astore_3            , ____|____|____|____, vtos, vtos, astore              ,  3           );
  def(Bytecodes::_iastore             , ____|____|____|____, itos, vtos, iastore             ,  _           );
  def(Bytecodes::_lastore             , ____|____|____|____, ltos, vtos, lastore             ,  _           );
  def(Bytecodes::_fastore             , ____|____|____|____, ftos, vtos, fastore             ,  _           );
  def(Bytecodes::_dastore             , ____|____|____|____, dtos, vtos, dastore             ,  _           );
  def(Bytecodes::_aastore             , ____|____|clvm|____, vtos, vtos, aastore             ,  _           );
  def(Bytecodes::_bastore             , ____|____|____|____, itos, vtos, bastore             ,  _           );
  def(Bytecodes::_castore             , ____|____|____|____, itos, vtos, castore             ,  _           );
  def(Bytecodes::_sastore             , ____|____|____|____, itos, vtos, sastore             ,  _           );
  def(Bytecodes::_pop                 , ____|____|____|____, vtos, vtos, pop                 ,  _           );
  def(Bytecodes::_pop2                , ____|____|____|____, vtos, vtos, pop2                ,  _           );
  def(Bytecodes::_dup                 , ____|____|____|____, vtos, vtos, dup                 ,  _           );
  def(Bytecodes::_dup_x1              , ____|____|____|____, vtos, vtos, dup_x1              ,  _           );
  def(Bytecodes::_dup_x2              , ____|____|____|____, vtos, vtos, dup_x2              ,  _           );
  def(Bytecodes::_dup2                , ____|____|____|____, vtos, vtos, dup2                ,  _           );
  def(Bytecodes::_dup2_x1             , ____|____|____|____, vtos, vtos, dup2_x1             ,  _           );
  def(Bytecodes::_dup2_x2             , ____|____|____|____, vtos, vtos, dup2_x2             ,  _           );
  def(Bytecodes::_swap                , ____|____|____|____, vtos, vtos, swap                ,  _           );
  def(Bytecodes::_iadd                , ____|____|____|____, itos, itos, iop2                , add          );
  def(Bytecodes::_ladd                , ____|____|____|____, ltos, ltos, lop2                , add          );
  def(Bytecodes::_fadd                , ____|____|____|____, ftos, ftos, fop2                , add          );
  def(Bytecodes::_dadd                , ____|____|____|____, dtos, dtos, dop2                , add          );
  def(Bytecodes::_isub                , ____|____|____|____, itos, itos, iop2                , sub          );
  def(Bytecodes::_lsub                , ____|____|____|____, ltos, ltos, lop2                , sub          );
  def(Bytecodes::_fsub                , ____|____|____|____, ftos, ftos, fop2                , sub          );
  def(Bytecodes::_dsub                , ____|____|____|____, dtos, dtos, dop2                , sub          );
  def(Bytecodes::_imul                , ____|____|____|____, itos, itos, iop2                , mul          );
  def(Bytecodes::_lmul                , ____|____|____|____, ltos, ltos, lmul                ,  _           );
  def(Bytecodes::_fmul                , ____|____|____|____, ftos, ftos, fop2                , mul          );
  def(Bytecodes::_dmul                , ____|____|____|____, dtos, dtos, dop2                , mul          );
  def(Bytecodes::_idiv                , ____|____|____|____, itos, itos, idiv                ,  _           );
  def(Bytecodes::_ldiv                , ____|____|____|____, ltos, ltos, ldiv                ,  _           );
  def(Bytecodes::_fdiv                , ____|____|____|____, ftos, ftos, fop2                , div          );
  def(Bytecodes::_ddiv                , ____|____|____|____, dtos, dtos, dop2                , div          );
  def(Bytecodes::_irem                , ____|____|____|____, itos, itos, irem                ,  _           );
  def(Bytecodes::_lrem                , ____|____|____|____, ltos, ltos, lrem                ,  _           );
  def(Bytecodes::_frem                , ____|____|____|____, ftos, ftos, fop2                , rem          );
  def(Bytecodes::_drem                , ____|____|____|____, dtos, dtos, dop2                , rem          );
  def(Bytecodes::_ineg                , ____|____|____|____, itos, itos, ineg                ,  _           );
  def(Bytecodes::_lneg                , ____|____|____|____, ltos, ltos, lneg                ,  _           );
  def(Bytecodes::_fneg                , ____|____|____|____, ftos, ftos, fneg                ,  _           );
  def(Bytecodes::_dneg                , ____|____|____|____, dtos, dtos, dneg                ,  _           );
  def(Bytecodes::_ishl                , ____|____|____|____, itos, itos, iop2                , shl          );
  def(Bytecodes::_lshl                , ____|____|____|____, itos, ltos, lshl                ,  _           );
  def(Bytecodes::_ishr                , ____|____|____|____, itos, itos, iop2                , shr          );
  def(Bytecodes::_lshr                , ____|____|____|____, itos, ltos, lshr                ,  _           );
  def(Bytecodes::_iushr               , ____|____|____|____, itos, itos, iop2                , ushr         );
  def(Bytecodes::_lushr               , ____|____|____|____, itos, ltos, lushr               ,  _           );
  def(Bytecodes::_iand                , ____|____|____|____, itos, itos, iop2                , _and         );
  def(Bytecodes::_land                , ____|____|____|____, ltos, ltos, lop2                , _and         );
  def(Bytecodes::_ior                 , ____|____|____|____, itos, itos, iop2                , _or          );
  def(Bytecodes::_lor                 , ____|____|____|____, ltos, ltos, lop2                , _or          );
  def(Bytecodes::_ixor                , ____|____|____|____, itos, itos, iop2                , _xor         );
  def(Bytecodes::_lxor                , ____|____|____|____, ltos, ltos, lop2                , _xor         );
  def(Bytecodes::_iinc                , ubcp|____|clvm|____, vtos, vtos, iinc                ,  _           );
  def(Bytecodes::_i2l                 , ____|____|____|____, itos, ltos, convert             ,  _           );
  def(Bytecodes::_i2f                 , ____|____|____|____, itos, ftos, convert             ,  _           );
  def(Bytecodes::_i2d                 , ____|____|____|____, itos, dtos, convert             ,  _           );
  def(Bytecodes::_l2i                 , ____|____|____|____, ltos, itos, convert             ,  _           );
  def(Bytecodes::_l2f                 , ____|____|____|____, ltos, ftos, convert             ,  _           );
  def(Bytecodes::_l2d                 , ____|____|____|____, ltos, dtos, convert             ,  _           );
  def(Bytecodes::_f2i                 , ____|____|____|____, ftos, itos, convert             ,  _           );
  def(Bytecodes::_f2l                 , ____|____|____|____, ftos, ltos, convert             ,  _           );
  def(Bytecodes::_f2d                 , ____|____|____|____, ftos, dtos, convert             ,  _           );
  def(Bytecodes::_d2i                 , ____|____|____|____, dtos, itos, convert             ,  _           );
  def(Bytecodes::_d2l                 , ____|____|____|____, dtos, ltos, convert             ,  _           );
  def(Bytecodes::_d2f                 , ____|____|____|____, dtos, ftos, convert             ,  _           );
  def(Bytecodes::_i2b                 , ____|____|____|____, itos, itos, convert             ,  _           );
  def(Bytecodes::_i2c                 , ____|____|____|____, itos, itos, convert             ,  _           );
  def(Bytecodes::_i2s                 , ____|____|____|____, itos, itos, convert             ,  _           );
  def(Bytecodes::_lcmp                , ____|____|____|____, ltos, itos, lcmp                ,  _           );
  def(Bytecodes::_fcmpl               , ____|____|____|____, ftos, itos, float_cmp           , -1           );
  def(Bytecodes::_fcmpg               , ____|____|____|____, ftos, itos, float_cmp           ,  1           );
  def(Bytecodes::_dcmpl               , ____|____|____|____, dtos, itos, double_cmp          , -1           );
  def(Bytecodes::_dcmpg               , ____|____|____|____, dtos, itos, double_cmp          ,  1           );
  def(Bytecodes::_ifeq                , ubcp|____|clvm|____, itos, vtos, normal_if_0cmp      , equal        );
  def(Bytecodes::_ifne                , ubcp|____|clvm|____, itos, vtos, normal_if_0cmp      , not_equal    );
  def(Bytecodes::_iflt                , ubcp|____|clvm|____, itos, vtos, normal_if_0cmp      , less         );
  def(Bytecodes::_ifge                , ubcp|____|clvm|____, itos, vtos, normal_if_0cmp      , greater_equal);
  def(Bytecodes::_ifgt                , ubcp|____|clvm|____, itos, vtos, normal_if_0cmp      , greater      );
  def(Bytecodes::_ifle                , ubcp|____|clvm|____, itos, vtos, normal_if_0cmp      , less_equal   );
  def(Bytecodes::_if_icmpeq           , ubcp|____|clvm|____, itos, vtos, normal_if_icmp      , equal        );
  def(Bytecodes::_if_icmpne           , ubcp|____|clvm|____, itos, vtos, normal_if_icmp      , not_equal    );
  def(Bytecodes::_if_icmplt           , ubcp|____|clvm|____, itos, vtos, normal_if_icmp      , less         );
  def(Bytecodes::_if_icmpge           , ubcp|____|clvm|____, itos, vtos, normal_if_icmp      , greater_equal);
  def(Bytecodes::_if_icmpgt           , ubcp|____|clvm|____, itos, vtos, normal_if_icmp      , greater      );
  def(Bytecodes::_if_icmple           , ubcp|____|clvm|____, itos, vtos, normal_if_icmp      , less_equal   );
  def(Bytecodes::_if_acmpeq           , ubcp|____|clvm|____, atos, vtos, normal_if_acmp      , equal        );
  def(Bytecodes::_if_acmpne           , ubcp|____|clvm|____, atos, vtos, normal_if_acmp      , not_equal    );
  def(Bytecodes::_goto                , ubcp|disp|clvm|____, vtos, vtos, normal_goto         ,  _           );
  def(Bytecodes::_jsr                 , ubcp|disp|____|____, vtos, vtos, normal_jsr          ,  _           ); // result is not an oop, so do not transition to atos
  def(Bytecodes::_ret                 , ubcp|disp|____|____, vtos, vtos, normal_ret          ,  _           );
  def(Bytecodes::_tableswitch         , ubcp|disp|____|____, itos, vtos, normal_tableswitch  ,  _           );
  def(Bytecodes::_lookupswitch        , ubcp|disp|____|____, itos, itos, normal_lookupswitch ,  _           );
  def(Bytecodes::_ireturn             , ____|disp|clvm|____, itos, itos, normal_return       , itos         );
  def(Bytecodes::_lreturn             , ____|disp|clvm|____, ltos, ltos, normal_return       , ltos         );
  def(Bytecodes::_freturn             , ____|disp|clvm|____, ftos, ftos, normal_return       , ftos         );
  def(Bytecodes::_dreturn             , ____|disp|clvm|____, dtos, dtos, normal_return       , dtos         );
  def(Bytecodes::_areturn             , ____|disp|clvm|____, atos, atos, normal_return       , atos         );
  def(Bytecodes::_return              , ____|disp|clvm|____, vtos, vtos, normal_return       , vtos         );
  def(Bytecodes::_getstatic           , ubcp|____|clvm|____, vtos, vtos, getstatic           , f1_byte      );
  def(Bytecodes::_putstatic           , ubcp|____|clvm|____, vtos, vtos, putstatic           , f2_byte      );
  def(Bytecodes::_getfield            , ubcp|____|clvm|____, vtos, vtos, getfield            , f1_byte      );
  def(Bytecodes::_putfield            , ubcp|____|clvm|____, vtos, vtos, putfield            , f2_byte      );
  def(Bytecodes::_invokevirtual       , ubcp|disp|clvm|____, vtos, vtos, normal_invokevirtual, f2_byte      );
  def(Bytecodes::_invokespecial       , ubcp|disp|clvm|____, vtos, vtos, normal_invokespecial, f1_byte      );
  def(Bytecodes::_invokestatic        , ubcp|disp|clvm|____, vtos, vtos, normal_invokestatic , f1_byte      );
  def(Bytecodes::_invokeinterface     , ubcp|disp|clvm|____, vtos, vtos, normal_invokeinterface, f1_byte      );
  def(Bytecodes::_invokedynamic       , ubcp|disp|clvm|____, vtos, vtos, normal_invokedynamic, f1_byte      );
  def(Bytecodes::_new                 , ubcp|____|clvm|____, vtos, atos, _new                ,  _           );
  def(Bytecodes::_newarray            , ubcp|____|clvm|____, itos, atos, newarray            ,  _           );
  def(Bytecodes::_anewarray           , ubcp|____|clvm|____, itos, atos, anewarray           ,  _           );
  def(Bytecodes::_arraylength         , ____|____|____|____, atos, itos, arraylength         ,  _           );
  def(Bytecodes::_athrow              , ____|disp|____|____, atos, vtos, normal_athrow       ,  _           );
  def(Bytecodes::_checkcast           , ubcp|____|clvm|____, atos, atos, checkcast           ,  _           );
  def(Bytecodes::_instanceof          , ubcp|____|clvm|____, atos, itos, instanceof          ,  _           );
  def(Bytecodes::_monitorenter        , ____|disp|clvm|____, atos, vtos, normal_monitorenter ,  _           );
  def(Bytecodes::_monitorexit         , ____|____|clvm|____, atos, vtos, normal_monitorexit  ,  _           );
  def(Bytecodes::_wide                , ubcp|disp|____|____, vtos, vtos, normal_wide         ,  _           );
  def(Bytecodes::_multianewarray      , ubcp|____|clvm|____, vtos, atos, multianewarray      ,  _           );
  def(Bytecodes::_ifnull              , ubcp|____|clvm|____, atos, vtos, normal_if_nullcmp   , equal        );
  def(Bytecodes::_ifnonnull           , ubcp|____|clvm|____, atos, vtos, normal_if_nullcmp   , not_equal    );
  def(Bytecodes::_goto_w              , ubcp|____|clvm|____, vtos, vtos, normal_goto_w       ,  _           );
  def(Bytecodes::_jsr_w               , ubcp|____|____|____, vtos, vtos, normal_jsr_w        ,  _           );

  // wide Java spec bytecodes
  def(Bytecodes::_iload               , ubcp|____|____|iswd, vtos, itos, wide_iload          ,  _           );
  def(Bytecodes::_lload               , ubcp|____|____|iswd, vtos, ltos, wide_lload          ,  _           );
  def(Bytecodes::_fload               , ubcp|____|____|iswd, vtos, ftos, wide_fload          ,  _           );
  def(Bytecodes::_dload               , ubcp|____|____|iswd, vtos, dtos, wide_dload          ,  _           );
  def(Bytecodes::_aload               , ubcp|____|____|iswd, vtos, atos, wide_aload          ,  _           );
  def(Bytecodes::_istore              , ubcp|____|____|iswd, vtos, vtos, wide_istore         ,  _           );
  def(Bytecodes::_lstore              , ubcp|____|____|iswd, vtos, vtos, wide_lstore         ,  _           );
  def(Bytecodes::_fstore              , ubcp|____|____|iswd, vtos, vtos, wide_fstore         ,  _           );
  def(Bytecodes::_dstore              , ubcp|____|____|iswd, vtos, vtos, wide_dstore         ,  _           );
  def(Bytecodes::_astore              , ubcp|____|____|iswd, vtos, vtos, wide_astore         ,  _           );
  def(Bytecodes::_iinc                , ubcp|____|____|iswd, vtos, vtos, wide_iinc           ,  _           );
  def(Bytecodes::_ret                 , ubcp|disp|____|iswd, vtos, vtos, normal_wide_ret     ,  _           );
  def(Bytecodes::_breakpoint          , ubcp|disp|clvm|____, vtos, vtos, normal_breakpoint   ,  _           );

  // JVM bytecodes
  def(Bytecodes::_fast_agetfield      , ubcp|____|____|____, atos, atos, fast_accessfield    ,  atos        );
  def(Bytecodes::_fast_bgetfield      , ubcp|____|____|____, atos, itos, fast_accessfield    ,  itos        );
  def(Bytecodes::_fast_cgetfield      , ubcp|____|____|____, atos, itos, fast_accessfield    ,  itos        );
  def(Bytecodes::_fast_dgetfield      , ubcp|____|____|____, atos, dtos, fast_accessfield    ,  dtos        );
  def(Bytecodes::_fast_fgetfield      , ubcp|____|____|____, atos, ftos, fast_accessfield    ,  ftos        );
  def(Bytecodes::_fast_igetfield      , ubcp|____|____|____, atos, itos, fast_accessfield    ,  itos        );
  def(Bytecodes::_fast_lgetfield      , ubcp|____|____|____, atos, ltos, fast_accessfield    ,  ltos        );
  def(Bytecodes::_fast_sgetfield      , ubcp|____|____|____, atos, itos, fast_accessfield    ,  itos        );

  def(Bytecodes::_fast_aputfield      , ubcp|____|____|____, atos, vtos, fast_storefield ,   atos        );
  def(Bytecodes::_fast_bputfield      , ubcp|____|____|____, itos, vtos, fast_storefield ,   itos        );
  def(Bytecodes::_fast_zputfield      , ubcp|____|____|____, itos, vtos, fast_storefield ,   itos        );
  def(Bytecodes::_fast_cputfield      , ubcp|____|____|____, itos, vtos, fast_storefield  ,  itos        );
  def(Bytecodes::_fast_dputfield      , ubcp|____|____|____, dtos, vtos, fast_storefield  ,  dtos        );
  def(Bytecodes::_fast_fputfield      , ubcp|____|____|____, ftos, vtos, fast_storefield  ,  ftos        );
  def(Bytecodes::_fast_iputfield      , ubcp|____|____|____, itos, vtos, fast_storefield  ,  itos        );
  def(Bytecodes::_fast_lputfield      , ubcp|____|____|____, ltos, vtos, fast_storefield  ,  ltos        );
  def(Bytecodes::_fast_sputfield      , ubcp|____|____|____, itos, vtos, fast_storefield  ,  itos        );

  def(Bytecodes::_fast_aload_0        , ____|____|____|____, vtos, atos, aload               ,  0           );
  def(Bytecodes::_fast_iaccess_0      , ubcp|____|____|____, vtos, itos, fast_xaccess        ,  itos        );
  def(Bytecodes::_fast_aaccess_0      , ubcp|____|____|____, vtos, atos, fast_xaccess        ,  atos        );
  def(Bytecodes::_fast_faccess_0      , ubcp|____|____|____, vtos, ftos, fast_xaccess        ,  ftos        );

  def(Bytecodes::_fast_iload          , ubcp|____|____|____, vtos, itos, fast_iload          ,  _       );
  def(Bytecodes::_fast_iload2         , ubcp|____|____|____, vtos, itos, fast_iload2         ,  _       );
  def(Bytecodes::_fast_icaload        , ubcp|____|____|____, vtos, itos, fast_icaload        ,  _       );

  def(Bytecodes::_fast_invokevfinal   , ubcp|disp|clvm|____, vtos, vtos, normal_fast_invokevfinal, f2_byte      );

  def(Bytecodes::_fast_linearswitch   , ubcp|disp|____|____, itos, vtos, normal_fast_linearswitch,  _           );
  def(Bytecodes::_fast_binaryswitch   , ubcp|disp|____|____, itos, vtos, normal_fast_binaryswitch,  _           );

  def(Bytecodes::_fast_aldc           , ubcp|____|clvm|____, vtos, atos, fast_aldc           ,  false       );
  def(Bytecodes::_fast_aldc_w         , ubcp|____|clvm|____, vtos, atos, fast_aldc           ,  true        );

  def(Bytecodes::_return_register_finalizer , ____|disp|clvm|____, vtos, vtos, normal_return ,  vtos        );

  def(Bytecodes::_invokehandle        , ubcp|disp|clvm|____, vtos, vtos, normal_invokehandle , f1_byte      );

  def(Bytecodes::_nofast_getfield     , ubcp|____|clvm|____, vtos, vtos, nofast_getfield     , f1_byte      );
  def(Bytecodes::_nofast_putfield     , ubcp|____|clvm|____, vtos, vtos, nofast_putfield     , f2_byte      );

  def(Bytecodes::_nofast_aload_0      , ____|____|clvm|____, vtos, atos, nofast_aload_0      ,  _           );
  def(Bytecodes::_nofast_iload        , ubcp|____|clvm|____, vtos, itos, nofast_iload        ,  _           );

  def(Bytecodes::_shouldnotreachhere   , ____|____|____|____, vtos, vtos, shouldnotreachhere ,  _           );

#ifdef JPORTAL_ENABLE
  if (JPortal) {
    //                                    interpr. templates
    // Java spec bytecodes                ubcp|disp|clvm|iswd  in    out   generator             argument       jportal
    def(Bytecodes::_nop                 , ____|____|____|____, vtos, vtos, nop                 ,  _           , true);
    def(Bytecodes::_aconst_null         , ____|____|____|____, vtos, atos, aconst_null         ,  _           , true);
    def(Bytecodes::_iconst_m1           , ____|____|____|____, vtos, itos, iconst              , -1           , true);
    def(Bytecodes::_iconst_0            , ____|____|____|____, vtos, itos, iconst              ,  0           , true);
    def(Bytecodes::_iconst_1            , ____|____|____|____, vtos, itos, iconst              ,  1           , true);
    def(Bytecodes::_iconst_2            , ____|____|____|____, vtos, itos, iconst              ,  2           , true);
    def(Bytecodes::_iconst_3            , ____|____|____|____, vtos, itos, iconst              ,  3           , true);
    def(Bytecodes::_iconst_4            , ____|____|____|____, vtos, itos, iconst              ,  4           , true);
    def(Bytecodes::_iconst_5            , ____|____|____|____, vtos, itos, iconst              ,  5           , true);
    def(Bytecodes::_lconst_0            , ____|____|____|____, vtos, ltos, lconst              ,  0           , true);
    def(Bytecodes::_lconst_1            , ____|____|____|____, vtos, ltos, lconst              ,  1           , true);
    def(Bytecodes::_fconst_0            , ____|____|____|____, vtos, ftos, fconst              ,  0           , true);
    def(Bytecodes::_fconst_1            , ____|____|____|____, vtos, ftos, fconst              ,  1           , true);
    def(Bytecodes::_fconst_2            , ____|____|____|____, vtos, ftos, fconst              ,  2           , true);
    def(Bytecodes::_dconst_0            , ____|____|____|____, vtos, dtos, dconst              ,  0           , true);
    def(Bytecodes::_dconst_1            , ____|____|____|____, vtos, dtos, dconst              ,  1           , true);
    def(Bytecodes::_bipush              , ubcp|____|____|____, vtos, itos, bipush              ,  _           , true);
    def(Bytecodes::_sipush              , ubcp|____|____|____, vtos, itos, sipush              ,  _           , true);
    def(Bytecodes::_ldc                 , ubcp|____|clvm|____, vtos, vtos, ldc                 ,  false       , true);
    def(Bytecodes::_ldc_w               , ubcp|____|clvm|____, vtos, vtos, ldc                 ,  true        , true);
    def(Bytecodes::_ldc2_w              , ubcp|____|clvm|____, vtos, vtos, ldc2_w              ,  _           , true);
    def(Bytecodes::_iload               , ubcp|____|clvm|____, vtos, itos, iload               ,  _           , true);
    def(Bytecodes::_lload               , ubcp|____|____|____, vtos, ltos, lload               ,  _           , true);
    def(Bytecodes::_fload               , ubcp|____|____|____, vtos, ftos, fload               ,  _           , true);
    def(Bytecodes::_dload               , ubcp|____|____|____, vtos, dtos, dload               ,  _           , true);
    def(Bytecodes::_aload               , ubcp|____|clvm|____, vtos, atos, aload               ,  _           , true);
    def(Bytecodes::_iload_0             , ____|____|____|____, vtos, itos, iload               ,  0           , true);
    def(Bytecodes::_iload_1             , ____|____|____|____, vtos, itos, iload               ,  1           , true);
    def(Bytecodes::_iload_2             , ____|____|____|____, vtos, itos, iload               ,  2           , true);
    def(Bytecodes::_iload_3             , ____|____|____|____, vtos, itos, iload               ,  3           , true);
    def(Bytecodes::_lload_0             , ____|____|____|____, vtos, ltos, lload               ,  0           , true);
    def(Bytecodes::_lload_1             , ____|____|____|____, vtos, ltos, lload               ,  1           , true);
    def(Bytecodes::_lload_2             , ____|____|____|____, vtos, ltos, lload               ,  2           , true);
    def(Bytecodes::_lload_3             , ____|____|____|____, vtos, ltos, lload               ,  3           , true);
    def(Bytecodes::_fload_0             , ____|____|____|____, vtos, ftos, fload               ,  0           , true);
    def(Bytecodes::_fload_1             , ____|____|____|____, vtos, ftos, fload               ,  1           , true);
    def(Bytecodes::_fload_2             , ____|____|____|____, vtos, ftos, fload               ,  2           , true);
    def(Bytecodes::_fload_3             , ____|____|____|____, vtos, ftos, fload               ,  3           , true);
    def(Bytecodes::_dload_0             , ____|____|____|____, vtos, dtos, dload               ,  0           , true);
    def(Bytecodes::_dload_1             , ____|____|____|____, vtos, dtos, dload               ,  1           , true);
    def(Bytecodes::_dload_2             , ____|____|____|____, vtos, dtos, dload               ,  2           , true);
    def(Bytecodes::_dload_3             , ____|____|____|____, vtos, dtos, dload               ,  3           , true);
    def(Bytecodes::_aload_0             , ubcp|____|clvm|____, vtos, atos, aload_0             ,  _           , true);
    def(Bytecodes::_aload_1             , ____|____|____|____, vtos, atos, aload               ,  1           , true);
    def(Bytecodes::_aload_2             , ____|____|____|____, vtos, atos, aload               ,  2           , true);
    def(Bytecodes::_aload_3             , ____|____|____|____, vtos, atos, aload               ,  3           , true);
    def(Bytecodes::_iaload              , ____|____|____|____, itos, itos, iaload              ,  _           , true);
    def(Bytecodes::_laload              , ____|____|____|____, itos, ltos, laload              ,  _           , true);
    def(Bytecodes::_faload              , ____|____|____|____, itos, ftos, faload              ,  _           , true);
    def(Bytecodes::_daload              , ____|____|____|____, itos, dtos, daload              ,  _           , true);
    def(Bytecodes::_aaload              , ____|____|____|____, itos, atos, aaload              ,  _           , true);
    def(Bytecodes::_baload              , ____|____|____|____, itos, itos, baload              ,  _           , true);
    def(Bytecodes::_caload              , ____|____|____|____, itos, itos, caload              ,  _           , true);
    def(Bytecodes::_saload              , ____|____|____|____, itos, itos, saload              ,  _           , true);
    def(Bytecodes::_istore              , ubcp|____|clvm|____, itos, vtos, istore              ,  _           , true);
    def(Bytecodes::_lstore              , ubcp|____|____|____, ltos, vtos, lstore              ,  _           , true);
    def(Bytecodes::_fstore              , ubcp|____|____|____, ftos, vtos, fstore              ,  _           , true);
    def(Bytecodes::_dstore              , ubcp|____|____|____, dtos, vtos, dstore              ,  _           , true);
    def(Bytecodes::_astore              , ubcp|____|clvm|____, vtos, vtos, astore              ,  _           , true);
    def(Bytecodes::_istore_0            , ____|____|____|____, itos, vtos, istore              ,  0           , true);
    def(Bytecodes::_istore_1            , ____|____|____|____, itos, vtos, istore              ,  1           , true);
    def(Bytecodes::_istore_2            , ____|____|____|____, itos, vtos, istore              ,  2           , true);
    def(Bytecodes::_istore_3            , ____|____|____|____, itos, vtos, istore              ,  3           , true);
    def(Bytecodes::_lstore_0            , ____|____|____|____, ltos, vtos, lstore              ,  0           , true);
    def(Bytecodes::_lstore_1            , ____|____|____|____, ltos, vtos, lstore              ,  1           , true);
    def(Bytecodes::_lstore_2            , ____|____|____|____, ltos, vtos, lstore              ,  2           , true);
    def(Bytecodes::_lstore_3            , ____|____|____|____, ltos, vtos, lstore              ,  3           , true);
    def(Bytecodes::_fstore_0            , ____|____|____|____, ftos, vtos, fstore              ,  0           , true);
    def(Bytecodes::_fstore_1            , ____|____|____|____, ftos, vtos, fstore              ,  1           , true);
    def(Bytecodes::_fstore_2            , ____|____|____|____, ftos, vtos, fstore              ,  2           , true);
    def(Bytecodes::_fstore_3            , ____|____|____|____, ftos, vtos, fstore              ,  3           , true);
    def(Bytecodes::_dstore_0            , ____|____|____|____, dtos, vtos, dstore              ,  0           , true);
    def(Bytecodes::_dstore_1            , ____|____|____|____, dtos, vtos, dstore              ,  1           , true);
    def(Bytecodes::_dstore_2            , ____|____|____|____, dtos, vtos, dstore              ,  2           , true);
    def(Bytecodes::_dstore_3            , ____|____|____|____, dtos, vtos, dstore              ,  3           , true);
    def(Bytecodes::_astore_0            , ____|____|____|____, vtos, vtos, astore              ,  0           , true);
    def(Bytecodes::_astore_1            , ____|____|____|____, vtos, vtos, astore              ,  1           , true);
    def(Bytecodes::_astore_2            , ____|____|____|____, vtos, vtos, astore              ,  2           , true);
    def(Bytecodes::_astore_3            , ____|____|____|____, vtos, vtos, astore              ,  3           , true);
    def(Bytecodes::_iastore             , ____|____|____|____, itos, vtos, iastore             ,  _           , true);
    def(Bytecodes::_lastore             , ____|____|____|____, ltos, vtos, lastore             ,  _           , true);
    def(Bytecodes::_fastore             , ____|____|____|____, ftos, vtos, fastore             ,  _           , true);
    def(Bytecodes::_dastore             , ____|____|____|____, dtos, vtos, dastore             ,  _           , true);
    def(Bytecodes::_aastore             , ____|____|clvm|____, vtos, vtos, aastore             ,  _           , true);
    def(Bytecodes::_bastore             , ____|____|____|____, itos, vtos, bastore             ,  _           , true);
    def(Bytecodes::_castore             , ____|____|____|____, itos, vtos, castore             ,  _           , true);
    def(Bytecodes::_sastore             , ____|____|____|____, itos, vtos, sastore             ,  _           , true);
    def(Bytecodes::_pop                 , ____|____|____|____, vtos, vtos, pop                 ,  _           , true);
    def(Bytecodes::_pop2                , ____|____|____|____, vtos, vtos, pop2                ,  _           , true);
    def(Bytecodes::_dup                 , ____|____|____|____, vtos, vtos, dup                 ,  _           , true);
    def(Bytecodes::_dup_x1              , ____|____|____|____, vtos, vtos, dup_x1              ,  _           , true);
    def(Bytecodes::_dup_x2              , ____|____|____|____, vtos, vtos, dup_x2              ,  _           , true);
    def(Bytecodes::_dup2                , ____|____|____|____, vtos, vtos, dup2                ,  _           , true);
    def(Bytecodes::_dup2_x1             , ____|____|____|____, vtos, vtos, dup2_x1             ,  _           , true);
    def(Bytecodes::_dup2_x2             , ____|____|____|____, vtos, vtos, dup2_x2             ,  _           , true);
    def(Bytecodes::_swap                , ____|____|____|____, vtos, vtos, swap                ,  _           , true);
    def(Bytecodes::_iadd                , ____|____|____|____, itos, itos, iop2                , add          , true);
    def(Bytecodes::_ladd                , ____|____|____|____, ltos, ltos, lop2                , add          , true);
    def(Bytecodes::_fadd                , ____|____|____|____, ftos, ftos, fop2                , add          , true);
    def(Bytecodes::_dadd                , ____|____|____|____, dtos, dtos, dop2                , add          , true);
    def(Bytecodes::_isub                , ____|____|____|____, itos, itos, iop2                , sub          , true);
    def(Bytecodes::_lsub                , ____|____|____|____, ltos, ltos, lop2                , sub          , true);
    def(Bytecodes::_fsub                , ____|____|____|____, ftos, ftos, fop2                , sub          , true);
    def(Bytecodes::_dsub                , ____|____|____|____, dtos, dtos, dop2                , sub          , true);
    def(Bytecodes::_imul                , ____|____|____|____, itos, itos, iop2                , mul          , true);
    def(Bytecodes::_lmul                , ____|____|____|____, ltos, ltos, lmul                ,  _           , true);
    def(Bytecodes::_fmul                , ____|____|____|____, ftos, ftos, fop2                , mul          , true);
    def(Bytecodes::_dmul                , ____|____|____|____, dtos, dtos, dop2                , mul          , true);
    def(Bytecodes::_idiv                , ____|____|____|____, itos, itos, idiv                ,  _           , true);
    def(Bytecodes::_ldiv                , ____|____|____|____, ltos, ltos, ldiv                ,  _           , true);
    def(Bytecodes::_fdiv                , ____|____|____|____, ftos, ftos, fop2                , div          , true);
    def(Bytecodes::_ddiv                , ____|____|____|____, dtos, dtos, dop2                , div          , true);
    def(Bytecodes::_irem                , ____|____|____|____, itos, itos, irem                ,  _           , true);
    def(Bytecodes::_lrem                , ____|____|____|____, ltos, ltos, lrem                ,  _           , true);
    def(Bytecodes::_frem                , ____|____|____|____, ftos, ftos, fop2                , rem          , true);
    def(Bytecodes::_drem                , ____|____|____|____, dtos, dtos, dop2                , rem          , true);
    def(Bytecodes::_ineg                , ____|____|____|____, itos, itos, ineg                ,  _           , true);
    def(Bytecodes::_lneg                , ____|____|____|____, ltos, ltos, lneg                ,  _           , true);
    def(Bytecodes::_fneg                , ____|____|____|____, ftos, ftos, fneg                ,  _           , true);
    def(Bytecodes::_dneg                , ____|____|____|____, dtos, dtos, dneg                ,  _           , true);
    def(Bytecodes::_ishl                , ____|____|____|____, itos, itos, iop2                , shl          , true);
    def(Bytecodes::_lshl                , ____|____|____|____, itos, ltos, lshl                ,  _           , true);
    def(Bytecodes::_ishr                , ____|____|____|____, itos, itos, iop2                , shr          , true);
    def(Bytecodes::_lshr                , ____|____|____|____, itos, ltos, lshr                ,  _           , true);
    def(Bytecodes::_iushr               , ____|____|____|____, itos, itos, iop2                , ushr         , true);
    def(Bytecodes::_lushr               , ____|____|____|____, itos, ltos, lushr               ,  _           , true);
    def(Bytecodes::_iand                , ____|____|____|____, itos, itos, iop2                , _and         , true);
    def(Bytecodes::_land                , ____|____|____|____, ltos, ltos, lop2                , _and         , true);
    def(Bytecodes::_ior                 , ____|____|____|____, itos, itos, iop2                , _or          , true);
    def(Bytecodes::_lor                 , ____|____|____|____, ltos, ltos, lop2                , _or          , true);
    def(Bytecodes::_ixor                , ____|____|____|____, itos, itos, iop2                , _xor         , true);
    def(Bytecodes::_lxor                , ____|____|____|____, ltos, ltos, lop2                , _xor         , true);
    def(Bytecodes::_iinc                , ubcp|____|clvm|____, vtos, vtos, iinc                ,  _           , true);
    def(Bytecodes::_i2l                 , ____|____|____|____, itos, ltos, convert             ,  _           , true);
    def(Bytecodes::_i2f                 , ____|____|____|____, itos, ftos, convert             ,  _           , true);
    def(Bytecodes::_i2d                 , ____|____|____|____, itos, dtos, convert             ,  _           , true);
    def(Bytecodes::_l2i                 , ____|____|____|____, ltos, itos, convert             ,  _           , true);
    def(Bytecodes::_l2f                 , ____|____|____|____, ltos, ftos, convert             ,  _           , true);
    def(Bytecodes::_l2d                 , ____|____|____|____, ltos, dtos, convert             ,  _           , true);
    def(Bytecodes::_f2i                 , ____|____|____|____, ftos, itos, convert             ,  _           , true);
    def(Bytecodes::_f2l                 , ____|____|____|____, ftos, ltos, convert             ,  _           , true);
    def(Bytecodes::_f2d                 , ____|____|____|____, ftos, dtos, convert             ,  _           , true);
    def(Bytecodes::_d2i                 , ____|____|____|____, dtos, itos, convert             ,  _           , true);
    def(Bytecodes::_d2l                 , ____|____|____|____, dtos, ltos, convert             ,  _           , true);
    def(Bytecodes::_d2f                 , ____|____|____|____, dtos, ftos, convert             ,  _           , true);
    def(Bytecodes::_i2b                 , ____|____|____|____, itos, itos, convert             ,  _           , true);
    def(Bytecodes::_i2c                 , ____|____|____|____, itos, itos, convert             ,  _           , true);
    def(Bytecodes::_i2s                 , ____|____|____|____, itos, itos, convert             ,  _           , true);
    def(Bytecodes::_lcmp                , ____|____|____|____, ltos, itos, lcmp                ,  _           , true);
    def(Bytecodes::_fcmpl               , ____|____|____|____, ftos, itos, float_cmp           , -1           , true);
    def(Bytecodes::_fcmpg               , ____|____|____|____, ftos, itos, float_cmp           ,  1           , true);
    def(Bytecodes::_dcmpl               , ____|____|____|____, dtos, itos, double_cmp          , -1           , true);
    def(Bytecodes::_dcmpg               , ____|____|____|____, dtos, itos, double_cmp          ,  1           , true);
    def(Bytecodes::_ifeq                , ubcp|____|clvm|____, itos, vtos, jportal_if_0cmp     , equal        , true);
    def(Bytecodes::_ifne                , ubcp|____|clvm|____, itos, vtos, jportal_if_0cmp     , not_equal    , true);
    def(Bytecodes::_iflt                , ubcp|____|clvm|____, itos, vtos, jportal_if_0cmp     , less         , true);
    def(Bytecodes::_ifge                , ubcp|____|clvm|____, itos, vtos, jportal_if_0cmp     , greater_equal, true);
    def(Bytecodes::_ifgt                , ubcp|____|clvm|____, itos, vtos, jportal_if_0cmp     , greater      , true);
    def(Bytecodes::_ifle                , ubcp|____|clvm|____, itos, vtos, jportal_if_0cmp     , less_equal   , true);
    def(Bytecodes::_if_icmpeq           , ubcp|____|clvm|____, itos, vtos, jportal_if_icmp     , equal        , true);
    def(Bytecodes::_if_icmpne           , ubcp|____|clvm|____, itos, vtos, jportal_if_icmp     , not_equal    , true);
    def(Bytecodes::_if_icmplt           , ubcp|____|clvm|____, itos, vtos, jportal_if_icmp     , less         , true);
    def(Bytecodes::_if_icmpge           , ubcp|____|clvm|____, itos, vtos, jportal_if_icmp     , greater_equal, true);
    def(Bytecodes::_if_icmpgt           , ubcp|____|clvm|____, itos, vtos, jportal_if_icmp     , greater      , true);
    def(Bytecodes::_if_icmple           , ubcp|____|clvm|____, itos, vtos, jportal_if_icmp     , less_equal   , true);
    def(Bytecodes::_if_acmpeq           , ubcp|____|clvm|____, atos, vtos, jportal_if_acmp     , equal        , true);
    def(Bytecodes::_if_acmpne           , ubcp|____|clvm|____, atos, vtos, jportal_if_acmp     , not_equal    , true);
    def(Bytecodes::_goto                , ubcp|disp|clvm|____, vtos, vtos, jportal_goto        ,  _           , true);
    def(Bytecodes::_jsr                 , ubcp|disp|____|____, vtos, vtos, jportal_jsr         ,  _           , true); // result is not an oop, so do not transition to atos
    def(Bytecodes::_ret                 , ubcp|disp|____|____, vtos, vtos, jportal_ret         ,  _           , true);
    def(Bytecodes::_tableswitch         , ubcp|disp|____|____, itos, vtos, jportal_tableswitch ,  _           , true);
    def(Bytecodes::_lookupswitch        , ubcp|disp|____|____, itos, itos, jportal_lookupswitch,  _           , true);
    def(Bytecodes::_ireturn             , ____|disp|clvm|____, itos, itos, jportal_return      , itos         , true);
    def(Bytecodes::_lreturn             , ____|disp|clvm|____, ltos, ltos, jportal_return      , ltos         , true);
    def(Bytecodes::_freturn             , ____|disp|clvm|____, ftos, ftos, jportal_return      , ftos         , true);
    def(Bytecodes::_dreturn             , ____|disp|clvm|____, dtos, dtos, jportal_return      , dtos         , true);
    def(Bytecodes::_areturn             , ____|disp|clvm|____, atos, atos, jportal_return      , atos         , true);
    def(Bytecodes::_return              , ____|disp|clvm|____, vtos, vtos, jportal_return      , vtos         , true);
    def(Bytecodes::_getstatic           , ubcp|____|clvm|____, vtos, vtos, getstatic           , f1_byte      , true);
    def(Bytecodes::_putstatic           , ubcp|____|clvm|____, vtos, vtos, putstatic           , f2_byte      , true);
    def(Bytecodes::_getfield            , ubcp|____|clvm|____, vtos, vtos, getfield            , f1_byte      , true);
    def(Bytecodes::_putfield            , ubcp|____|clvm|____, vtos, vtos, putfield            , f2_byte      , true);
    def(Bytecodes::_invokevirtual       , ubcp|disp|clvm|____, vtos, vtos, jportal_invokevirtual, f2_byte      , true);
    def(Bytecodes::_invokespecial       , ubcp|disp|clvm|____, vtos, vtos, jportal_invokespecial, f1_byte      , true);
    def(Bytecodes::_invokestatic        , ubcp|disp|clvm|____, vtos, vtos, jportal_invokestatic, f1_byte      , true);
    def(Bytecodes::_invokeinterface     , ubcp|disp|clvm|____, vtos, vtos, jportal_invokeinterface, f1_byte      , true);
    def(Bytecodes::_invokedynamic       , ubcp|disp|clvm|____, vtos, vtos, jportal_invokedynamic, f1_byte      , true);
    def(Bytecodes::_new                 , ubcp|____|clvm|____, vtos, atos, _new                ,  _           , true);
    def(Bytecodes::_newarray            , ubcp|____|clvm|____, itos, atos, newarray            ,  _           , true);
    def(Bytecodes::_anewarray           , ubcp|____|clvm|____, itos, atos, anewarray           ,  _           , true);
    def(Bytecodes::_arraylength         , ____|____|____|____, atos, itos, arraylength         ,  _           , true);
    def(Bytecodes::_athrow              , ____|disp|____|____, atos, vtos, jportal_athrow      ,  _           , true);
    def(Bytecodes::_checkcast           , ubcp|____|clvm|____, atos, atos, checkcast           ,  _           , true);
    def(Bytecodes::_instanceof          , ubcp|____|clvm|____, atos, itos, instanceof          ,  _           , true);
    def(Bytecodes::_monitorenter        , ____|disp|clvm|____, atos, vtos, jportal_monitorenter,  _           , true);
    def(Bytecodes::_monitorexit         , ____|____|clvm|____, atos, vtos, jportal_monitorexit ,  _           , true);
    def(Bytecodes::_wide                , ubcp|disp|____|____, vtos, vtos, jportal_wide                ,  _           , true);
    def(Bytecodes::_multianewarray      , ubcp|____|clvm|____, vtos, atos, multianewarray      ,  _           , true);
    def(Bytecodes::_ifnull              , ubcp|____|clvm|____, atos, vtos, jportal_if_nullcmp  , equal        , true);
    def(Bytecodes::_ifnonnull           , ubcp|____|clvm|____, atos, vtos, jportal_if_nullcmp  , not_equal    , true);
    def(Bytecodes::_goto_w              , ubcp|____|clvm|____, vtos, vtos, jportal_goto_w      ,  _           , true);
    def(Bytecodes::_jsr_w               , ubcp|____|____|____, vtos, vtos, jportal_jsr_w       ,  _           , true);

  // wide Java spec bytecodes
    def(Bytecodes::_iload               , ubcp|____|____|iswd, vtos, itos, wide_iload          ,  _           , true);
    def(Bytecodes::_lload               , ubcp|____|____|iswd, vtos, ltos, wide_lload          ,  _           , true);
    def(Bytecodes::_fload               , ubcp|____|____|iswd, vtos, ftos, wide_fload          ,  _           , true);
    def(Bytecodes::_dload               , ubcp|____|____|iswd, vtos, dtos, wide_dload          ,  _           , true);
    def(Bytecodes::_aload               , ubcp|____|____|iswd, vtos, atos, wide_aload          ,  _           , true);
    def(Bytecodes::_istore              , ubcp|____|____|iswd, vtos, vtos, wide_istore         ,  _           , true);
    def(Bytecodes::_lstore              , ubcp|____|____|iswd, vtos, vtos, wide_lstore         ,  _           , true);
    def(Bytecodes::_fstore              , ubcp|____|____|iswd, vtos, vtos, wide_fstore         ,  _           , true);
    def(Bytecodes::_dstore              , ubcp|____|____|iswd, vtos, vtos, wide_dstore         ,  _           , true);
    def(Bytecodes::_astore              , ubcp|____|____|iswd, vtos, vtos, wide_astore         ,  _           , true);
    def(Bytecodes::_iinc                , ubcp|____|____|iswd, vtos, vtos, wide_iinc           ,  _           , true);
    def(Bytecodes::_ret                 , ubcp|disp|____|iswd, vtos, vtos, jportal_wide_ret    ,  _           , true);
    def(Bytecodes::_breakpoint          , ubcp|disp|clvm|____, vtos, vtos, jportal_breakpoint  ,  _           , true);

  // JVM bytecodes
    def(Bytecodes::_fast_agetfield      , ubcp|____|____|____, atos, atos, fast_accessfield    ,  atos        , true);
    def(Bytecodes::_fast_bgetfield      , ubcp|____|____|____, atos, itos, fast_accessfield    ,  itos        , true);
    def(Bytecodes::_fast_cgetfield      , ubcp|____|____|____, atos, itos, fast_accessfield    ,  itos        , true);
    def(Bytecodes::_fast_dgetfield      , ubcp|____|____|____, atos, dtos, fast_accessfield    ,  dtos        , true);
    def(Bytecodes::_fast_fgetfield      , ubcp|____|____|____, atos, ftos, fast_accessfield    ,  ftos        , true);
    def(Bytecodes::_fast_igetfield      , ubcp|____|____|____, atos, itos, fast_accessfield    ,  itos        , true);
    def(Bytecodes::_fast_lgetfield      , ubcp|____|____|____, atos, ltos, fast_accessfield    ,  ltos        , true);
    def(Bytecodes::_fast_sgetfield      , ubcp|____|____|____, atos, itos, fast_accessfield    ,  itos        , true);

    def(Bytecodes::_fast_aputfield      , ubcp|____|____|____, atos, vtos, fast_storefield ,   atos        , true);
    def(Bytecodes::_fast_bputfield      , ubcp|____|____|____, itos, vtos, fast_storefield ,   itos        , true);
    def(Bytecodes::_fast_zputfield      , ubcp|____|____|____, itos, vtos, fast_storefield ,   itos        , true);
    def(Bytecodes::_fast_cputfield      , ubcp|____|____|____, itos, vtos, fast_storefield  ,  itos        , true);
    def(Bytecodes::_fast_dputfield      , ubcp|____|____|____, dtos, vtos, fast_storefield  ,  dtos        , true);
    def(Bytecodes::_fast_fputfield      , ubcp|____|____|____, ftos, vtos, fast_storefield  ,  ftos        , true);
    def(Bytecodes::_fast_iputfield      , ubcp|____|____|____, itos, vtos, fast_storefield  ,  itos        , true);
    def(Bytecodes::_fast_lputfield      , ubcp|____|____|____, ltos, vtos, fast_storefield  ,  ltos        , true);
    def(Bytecodes::_fast_sputfield      , ubcp|____|____|____, itos, vtos, fast_storefield  ,  itos        , true);

    def(Bytecodes::_fast_aload_0        , ____|____|____|____, vtos, atos, aload               ,  0           , true);
    def(Bytecodes::_fast_iaccess_0      , ubcp|____|____|____, vtos, itos, fast_xaccess        ,  itos        , true);
    def(Bytecodes::_fast_aaccess_0      , ubcp|____|____|____, vtos, atos, fast_xaccess        ,  atos        , true);
    def(Bytecodes::_fast_faccess_0      , ubcp|____|____|____, vtos, ftos, fast_xaccess        ,  ftos        , true);

    def(Bytecodes::_fast_iload          , ubcp|____|____|____, vtos, itos, fast_iload          ,  _       , true);
    def(Bytecodes::_fast_iload2         , ubcp|____|____|____, vtos, itos, fast_iload2         ,  _       , true);
    def(Bytecodes::_fast_icaload        , ubcp|____|____|____, vtos, itos, fast_icaload        ,  _       , true);

    def(Bytecodes::_fast_invokevfinal   , ubcp|disp|clvm|____, vtos, vtos, jportal_fast_invokevfinal, f2_byte      , true);

    def(Bytecodes::_fast_linearswitch   , ubcp|disp|____|____, itos, vtos, jportal_fast_linearswitch,  _           , true);
    def(Bytecodes::_fast_binaryswitch   , ubcp|disp|____|____, itos, vtos, jportal_fast_binaryswitch,  _           , true);

    def(Bytecodes::_fast_aldc           , ubcp|____|clvm|____, vtos, atos, fast_aldc           ,  false       , true);
    def(Bytecodes::_fast_aldc_w         , ubcp|____|clvm|____, vtos, atos, fast_aldc           ,  true        , true);

    def(Bytecodes::_return_register_finalizer , ____|disp|clvm|____, vtos, vtos, jportal_return,  vtos        , true);

    def(Bytecodes::_invokehandle        , ubcp|disp|clvm|____, vtos, vtos, jportal_invokehandle, f1_byte      , true);

    def(Bytecodes::_nofast_getfield     , ubcp|____|clvm|____, vtos, vtos, nofast_getfield     , f1_byte      , true);
    def(Bytecodes::_nofast_putfield     , ubcp|____|clvm|____, vtos, vtos, nofast_putfield     , f2_byte      , true);

    def(Bytecodes::_nofast_aload_0      , ____|____|clvm|____, vtos, atos, nofast_aload_0      ,  _           , true);
    def(Bytecodes::_nofast_iload        , ubcp|____|clvm|____, vtos, itos, nofast_iload        ,  _           , true);

    def(Bytecodes::_shouldnotreachhere   , ____|____|____|____, vtos, vtos, shouldnotreachhere ,  _           , true);
  }
#endif
  // platform specific bytecodes
  pd_initialize();

  _is_initialized = true;
}

#if defined(TEMPLATE_TABLE_BUG)
  #undef iload
  #undef lload
  #undef fload
  #undef dload
  #undef aload
  #undef istore
  #undef lstore
  #undef fstore
  #undef dstore
  #undef astore
#endif // TEMPLATE_TABLE_BUG


void templateTable_init() {
  TemplateTable::initialize();
}


void TemplateTable::unimplemented_bc() {
  _masm->unimplemented( Bytecodes::name(_desc->bytecode()));
}
#endif /* !CC_INTERP */

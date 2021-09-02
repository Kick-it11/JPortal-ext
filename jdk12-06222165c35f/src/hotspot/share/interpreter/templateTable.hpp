/*
 * Copyright (c) 1997, 2018, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_VM_INTERPRETER_TEMPLATETABLE_HPP
#define SHARE_VM_INTERPRETER_TEMPLATETABLE_HPP

#include "interpreter/bytecodes.hpp"
#include "memory/allocation.hpp"
#include "runtime/frame.hpp"
#include "utilities/macros.hpp"

#ifndef CC_INTERP
// All the necessary definitions used for (bytecode) template generation. Instead of
// spreading the implementation functionality for each bytecode in the interpreter
// and the snippet generator, a template is assigned to each bytecode which can be
// used to generate the bytecode's implementation if needed.

class BarrierSet;
class InterpreterMacroAssembler;

// A Template describes the properties of a code template for a given bytecode
// and provides a generator to generate the code template.

class Template {
 private:
  enum Flags {
    uses_bcp_bit,                                // set if template needs the bcp pointing to bytecode
    does_dispatch_bit,                           // set if template dispatches on its own
    calls_vm_bit,                                // set if template calls the vm
    wide_bit                                     // set if template belongs to a wide instruction
  };

  typedef void (*generator)(int arg);

  int       _flags;                              // describes interpreter template properties (bcp unknown)
  TosState  _tos_in;                             // tos cache state before template execution
  TosState  _tos_out;                            // tos cache state after  template execution
  generator _gen;                                // template code generator
  int       _arg;                                // argument for template code generator

  void      initialize(int flags, TosState tos_in, TosState tos_out, generator gen, int arg);

  friend class TemplateTable;

 public:
  Bytecodes::Code bytecode() const;
  bool      is_valid() const                     { return _gen != NULL; }
  bool      uses_bcp() const                     { return (_flags & (1 << uses_bcp_bit     )) != 0; }
  bool      does_dispatch() const                { return (_flags & (1 << does_dispatch_bit)) != 0; }
  bool      calls_vm() const                     { return (_flags & (1 << calls_vm_bit     )) != 0; }
  bool      is_wide() const                      { return (_flags & (1 << wide_bit         )) != 0; }
  TosState  tos_in() const                       { return _tos_in; }
  TosState  tos_out() const                      { return _tos_out; }
  void      generate(InterpreterMacroAssembler* masm);
};


// The TemplateTable defines all Templates and provides accessor functions
// to get the template for a given bytecode.

class TemplateTable: AllStatic {
 public:
  enum Operation { add, sub, mul, div, rem, _and, _or, _xor, shl, shr, ushr };
  enum Condition { equal, not_equal, less, less_equal, greater, greater_equal };
  enum CacheByte { f1_byte = 1, f2_byte = 2 };  // byte_no codes
  enum RewriteControl { may_rewrite, may_not_rewrite };  // control for fast code under CDS

 private:
  static bool            _is_initialized;        // true if TemplateTable has been initialized
  static Template        _normal_template_table     [Bytecodes::number_of_codes];
  static Template        _normal_template_table_wide[Bytecodes::number_of_codes];
  static Template        _jportal_template_table     [Bytecodes::number_of_codes];
  static Template        _jportal_template_table_wide[Bytecodes::number_of_codes];

  static Template*       _desc;                  // the current template to be generated
  static Bytecodes::Code bytecode()              { return _desc->bytecode(); }

  static BarrierSet*     _bs;                    // Cache the barrier set.
 public:
  //%note templates_1
  static InterpreterMacroAssembler* _masm;       // the assembler used when generating templates

 private:

  // special registers
  static inline Address at_bcp(int offset);

  // helpers
  static void unimplemented_bc();
  static void patch_bytecode(Bytecodes::Code bc, Register bc_reg,
                             Register temp_reg, bool jportal, bool load_bc_into_bc_reg = true, int byte_no = -1);

  // C calls
  static void call_VM(Register oop_result, address entry_point, bool jportal);
  static void call_VM(Register oop_result, address entry_point, Register arg_1, bool jportal);
  static void call_VM(Register oop_result, address entry_point, Register arg_1, Register arg_2, bool jportal);
  static void call_VM(Register oop_result, address entry_point, Register arg_1, Register arg_2, Register arg_3, bool jportal);

  // these overloadings are not presently used on SPARC:
  static void call_VM(Register oop_result, Register last_java_sp, address entry_point, bool jportal);
  static void call_VM(Register oop_result, Register last_java_sp, address entry_point, Register arg_1, bool jportal);
  static void call_VM(Register oop_result, Register last_java_sp, address entry_point, Register arg_1, Register arg_2, bool jportal);
  static void call_VM(Register oop_result, Register last_java_sp, address entry_point, Register arg_1, Register arg_2, Register arg_3, bool jportal);

  // bytecodes
  static void nop();

  static void aconst_null();
  static void iconst(int value);
  static void lconst(int value);
  static void fconst(int value);
  static void dconst(int value);

  static void bipush();
  static void sipush();
  static void normal_ldc(bool wide) { ldc(wide, false); }
  static void jportal_ldc(bool wide) { ldc(wide, true); }
  static void ldc(bool wide, bool jportal);
  static void normal_ldc2_w() { ldc2_w(false); }
  static void jportal_ldc2_w() { ldc2_w(true); }
  static void ldc2_w(bool jportal);
  static void normal_fast_aldc(bool wide) { fast_aldc(wide, false); }
  static void jportal_fast_aldc(bool wide) { fast_aldc(wide, true); }
  static void fast_aldc(bool wide, bool jportal);

  static void locals_index(Register reg, int offset = 1);
  static void normal_iload() { iload(false); }
  static void jportal_iload() { iload(true); }
  static void iload(bool jportal);
  static void fast_iload();
  static void fast_iload2();
  static void normal_fast_icaload() { fast_icaload(false); }
  static void jportal_fast_icaload() { fast_icaload(true); }
  static void fast_icaload(bool jportal);
  static void lload();
  static void fload();
  static void dload();
  static void aload();

  static void locals_index_wide(Register reg);
  static void wide_iload();
  static void wide_lload();
  static void wide_fload();
  static void wide_dload();
  static void wide_aload();

  static void normal_iaload() { iaload(false); }
  static void jportal_iaload() { iaload(true); }
  static void iaload(bool jportal);
  static void normal_laload() { laload(false); };
  static void jportal_laload() { laload(true); }
  static void laload(bool jportal);
  static void normal_faload() { faload(false); }
  static void jportal_faload() { faload(true); }
  static void faload(bool jportal);
  static void normal_daload() { daload(false); }
  static void jportal_daload() { daload(true); }
  static void daload(bool jportal);
  static void normal_aaload() { aaload(false); }
  static void jportal_aaload() { aaload(true); }
  static void aaload(bool jportal);
  static void normal_baload() { baload(false); }
  static void jportal_baload() { baload(true); }
  static void baload(bool jportal);
  static void normal_caload() { caload(false); }
  static void jportal_caload() { caload(true); }
  static void caload(bool jportal);
  static void normal_saload() { saload(false); }
  static void jportal_saload() {  saload(true); }
  static void saload(bool jportal);

  static void iload(int n);
  static void lload(int n);
  static void fload(int n);
  static void dload(int n);
  static void aload(int n);
  static void normal_aload_0() { aload_0(false); }
  static void jportal_aload_0() { aload_0(true); }
  static void aload_0(bool jportal);
  static void normal_nofast_aload_0() { nofast_aload_0(false); }
  static void jportal_nofast_aload_0() { nofast_aload_0(true); }
  static void nofast_aload_0(bool jportal);
  static void normal_nofast_iload() { nofast_iload(false); }
  static void jportal_nofast_iload() { nofast_iload(true); }
  static void nofast_iload(bool jportal);
  static void iload_internal(bool jportal, RewriteControl rc = may_rewrite);
  static void aload_0_internal(bool jportal, RewriteControl rc = may_rewrite);

  static void istore();
  static void lstore();
  static void fstore();
  static void dstore();
  static void astore();

  static void wide_istore();
  static void wide_lstore();
  static void wide_fstore();
  static void wide_dstore();
  static void wide_astore();

  static void normal_iastore() { iastore(false); }
  static void jportal_iastore() { iastore(true); }
  static void iastore(bool jportal);
  static void normal_lastore() { lastore(false); }
  static void jportal_lastore() { lastore(true); }
  static void lastore(bool jportal);
  static void normal_fastore() { fastore(false); }
  static void jportal_fastore() { fastore(true); }
  static void fastore(bool jportal);
  static void normal_dastore() { dastore(false); }
  static void jportal_dastore() { dastore(true); }
  static void dastore(bool jportal);
  static void normal_aastore() { aastore(false); }
  static void jportal_aastore() { aastore(true); }
  static void aastore(bool jportal);
  static void normal_bastore() { bastore(false); }
  static void jportal_bastore() { bastore(true); }
  static void bastore(bool jportal);
  static void normal_castore() { castore(false); }
  static void jportal_castore() { castore(true); }
  static void castore(bool jportal);
  static void normal_sastore() { sastore(false); }
  static void jportal_sastore() { sastore(true); }
  static void sastore(bool jportal);

  static void istore(int n);
  static void lstore(int n);
  static void fstore(int n);
  static void dstore(int n);
  static void astore(int n);

  static void pop();
  static void pop2();
  static void dup();
  static void dup_x1();
  static void dup_x2();
  static void dup2();
  static void dup2_x1();
  static void dup2_x2();
  static void swap();

  static void iop2(Operation op);
  static void lop2(Operation op);
  static void fop2(Operation op);
  static void dop2(Operation op);

  static void idiv();
  static void irem();

  static void lmul();
  static void normal_ldiv() { ldiv(false); }
  static void jportal_ldiv() { ldiv(true); }
  static void ldiv(bool jportal);
  static void normal_lrem() { lrem(false); }
  static void jportal_lrem() { lrem(true); }
  static void lrem(bool jportal);
  static void lshl();
  static void lshr();
  static void lushr();

  static void ineg();
  static void lneg();
  static void fneg();
  static void dneg();

  static void iinc();
  static void wide_iinc();
  static void convert();
  static void lcmp();

  static void float_cmp (bool is_float, int unordered_result);
  static void float_cmp (int unordered_result);
  static void double_cmp(int unordered_result);

  static void count_calls(Register method, Register temp);
  static void branch(bool is_jsr, bool is_wide, bool jportal);
  static void normal_if_0cmp   (Condition cc) { if_0cmp(cc, false); }
  static void jportal_if_0cmp   (Condition cc) { if_0cmp(cc, true); }
  static void if_0cmp   (Condition cc, bool jportal);
  static void normal_if_icmp   (Condition cc) { if_icmp(cc, false); }
  static void jportal_if_icmp   (Condition cc) { if_icmp(cc, true); }
  static void if_icmp   (Condition cc, bool jportal);
  static void normal_if_nullcmp(Condition cc) { if_nullcmp(cc, false); }
  static void jportal_if_nullcmp(Condition cc) { if_nullcmp(cc, true); }
  static void if_nullcmp(Condition cc, bool jportal);
  static void normal_if_acmp   (Condition cc) { if_acmp(cc, false); }
  static void jportal_if_acmp   (Condition cc) { if_acmp(cc, true); }
  static void if_acmp   (Condition cc, bool jportal);

  static void normal_goto() { _goto(false); }
  static void jportal_goto() { _goto(true); }
  static void _goto(bool jportal);
  static void normal_jsr() { jsr(false); }
  static void jportal_jsr() { jsr(true); }
  static void jsr(bool jportal);
  static void normal_ret() { ret(false); }
  static void jportal_ret() { ret(true); }
  static void ret(bool jportal);
  static void normal_wide_ret() { wide_ret(false); }
  static void jportal_wide_ret() { wide_ret(true); }
  static void wide_ret(bool jportal);

  static void normal_goto_w() { goto_w(false); }
  static void jportal_goto_w() { goto_w(true); }
  static void goto_w(bool jportal);
  static void normal_jsr_w() { jsr_w(false); }
  static void jportal_jsr_w() { jsr_w(true); }
  static void jsr_w(bool jportal);

  static void normal_tableswitch() { tableswitch(false); }
  static void jportal_tableswitch() { tableswitch(true); }
  static void tableswitch(bool jportal);
  static void lookupswitch();
  static void normal_fast_linearswitch() { fast_linearswitch(false); }
  static void jportal_fast_linearswitch() { fast_linearswitch(true); }
  static void fast_linearswitch(bool jportal);
  static void normal_fast_binaryswitch() { fast_binaryswitch(false); }
  static void jportal_fast_binaryswitch() { fast_binaryswitch(true); }
  static void fast_binaryswitch(bool jportal);

  static void normal_return(TosState state) { _return(state, false); }
  static void jportal_return(TosState state) { _return(state, true); }
  static void _return(TosState state, bool jportal);

  static void resolve_cache_and_index(int byte_no,       // one of 1,2,11
                                      Register cache,    // output for CP cache
                                      Register index,    // output for CP index
                                      size_t index_size, // one of 1,2,4
                                      bool jportal);
  static void load_invoke_cp_cache_entry(int byte_no,
                                         Register method,
                                         Register itable_index,
                                         Register flags,
                                         bool is_invokevirtual,
                                         bool is_virtual_final,
                                         bool is_invokedynamic,
                                         bool jportal);
  static void load_field_cp_cache_entry(Register obj,
                                        Register cache,
                                        Register index,
                                        Register offset,
                                        Register flags,
                                        bool is_static);
  static void normal_invokevirtual(int byte_no) { invokevirtual(byte_no, false); }
  static void jportal_invokevirtual(int byte_no) { invokevirtual(byte_no, true); }
  static void invokevirtual(int byte_no, bool jportal);
  static void normal_invokespecial(int byte_no) { invokespecial(byte_no, false); }
  static void jportal_invokespecial(int byte_no) { invokespecial(byte_no, true); }
  static void invokespecial(int byte_no, bool jportal);
  static void normal_invokestatic(int byte_no) { invokestatic(byte_no, false); }
  static void jportal_invokestatic(int byte_no) { invokestatic(byte_no, true); }
  static void invokestatic(int byte_no, bool jportal);
  static void normal_invokeinterface(int byte_no) { invokeinterface(byte_no, false); }
  static void jportal_invokeinterface(int byte_no) { invokeinterface(byte_no, true); }
  static void invokeinterface(int byte_no, bool jportal);
  static void normal_invokedynamic(int byte_no) { invokedynamic(byte_no, false); }
  static void jportal_invokedynamic(int byte_no) { invokedynamic(byte_no, true); }
  static void invokedynamic(int byte_no, bool jportal);
  static void normal_invokehandle(int byte_no) { invokehandle(byte_no, false); }
  static void jportal_invokehandle(int byte_no) { invokehandle(byte_no, true); }
  static void invokehandle(int byte_no, bool jportal);
  static void fast_invokevfinal(int byte_no);

  static void getfield_or_static(int byte_no, bool is_static, bool jportal, RewriteControl rc = may_rewrite);
  static void putfield_or_static(int byte_no, bool is_static, bool jportal, RewriteControl rc = may_rewrite);

  static void normal_getfield(int byte_no) { getfield(byte_no, false); }
  static void jportal_getfield(int byte_no) { getfield(byte_no, true); }
  static void getfield(int byte_no, bool jportal);
  static void normal_putfield(int byte_no) { putfield(byte_no, false); }
  static void jportal_putfield(int byte_no) { putfield(byte_no, true); }
  static void putfield(int byte_no, bool jportal);
  static void normal_nofast_getfield(int byte_no) { nofast_getfield(byte_no, false); }
  static void jportal_nofast_getfield(int byte_no) { nofast_getfield(byte_no, true); }
  static void nofast_getfield(int byte_no, bool jportal);
  static void normal_nofast_putfield(int byte_no) { nofast_putfield(byte_no, false); }
  static void jportal_nofast_putfield(int byte_no) { nofast_putfield(byte_no, true); }
  static void nofast_putfield(int byte_no, bool jportal);
  static void normal_getstatic(int byte_no) { getstatic(byte_no, false); }
  static void jportal_getstatic(int byte_no) { getstatic(byte_no, true); }
  static void getstatic(int byte_no, bool jportal);
  static void normal_putstatic(int byte_no) { putstatic(byte_no, false); }
  static void jportal_putstatic(int byte_no) { putstatic(byte_no, true); }
  static void putstatic(int byte_no, bool jportal);
  static void pop_and_check_object(Register obj);
  static void condy_helper(Label& Done, bool jportal);  // shared by ldc instances

  static void normal_new() { _new(false); }
  static void jportal_new() { _new(true); }
  static void _new(bool jportal);
  static void normal_newarray() { newarray(false); }
  static void jportal_newarray() { newarray(true); }
  static void newarray(bool jportal);
  static void normal_anewarray() { anewarray(false); }
  static void jportal_anewarray() { anewarray(true); }
  static void anewarray(bool jportal);
  static void arraylength();
  static void normal_checkcast() { checkcast(false); }
  static void jportal_checkcast() { checkcast(true); }
  static void checkcast(bool jportal);
  static void normal_instanceof() { instanceof(false); }
  static void jportal_instanceof() { instanceof(true); }
  static void instanceof(bool jportal);

  static void normal_athrow() { athrow(false); }
  static void jportal_athrow() { athrow(true); }
  static void athrow(bool jportal);

  static void normal_monitorenter() { monitorenter(false); }
  static void jportal_monitorenter() { monitorenter(true); }
  static void monitorenter(bool jportal);
  static void normal_monitorexit() { monitorexit(false); }
  static void jportal_monitorexit() { monitorexit(true); }
  static void monitorexit(bool jportal);

  static void normal_wide() { wide(false); };
  static void jportal_wide() { wide(true); }
  static void wide(bool jportal);
  static void normal_multianewarray() { multianewarray(false); }
  static void jportal_multianewarray() { multianewarray(true); }
  static void multianewarray(bool jportal);

  static void fast_xaccess(TosState state);
  static void normal_fast_accessfield(TosState state) { fast_accessfield(state, false); }
  static void jportal_fast_accessfield(TosState state) { fast_accessfield(state, true); }
  static void fast_accessfield(TosState state, bool jportal);
  static void normal_fast_storefield(TosState state) { fast_storefield(state, false); }
  static void jportal_fast_storefield(TosState state) { fast_storefield(state, true); }
  static void fast_storefield(TosState state, bool jportal);

  static void normal_breakpoint() { _breakpoint(false); }
  static void jportal_breakpoint() { _breakpoint(true); }
  static void _breakpoint(bool jportal);

  static void shouldnotreachhere();

  // jvmti support
  static void jvmti_post_field_access(Register cache, Register index, bool is_static, bool has_tos, bool jportal);
  static void jvmti_post_field_mod(Register cache, Register index, bool is_static, bool jportal);
  static void jvmti_post_fast_field_mod(bool jportal);

  // debugging of TemplateGenerator
  static void transition(TosState tos_in, TosState tos_out);// checks if in/out states expected by template generator correspond to table entries

  // initialization helpers
  static void def(Bytecodes::Code code, int flags, TosState in, TosState out, void (*gen)(            ), char filler, bool jportal );
  static void def(Bytecodes::Code code, int flags, TosState in, TosState out, void (*gen)(int arg     ), int arg, bool jportal     );
 static void def(Bytecodes::Code code, int flags, TosState in, TosState out, void (*gen)(bool arg    ), bool arg, bool jportal    );
  static void def(Bytecodes::Code code, int flags, TosState in, TosState out, void (*gen)(TosState tos), TosState tos, bool jportal);
  static void def(Bytecodes::Code code, int flags, TosState in, TosState out, void (*gen)(Operation op), Operation op, bool jportal);
  static void def(Bytecodes::Code code, int flags, TosState in, TosState out, void (*gen)(Condition cc), Condition cc, bool jportal);

  friend class Template;

  // InterpreterMacroAssembler::is_a(), etc., need TemplateTable::call_VM().
  friend class InterpreterMacroAssembler;

 public:
  // Initialization
  static void initialize();
  static void pd_initialize();

  // Templates
  static Template* template_for     (Bytecodes::Code code, bool jportal)  { Bytecodes::check     (code); return &((jportal?_jportal_template_table:_normal_template_table)     [code]); }
  static Template* template_for_wide(Bytecodes::Code code, bool jportal)  { Bytecodes::wide_check(code); return &((jportal?_jportal_template_table_wide:_normal_template_table_wide)[code]); }

  // Platform specifics
#include CPU_HEADER(templateTable)

};
#endif /* !CC_INTERP */

#endif // SHARE_VM_INTERPRETER_TEMPLATETABLE_HPP

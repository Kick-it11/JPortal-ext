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

#include "precompiled.hpp"
#include "compiler/disassembler.hpp"
#include "interpreter/interpreter.hpp"
#include "interpreter/interpreterRuntime.hpp"
#include "interpreter/interp_masm.hpp"
#include "interpreter/templateInterpreter.hpp"
#include "interpreter/templateInterpreterGenerator.hpp"
#include "interpreter/templateTable.hpp"
#include "oops/methodData.hpp"

#ifndef CC_INTERP

#define __ Disassembler::hook<InterpreterMacroAssembler>(__FILE__, __LINE__, _masm)->

TemplateInterpreterGenerator::TemplateInterpreterGenerator(StubQueue* _code, StubQueue* _jportal_code): AbstractInterpreterGenerator(_code, _jportal_code) {
  _normal_unimplemented_bytecode     = NULL;
  _normal_illegal_bytecode_sequence  = NULL;
  _jportal_unimplemented_bytecode    = NULL;
  _jportal_illegal_bytecode_sequence = NULL;
  generate_all();
}

static const BasicType types[Interpreter::number_of_result_handlers] = {
  T_BOOLEAN,
  T_CHAR   ,
  T_BYTE   ,
  T_SHORT  ,
  T_INT    ,
  T_LONG   ,
  T_VOID   ,
  T_FLOAT  ,
  T_DOUBLE ,
  T_OBJECT
};

void TemplateInterpreterGenerator::generate_all() {
  { CodeletMark cm(_masm, "slow signature handler", false);
    AbstractInterpreter::_normal_slow_signature_handler = generate_slow_signature_handler(false);
  }
  { CodeletMark cm(_masm, "error exits", false);
    _normal_unimplemented_bytecode    = generate_error_exit("unimplemented bytecode");
    _normal_illegal_bytecode_sequence = generate_error_exit("illegal bytecode sequence - method not verified");
  }

#ifndef PRODUCT
  if (TraceBytecodes) {
    CodeletMark cm(_masm, "bytecode tracing support", false);
    Interpreter::_normal_trace_code =
      EntryPoint(
                 generate_trace_code(btos, false),
                 generate_trace_code(ztos, false),
                 generate_trace_code(ctos, false),
                 generate_trace_code(stos, false),
                 generate_trace_code(atos, false),
                 generate_trace_code(itos, false),
                 generate_trace_code(ltos, false),
                 generate_trace_code(ftos, false),
                 generate_trace_code(dtos, false),
                 generate_trace_code(vtos, false)
                 );
  }
#endif // !PRODUCT

  { CodeletMark cm(_masm, "return entry points", false);
    const int index_size = sizeof(u2);
    Interpreter::_normal_return_entry[0] = EntryPoint();
    for (int i = 1; i < Interpreter::number_of_return_entries; i++) {
      address return_itos = generate_return_entry_for(itos, i, index_size, false);
      Interpreter::_normal_return_entry[i] =
        EntryPoint(
                   return_itos,
                   return_itos,
                   return_itos,
                   return_itos,
                   generate_return_entry_for(atos, i, index_size, false),
                   return_itos,
                   generate_return_entry_for(ltos, i, index_size, false),
                   generate_return_entry_for(ftos, i, index_size, false),
                   generate_return_entry_for(dtos, i, index_size, false),
                   generate_return_entry_for(vtos, i, index_size, false)
                   );
    }
  }

  { CodeletMark cm(_masm, "invoke return entry points", false);
    // These states are in order specified in TosState, except btos/ztos/ctos/stos are
    // really the same as itos since there is no top of stack optimization for these types
    const TosState states[] = {itos, itos, itos, itos, itos, ltos, ftos, dtos, atos, vtos, ilgl};
    const int invoke_length = Bytecodes::length_for(Bytecodes::_invokestatic);
    const int invokeinterface_length = Bytecodes::length_for(Bytecodes::_invokeinterface);
    const int invokedynamic_length = Bytecodes::length_for(Bytecodes::_invokedynamic);

    for (int i = 0; i < Interpreter::number_of_return_addrs; i++) {
      TosState state = states[i];
      assert(state != ilgl, "states array is wrong above");
      Interpreter::_normal_invoke_return_entry[i] = generate_return_entry_for(state, invoke_length, sizeof(u2), false);
      Interpreter::_normal_invokeinterface_return_entry[i] = generate_return_entry_for(state, invokeinterface_length, sizeof(u2), false);
      Interpreter::_normal_invokedynamic_return_entry[i] = generate_return_entry_for(state, invokedynamic_length, sizeof(u4), false);
    }
  }

  { CodeletMark cm(_masm, "earlyret entry points", false);
    Interpreter::_normal_earlyret_entry =
      EntryPoint(
                 generate_earlyret_entry_for(btos, false),
                 generate_earlyret_entry_for(ztos, false),
                 generate_earlyret_entry_for(ctos, false),
                 generate_earlyret_entry_for(stos, false),
                 generate_earlyret_entry_for(atos, false),
                 generate_earlyret_entry_for(itos, false),
                 generate_earlyret_entry_for(ltos, false),
                 generate_earlyret_entry_for(ftos, false),
                 generate_earlyret_entry_for(dtos, false),
                 generate_earlyret_entry_for(vtos, false)
                 );
  }

  { CodeletMark cm(_masm, "result handlers for native calls", false);
    // The various result converter stublets.
    int is_generated[Interpreter::number_of_result_handlers];
    memset(is_generated, 0, sizeof(is_generated));

    for (int i = 0; i < Interpreter::number_of_result_handlers; i++) {
      BasicType type = types[i];
      if (!is_generated[Interpreter::BasicType_as_index(type)]++) {
        Interpreter::_normal_native_abi_to_tosca[Interpreter::BasicType_as_index(type)] = generate_result_handler_for(type);
      }
    }
  }


  { CodeletMark cm(_masm, "safepoint entry points", false);
    Interpreter::_normal_safept_entry =
      EntryPoint(
                 generate_safept_entry_for(btos, CAST_FROM_FN_PTR(address, InterpreterRuntime::at_safepoint), false),
                 generate_safept_entry_for(ztos, CAST_FROM_FN_PTR(address, InterpreterRuntime::at_safepoint), false),
                 generate_safept_entry_for(ctos, CAST_FROM_FN_PTR(address, InterpreterRuntime::at_safepoint), false),
                 generate_safept_entry_for(stos, CAST_FROM_FN_PTR(address, InterpreterRuntime::at_safepoint), false),
                 generate_safept_entry_for(atos, CAST_FROM_FN_PTR(address, InterpreterRuntime::at_safepoint), false),
                 generate_safept_entry_for(itos, CAST_FROM_FN_PTR(address, InterpreterRuntime::at_safepoint), false),
                 generate_safept_entry_for(ltos, CAST_FROM_FN_PTR(address, InterpreterRuntime::at_safepoint), false),
                 generate_safept_entry_for(ftos, CAST_FROM_FN_PTR(address, InterpreterRuntime::at_safepoint), false),
                 generate_safept_entry_for(dtos, CAST_FROM_FN_PTR(address, InterpreterRuntime::at_safepoint), false),
                 generate_safept_entry_for(vtos, CAST_FROM_FN_PTR(address, InterpreterRuntime::at_safepoint), false)
                 );
  }

  { CodeletMark cm(_masm, "exception handling", false);
    // (Note: this is not safepoint safe because thread may return to compiled code)
    generate_throw_exception(false);
  }

  { CodeletMark cm(_masm, "throw exception entrypoints", false);
    Interpreter::_normal_throw_ArrayIndexOutOfBoundsException_entry = generate_ArrayIndexOutOfBounds_handler(false);
    Interpreter::_normal_throw_ArrayStoreException_entry            = generate_klass_exception_handler("java/lang/ArrayStoreException", false);
    Interpreter::_normal_throw_ArithmeticException_entry            = generate_exception_handler("java/lang/ArithmeticException", "/ by zero", false);
    Interpreter::_normal_throw_ClassCastException_entry             = generate_ClassCastException_handler(false);
    Interpreter::_normal_throw_NullPointerException_entry           = generate_exception_handler("java/lang/NullPointerException", NULL, false);
    Interpreter::_normal_throw_StackOverflowError_entry             = generate_StackOverflowError_handler(false);
  }



#define method_entry(kind)                                              \
  { CodeletMark cm(_masm, "method entry point (kind = " #kind ")", false); \
    Interpreter::_normal_entry_table[Interpreter::kind] = generate_method_entry(Interpreter::kind, false); \
    Interpreter::update_cds_entry_table(Interpreter::kind, false); \
  }

  // all non-native method kinds
  method_entry(zerolocals)
  method_entry(zerolocals_synchronized)
  method_entry(empty)
  method_entry(accessor)
  method_entry(abstract)
  method_entry(java_lang_math_sin  )
  method_entry(java_lang_math_cos  )
  method_entry(java_lang_math_tan  )
  method_entry(java_lang_math_abs  )
  method_entry(java_lang_math_sqrt )
  method_entry(java_lang_math_log  )
  method_entry(java_lang_math_log10)
  method_entry(java_lang_math_exp  )
  method_entry(java_lang_math_pow  )
  method_entry(java_lang_math_fmaF )
  method_entry(java_lang_math_fmaD )
  method_entry(java_lang_ref_reference_get)

  AbstractInterpreter::initialize_method_handle_entries(false);

  // all native method kinds (must be one contiguous block)
  Interpreter::_normal_native_entry_begin = Interpreter::code(false)->code_end();
  method_entry(native)
  method_entry(native_synchronized)
  Interpreter::_normal_native_entry_end = Interpreter::code(false)->code_end();

  method_entry(java_util_zip_CRC32_update)
  method_entry(java_util_zip_CRC32_updateBytes)
  method_entry(java_util_zip_CRC32_updateByteBuffer)
  method_entry(java_util_zip_CRC32C_updateBytes)
  method_entry(java_util_zip_CRC32C_updateDirectByteBuffer)

  method_entry(java_lang_Float_intBitsToFloat);
  method_entry(java_lang_Float_floatToRawIntBits);
  method_entry(java_lang_Double_longBitsToDouble);
  method_entry(java_lang_Double_doubleToRawLongBits);

#undef method_entry

  // Bytecodes
  set_entry_points_for_all_bytes(false);

  // installation of code in other places in the runtime
  // (ExcutableCodeManager calls not needed to copy the entries)
  set_safepoints_for_all_bytes(false);

  { CodeletMark cm(_masm, "deoptimization entry points", false);
    Interpreter::_normal_deopt_entry[0] = EntryPoint();
    Interpreter::_normal_deopt_entry[0].set_entry(vtos, generate_deopt_entry_for(vtos, 0, false));
    for (int i = 1; i < Interpreter::number_of_deopt_entries; i++) {
      address deopt_itos = generate_deopt_entry_for(itos, i, false);
      Interpreter::_normal_deopt_entry[i] =
        EntryPoint(
                   deopt_itos, /* btos */
                   deopt_itos, /* ztos */
                   deopt_itos, /* ctos */
                   deopt_itos, /* stos */
                   generate_deopt_entry_for(atos, i, false),
                   deopt_itos, /* itos */
                   generate_deopt_entry_for(ltos, i, false),
                   generate_deopt_entry_for(ftos, i, false),
                   generate_deopt_entry_for(dtos, i, false),
                   generate_deopt_entry_for(vtos, i, false)
                   );
    }
    address return_continuation = Interpreter::_normal_normal_table.entry(Bytecodes::_return).entry(vtos);
    vmassert(return_continuation != NULL, "return entry not generated yet");
    Interpreter::_normal_deopt_reexecute_return_entry = generate_deopt_entry_for(vtos, 0, false, return_continuation);
  }

if (JPortal) {
  { CodeletMark cm(_masm, "slow signature handler", true);
    AbstractInterpreter::_jportal_slow_signature_handler = generate_slow_signature_handler(true);
  }

  { CodeletMark cm(_masm, "error exits", true);
    _jportal_unimplemented_bytecode    = generate_error_exit("unimplemented bytecode");
    _jportal_illegal_bytecode_sequence = generate_error_exit("illegal bytecode sequence - method not verified");
  }

#ifndef PRODUCT
  if (TraceBytecodes) {
    CodeletMark cm(_masm, "bytecode tracing support", true);
    Interpreter::_jportal_trace_code =
      EntryPoint(
                 generate_trace_code(btos, true),
                 generate_trace_code(ztos, true),
                 generate_trace_code(ctos, true),
                 generate_trace_code(stos, true),
                 generate_trace_code(atos, true),
                 generate_trace_code(itos, true),
                 generate_trace_code(ltos, true),
                 generate_trace_code(ftos, true),
                 generate_trace_code(dtos, true),
                 generate_trace_code(vtos, true)
                 );
  }
#endif // !PRODUCT

  { CodeletMark cm(_masm, "return entry points", true);
    const int index_size = sizeof(u2);
    Interpreter::_jportal_return_entry[0] = EntryPoint();
    for (int i = 1; i < Interpreter::number_of_return_entries; i++) {
      address return_itos = generate_return_entry_for(itos, i, index_size, true);
      Interpreter::_jportal_return_entry[i] =
        EntryPoint(
                   return_itos,
                   return_itos,
                   return_itos,
                   return_itos,
                   generate_return_entry_for(atos, i, index_size, true),
                   return_itos,
                   generate_return_entry_for(ltos, i, index_size, true),
                   generate_return_entry_for(ftos, i, index_size, true),
                   generate_return_entry_for(dtos, i, index_size, true),
                   generate_return_entry_for(vtos, i, index_size, true)
                   );
    }
  }

  { CodeletMark cm(_masm, "invoke return entry points", true);
    // These states are in order specified in TosState, except btos/ztos/ctos/stos are
    // really the same as itos since there is no top of stack optimization for these types
    const TosState states[] = {itos, itos, itos, itos, itos, ltos, ftos, dtos, atos, vtos, ilgl};
    const int invoke_length = Bytecodes::length_for(Bytecodes::_invokestatic);
    const int invokeinterface_length = Bytecodes::length_for(Bytecodes::_invokeinterface);
    const int invokedynamic_length = Bytecodes::length_for(Bytecodes::_invokedynamic);

    for (int i = 0; i < Interpreter::number_of_return_addrs; i++) {
      TosState state = states[i];
      assert(state != ilgl, "states array is wrong above");
      Interpreter::_jportal_invoke_return_entry[i] = generate_return_entry_for(state, invoke_length, sizeof(u2), true);
      Interpreter::_jportal_invokeinterface_return_entry[i] = generate_return_entry_for(state, invokeinterface_length, sizeof(u2), true);
      Interpreter::_jportal_invokedynamic_return_entry[i] = generate_return_entry_for(state, invokedynamic_length, sizeof(u4), true);
    }
  }

  { CodeletMark cm(_masm, "earlyret entry points", true);
    Interpreter::_jportal_earlyret_entry =
      EntryPoint(
                 generate_earlyret_entry_for(btos, true),
                 generate_earlyret_entry_for(ztos, true),
                 generate_earlyret_entry_for(ctos, true),
                 generate_earlyret_entry_for(stos, true),
                 generate_earlyret_entry_for(atos, true),
                 generate_earlyret_entry_for(itos, true),
                 generate_earlyret_entry_for(ltos, true),
                 generate_earlyret_entry_for(ftos, true),
                 generate_earlyret_entry_for(dtos, true),
                 generate_earlyret_entry_for(vtos, true)
                 );
  }

  { CodeletMark cm(_masm, "result handlers for native calls", true);
    // The various result converter stublets.
    int is_generated[Interpreter::number_of_result_handlers];
    memset(is_generated, 0, sizeof(is_generated));

    for (int i = 0; i < Interpreter::number_of_result_handlers; i++) {
      BasicType type = types[i];
      if (!is_generated[Interpreter::BasicType_as_index(type)]++) {
        Interpreter::_jportal_native_abi_to_tosca[Interpreter::BasicType_as_index(type)] = generate_result_handler_for(type);
      }
    }
  }


  { CodeletMark cm(_masm, "safepoint entry points", true);
    Interpreter::_jportal_safept_entry =
      EntryPoint(
                 generate_safept_entry_for(btos, CAST_FROM_FN_PTR(address, InterpreterRuntime::at_safepoint), true),
                 generate_safept_entry_for(ztos, CAST_FROM_FN_PTR(address, InterpreterRuntime::at_safepoint), true),
                 generate_safept_entry_for(ctos, CAST_FROM_FN_PTR(address, InterpreterRuntime::at_safepoint), true),
                 generate_safept_entry_for(stos, CAST_FROM_FN_PTR(address, InterpreterRuntime::at_safepoint), true),
                 generate_safept_entry_for(atos, CAST_FROM_FN_PTR(address, InterpreterRuntime::at_safepoint), true),
                 generate_safept_entry_for(itos, CAST_FROM_FN_PTR(address, InterpreterRuntime::at_safepoint), true),
                 generate_safept_entry_for(ltos, CAST_FROM_FN_PTR(address, InterpreterRuntime::at_safepoint), true),
                 generate_safept_entry_for(ftos, CAST_FROM_FN_PTR(address, InterpreterRuntime::at_safepoint), true),
                 generate_safept_entry_for(dtos, CAST_FROM_FN_PTR(address, InterpreterRuntime::at_safepoint), true),
                 generate_safept_entry_for(vtos, CAST_FROM_FN_PTR(address, InterpreterRuntime::at_safepoint), true)
                 );
  }

  { CodeletMark cm(_masm, "exception handling", true);
    // (Note: this is not safepoint safe because thread may return to compiled code)
    generate_throw_exception(true);
  }

  { CodeletMark cm(_masm, "throw exception entrypoints", true);
    Interpreter::_jportal_throw_ArrayIndexOutOfBoundsException_entry = generate_ArrayIndexOutOfBounds_handler(true);
    Interpreter::_jportal_throw_ArrayStoreException_entry            = generate_klass_exception_handler("java/lang/ArrayStoreException", true);
    Interpreter::_jportal_throw_ArithmeticException_entry            = generate_exception_handler("java/lang/ArithmeticException", "/ by zero", true);
    Interpreter::_jportal_throw_ClassCastException_entry             = generate_ClassCastException_handler(true);
    Interpreter::_jportal_throw_NullPointerException_entry           = generate_exception_handler("java/lang/NullPointerException", NULL, true);
    Interpreter::_jportal_throw_StackOverflowError_entry             = generate_StackOverflowError_handler(true);
  }



#define method_entry(kind)                                              \
  { CodeletMark cm(_masm, "method entry point (kind = " #kind ")", true); \
    Interpreter::_jportal_entry_table[Interpreter::kind] = generate_method_entry(Interpreter::kind, true); \
    Interpreter::update_cds_entry_table(Interpreter::kind, true); \
  }

  // all non-native method kinds
  method_entry(zerolocals)
  method_entry(zerolocals_synchronized)
  method_entry(empty)
  method_entry(accessor)
  method_entry(abstract)
  method_entry(java_lang_math_sin  )
  method_entry(java_lang_math_cos  )
  method_entry(java_lang_math_tan  )
  method_entry(java_lang_math_abs  )
  method_entry(java_lang_math_sqrt )
  method_entry(java_lang_math_log  )
  method_entry(java_lang_math_log10)
  method_entry(java_lang_math_exp  )
  method_entry(java_lang_math_pow  )
  method_entry(java_lang_math_fmaF )
  method_entry(java_lang_math_fmaD )
  method_entry(java_lang_ref_reference_get)

  AbstractInterpreter::initialize_method_handle_entries(true);

  // all native method kinds (must be one contiguous block)
  Interpreter::_jportal_native_entry_begin = Interpreter::code(true)->code_end();
  method_entry(native)
  method_entry(native_synchronized)
  Interpreter::_jportal_native_entry_end = Interpreter::code(true)->code_end();

  method_entry(java_util_zip_CRC32_update)
  method_entry(java_util_zip_CRC32_updateBytes)
  method_entry(java_util_zip_CRC32_updateByteBuffer)
  method_entry(java_util_zip_CRC32C_updateBytes)
  method_entry(java_util_zip_CRC32C_updateDirectByteBuffer)

  method_entry(java_lang_Float_intBitsToFloat);
  method_entry(java_lang_Float_floatToRawIntBits);
  method_entry(java_lang_Double_longBitsToDouble);
  method_entry(java_lang_Double_doubleToRawLongBits);

#undef method_entry

  // Bytecodes
  set_entry_points_for_all_bytes(true);

  // installation of code in other places in the runtime
  // (ExcutableCodeManager calls not needed to copy the entries)
  set_safepoints_for_all_bytes(true);

  { CodeletMark cm(_masm, "deoptimization entry points", true);
    Interpreter::_jportal_deopt_entry[0] = EntryPoint();
    Interpreter::_jportal_deopt_entry[0].set_entry(vtos, generate_deopt_entry_for(vtos, 0, true));
    for (int i = 1; i < Interpreter::number_of_deopt_entries; i++) {
      address deopt_itos = generate_deopt_entry_for(itos, i, true);
      Interpreter::_jportal_deopt_entry[i] =
        EntryPoint(
                   deopt_itos, /* btos */
                   deopt_itos, /* ztos */
                   deopt_itos, /* ctos */
                   deopt_itos, /* stos */
                   generate_deopt_entry_for(atos, i, true),
                   deopt_itos, /* itos */
                   generate_deopt_entry_for(ltos, i, true),
                   generate_deopt_entry_for(ftos, i, true),
                   generate_deopt_entry_for(dtos, i, true),
                   generate_deopt_entry_for(vtos, i, true)
                   );
    }
    address return_continuation = Interpreter::_jportal_normal_table.entry(Bytecodes::_return).entry(vtos);
    vmassert(return_continuation != NULL, "return entry not generated yet");
    Interpreter::_jportal_deopt_reexecute_return_entry = generate_deopt_entry_for(vtos, 0, true, return_continuation);
  }
}
}

//------------------------------------------------------------------------------------------------------------------------

address TemplateInterpreterGenerator::generate_error_exit(const char* msg) {
  address entry = __ pc();
  __ stop(msg);
  return entry;
}


//------------------------------------------------------------------------------------------------------------------------

void TemplateInterpreterGenerator::set_entry_points_for_all_bytes(bool jportal) {
  for (int i = 0; i < DispatchTable::length; i++) {
    Bytecodes::Code code = (Bytecodes::Code)i;
    if (Bytecodes::is_defined(code)) {
      set_entry_points(code, jportal);
    } else {
      set_unimplemented(i, jportal);
    }
  }
}


void TemplateInterpreterGenerator::set_safepoints_for_all_bytes(bool jportal) {
  if (jportal) {
    for (int i = 0; i < DispatchTable::length; i++) {
      Bytecodes::Code code = (Bytecodes::Code)i;
      if (Bytecodes::is_defined(code)) Interpreter::_jportal_safept_table.set_entry(code, Interpreter::_jportal_safept_entry);
    }
  } else {
    for (int i = 0; i < DispatchTable::length; i++) {
      Bytecodes::Code code = (Bytecodes::Code)i;
      if (Bytecodes::is_defined(code)) Interpreter::_normal_safept_table.set_entry(code, Interpreter::_normal_safept_entry);
    }
  }
}


void TemplateInterpreterGenerator::set_unimplemented(int i, bool jportal) {
  if (jportal) {
    address e = _jportal_unimplemented_bytecode;
    EntryPoint entry(e, e, e, e, e, e, e, e, e, e);
    Interpreter::_jportal_normal_table.set_entry(i, entry);
    Interpreter::_jportal_wentry_point[i] = _jportal_unimplemented_bytecode;
    return;
  } else {
    address e = _normal_unimplemented_bytecode;
    EntryPoint entry(e, e, e, e, e, e, e, e, e, e);
    Interpreter::_normal_normal_table.set_entry(i, entry);
    Interpreter::_normal_wentry_point[i] = _normal_unimplemented_bytecode;
  }
}


void TemplateInterpreterGenerator::set_entry_points(Bytecodes::Code code, bool jportal) {
  CodeletMark cm(_masm, Bytecodes::name(code), jportal, code);
  // initialize entry points
  address _unimplemented_bytecode = jportal?_jportal_unimplemented_bytecode:_normal_unimplemented_bytecode;
  address _illegal_bytecode_sequence = jportal?_jportal_illegal_bytecode_sequence:_normal_illegal_bytecode_sequence;
  assert(_unimplemented_bytecode    != NULL, "should have been generated before");
  assert(_illegal_bytecode_sequence != NULL, "should have been generated before");
  address bep = _illegal_bytecode_sequence;
  address zep = _illegal_bytecode_sequence;
  address cep = _illegal_bytecode_sequence;
  address sep = _illegal_bytecode_sequence;
  address aep = _illegal_bytecode_sequence;
  address iep = _illegal_bytecode_sequence;
  address lep = _illegal_bytecode_sequence;
  address fep = _illegal_bytecode_sequence;
  address dep = _illegal_bytecode_sequence;
  address vep = _unimplemented_bytecode;
  address wep = _unimplemented_bytecode;
  // code for short & wide version of bytecode
  if (Bytecodes::is_defined(code)) {
    Template* t = TemplateTable::template_for(code, jportal);
    assert(t->is_valid(), "just checking");
    set_short_entry_points(t, bep, cep, sep, aep, iep, lep, fep, dep, vep, jportal);
  }
  if (Bytecodes::wide_is_defined(code)) {
    Template* t = TemplateTable::template_for_wide(code, jportal);
    assert(t->is_valid(), "just checking");
    set_wide_entry_point(t, wep, jportal);
  }
  // set entry points
  EntryPoint entry(bep, zep, cep, sep, aep, iep, lep, fep, dep, vep);
  jportal?Interpreter::_jportal_normal_table.set_entry(code, entry):Interpreter::_normal_normal_table.set_entry(code, entry);
  (jportal?Interpreter::_jportal_wentry_point[code]:Interpreter::_normal_wentry_point[code]) = wep;
}


void TemplateInterpreterGenerator::set_wide_entry_point(Template* t, address& wep, bool jportal) {
  assert(t->is_valid(), "template must exist");
  assert(t->tos_in() == vtos, "only vtos tos_in supported for wide instructions");
  wep = __ pc(); generate_and_dispatch(t, jportal);
}


void TemplateInterpreterGenerator::set_short_entry_points(Template* t, address& bep, address& cep, address& sep, address& aep, address& iep, address& lep, address& fep, address& dep, address& vep, bool jportal) {
  assert(t->is_valid(), "template must exist");
  switch (t->tos_in()) {
    case btos:
    case ztos:
    case ctos:
    case stos:
      ShouldNotReachHere();  // btos/ctos/stos should use itos.
      break;
    case atos: vep = __ pc(); __ pop(atos); aep = __ pc(); generate_and_dispatch(t, jportal); break;
    case itos: vep = __ pc(); __ pop(itos); iep = __ pc(); generate_and_dispatch(t, jportal); break;
    case ltos: vep = __ pc(); __ pop(ltos); lep = __ pc(); generate_and_dispatch(t, jportal); break;
    case ftos: vep = __ pc(); __ pop(ftos); fep = __ pc(); generate_and_dispatch(t, jportal); break;
    case dtos: vep = __ pc(); __ pop(dtos); dep = __ pc(); generate_and_dispatch(t, jportal); break;
    case vtos: set_vtos_entry_points(t, bep, cep, sep, aep, iep, lep, fep, dep, vep, jportal);     break;
    default  : ShouldNotReachHere();                                                 break;
  }
}


//------------------------------------------------------------------------------------------------------------------------

void TemplateInterpreterGenerator::generate_and_dispatch(Template* t, bool jportal, TosState tos_out) {
  if (PrintBytecodeHistogram)                                    histogram_bytecode(t);
#ifndef PRODUCT
  // debugging code
  if (CountBytecodes || TraceBytecodes || StopInterpreterAt > 0) count_bytecode();
  if (PrintBytecodePairHistogram)                                histogram_bytecode_pair(t);
  if (TraceBytecodes)                                            trace_bytecode(t, jportal);
  if (StopInterpreterAt > 0)                                     stop_interpreter_at();
  __ verify_FPU(1, t->tos_in());
#endif // !PRODUCT
  int step = 0;
  if (!t->does_dispatch()) {
    step = t->is_wide() ? Bytecodes::wide_length_for(t->bytecode()) : Bytecodes::length_for(t->bytecode());
    if (tos_out == ilgl) tos_out = t->tos_out();
    // compute bytecode size
    assert(step > 0, "just checkin'");
    // setup stuff for dispatching next bytecode
    if (ProfileInterpreter && VerifyDataPointer
        && MethodData::bytecode_has_profile(t->bytecode())) {
      __ verify_method_data_pointer();
    }
    __ dispatch_prolog(tos_out, step);
  }
  // generate template
  t->generate(_masm);
  // advance
  if (t->does_dispatch()) {
#ifdef ASSERT
    // make sure execution doesn't go beyond this point if code is broken
    __ should_not_reach_here();
#endif // ASSERT
  } else {
    // dispatch to next bytecode
    __ dispatch_epilog(tos_out, jportal, step);
  }
}

// Generate method entries
address TemplateInterpreterGenerator::generate_method_entry(
                                        AbstractInterpreter::MethodKind kind, bool jportal) {
  // determine code generation flags
  bool native = false;
  bool synchronized = false;
  address entry_point = NULL;

  switch (kind) {
  case Interpreter::zerolocals             :                                          break;
  case Interpreter::zerolocals_synchronized:                synchronized = true;      break;
  case Interpreter::native                 : native = true;                           break;
  case Interpreter::native_synchronized    : native = true; synchronized = true;      break;
  case Interpreter::empty                  : break;
  case Interpreter::accessor               : break;
  case Interpreter::abstract               : entry_point = generate_abstract_entry(jportal); break;

  case Interpreter::java_lang_math_sin     : // fall thru
  case Interpreter::java_lang_math_cos     : // fall thru
  case Interpreter::java_lang_math_tan     : // fall thru
  case Interpreter::java_lang_math_abs     : // fall thru
  case Interpreter::java_lang_math_log     : // fall thru
  case Interpreter::java_lang_math_log10   : // fall thru
  case Interpreter::java_lang_math_sqrt    : // fall thru
  case Interpreter::java_lang_math_pow     : // fall thru
  case Interpreter::java_lang_math_exp     : // fall thru
  case Interpreter::java_lang_math_fmaD    : // fall thru
  case Interpreter::java_lang_math_fmaF    : entry_point = generate_math_entry(kind);      break;
  case Interpreter::java_lang_ref_reference_get
                                           : entry_point = generate_Reference_get_entry(jportal); break;
  case Interpreter::java_util_zip_CRC32_update
                                           : native = true; entry_point = generate_CRC32_update_entry(jportal);  break;
  case Interpreter::java_util_zip_CRC32_updateBytes
                                           : // fall thru
  case Interpreter::java_util_zip_CRC32_updateByteBuffer
                                           : native = true; entry_point = generate_CRC32_updateBytes_entry(kind, jportal); break;
  case Interpreter::java_util_zip_CRC32C_updateBytes
                                           : // fall thru
  case Interpreter::java_util_zip_CRC32C_updateDirectByteBuffer
                                           : entry_point = generate_CRC32C_updateBytes_entry(kind); break;
#ifdef IA32
  // On x86_32 platforms, a special entry is generated for the following four methods.
  // On other platforms the normal entry is used to enter these methods.
  case Interpreter::java_lang_Float_intBitsToFloat
                                           : native = true; entry_point = generate_Float_intBitsToFloat_entry(); break;
  case Interpreter::java_lang_Float_floatToRawIntBits
                                           : native = true; entry_point = generate_Float_floatToRawIntBits_entry(); break;
  case Interpreter::java_lang_Double_longBitsToDouble
                                           : native = true; entry_point = generate_Double_longBitsToDouble_entry(); break;
  case Interpreter::java_lang_Double_doubleToRawLongBits
                                           : native = true; entry_point = generate_Double_doubleToRawLongBits_entry(); break;
#else
  case Interpreter::java_lang_Float_intBitsToFloat:
  case Interpreter::java_lang_Float_floatToRawIntBits:
  case Interpreter::java_lang_Double_longBitsToDouble:
  case Interpreter::java_lang_Double_doubleToRawLongBits:
    native = true;
    break;
#endif // !IA32
  default:
    fatal("unexpected method kind: %d", kind);
    break;
  }

  if (entry_point) {
    return entry_point;
  }

  // We expect the normal and native entry points to be generated first so we can reuse them.
  if (native) {
    entry_point = Interpreter::entry_for_kind(synchronized ? Interpreter::native_synchronized : Interpreter::native, jportal);
    if (entry_point == NULL) {
      entry_point = generate_native_entry(synchronized, jportal);
    }
  } else {
    entry_point = Interpreter::entry_for_kind(synchronized ? Interpreter::zerolocals_synchronized : Interpreter::zerolocals, jportal);
    if (entry_point == NULL) {
      entry_point = generate_normal_entry(synchronized, jportal);
    }
  }

  return entry_point;
}
#endif // !CC_INTERP

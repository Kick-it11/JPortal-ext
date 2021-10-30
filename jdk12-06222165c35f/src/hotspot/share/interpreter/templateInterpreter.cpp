/*
 * Copyright (c) 1997, 2017, Oracle and/or its affiliates. All rights reserved.
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
#include "interpreter/interpreter.hpp"
#include "interpreter/interpreterRuntime.hpp"
#include "interpreter/interp_masm.hpp"
#include "interpreter/templateInterpreter.hpp"
#include "interpreter/templateInterpreterGenerator.hpp"
#include "interpreter/templateTable.hpp"
#include "memory/resourceArea.hpp"
#include "runtime/timerTrace.hpp"

#ifndef CC_INTERP

# define __ _masm->

void TemplateInterpreter::initialize() {
  if (_normal_code != NULL) return;
  // assertions
  assert((int)Bytecodes::number_of_codes <= (int)DispatchTable::length,
         "dispatch table too small");

  AbstractInterpreter::initialize();

  TemplateTable::initialize();

  // generate interpreter
  { ResourceMark rm;
    TraceTime timer("Interpreter generation", TRACETIME_LOG(Info, startuptime));
    int code_size = InterpreterCodeSize;
    NOT_PRODUCT(code_size *= 4;)  // debug uses extra interpreter code space
    _normal_code = new StubQueue(new InterpreterCodeletInterface, code_size, NULL,
                          "Interpreter", false);
    if (JPortal) {
      _mirror_code = new StubQueue(new InterpreterCodeletInterface, code_size, NULL,
                            "Interpreter", false);
      _jportal_code = new StubQueue(new InterpreterCodeletInterface, code_size, NULL,
                            "Interpreter", true);
    }
    TemplateInterpreterGenerator g(_normal_code);
    // Free the unused memory not occupied by the interpreter and the stubs
    _normal_code->deallocate_unused_tail();
    if (JPortal) {
      _mirror_code->deallocate_unused_tail();
      _jportal_code->deallocate_unused_tail();
    }
  }

  if (JPortal) {
    jportal_entry_dump();
  }

  if (PrintInterpreter) {
    ResourceMark rm;
    print();
  }

  // initialize dispatch table
  _normal_active_table = _normal_normal_table;
  _mirror_active_table = _jportal_normal_table;
}

void TemplateInterpreter::jportal_entry_dump() {
  if (JPortal) {
    JPortalEnable::InterpreterInfo inter(sizeof(JPortalEnable::InterpreterInfo));
    inter.unimplemented_bytecode = (uint64_t)_jportal_unimplemented_bytecode;
    inter.illegal_bytecode_sequence = (uint64_t)_jportal_illegal_bytecode_sequence;
    for (int i = 0; i < number_of_return_entries; i++) {
      for (int j = 0; j < number_of_states; j++) {
        inter.return_entry[i][j] = (uint64_t)_jportal_return_entry[i].entry((TosState)j);
      }
    }
    for (int i = 0; i < number_of_return_addrs; i++) {
      inter.invoke_return_entry[i] = (uint64_t)_jportal_invoke_return_entry[i];
      inter.invokeinterface_return_entry[i] = (uint64_t)_jportal_invokeinterface_return_entry[i];
      inter.invokedynamic_return_entry[i] = (uint64_t)_jportal_invokedynamic_return_entry[i];
    }
    for (int i = 0; i < number_of_result_handlers; i++) {
      inter.native_abi_to_tosca[i] = (uint64_t)_jportal_native_abi_to_tosca[i];
    }
    inter.rethrow_exception_entry = (uint64_t)_jportal_rethrow_exception_entry;
    inter.throw_exception_entry = (uint64_t)_jportal_throw_exception_entry;
    inter.remove_activation_preserving_args_entry = (uint64_t)_jportal_remove_activation_preserving_args_entry;
    inter.remove_activation_entry = (uint64_t)_jportal_remove_activation_entry;
    inter.throw_ArrayIndexOutOfBoundsException_entry = (uint64_t)_jportal_throw_ArrayIndexOutOfBoundsException_entry;
    inter.throw_ArrayStoreException_entry = (uint64_t)_jportal_throw_ArrayStoreException_entry;
    inter.throw_ArithmeticException_entry = (uint64_t)_jportal_throw_ArithmeticException_entry;
    inter.throw_ClassCastException_entry = (uint64_t)_jportal_throw_ClassCastException_entry;
    inter.throw_NullPointerException_entry = (uint64_t)_jportal_throw_NullPointerException_entry;
    inter.throw_StackOverflowError_entry = (uint64_t)_jportal_throw_StackOverflowError_entry;
    for (int i = 0; i < number_of_method_entries; i++) {
      inter.entry_table[i] = (uint64_t)(_jportal_entry_table[i]);
    }
    for (int i = 0; i < DispatchTable::length; i++) {
      for (int j = 0; j < number_of_states; j++) {
        inter.normal_table[i][j] = (uint64_t)_jportal_normal_table.entry(i).entry((TosState)j);
      }
      inter.wentry_point[i] = (uint64_t)_jportal_wentry_point[i];
    }
    for (int i = 0; i < number_of_deopt_entries; i++) {
      for (int j = 0; j < number_of_states; j++) {
        inter.deopt_entry[i][j] = (uint64_t)(_jportal_deopt_entry[i].entry((TosState)j));
      }
    }
    inter.deopt_reexecute_return_entry = (uint64_t)_jportal_deopt_reexecute_return_entry;
    JPortalEnable::jportal_interpreter_codelets(inter);
  }
}
//------------------------------------------------------------------------------------------------------------------------
// Implementation of EntryPoint

EntryPoint::EntryPoint() {
  assert(number_of_states == 10, "check the code below");
  _entry[btos] = NULL;
  _entry[ztos] = NULL;
  _entry[ctos] = NULL;
  _entry[stos] = NULL;
  _entry[atos] = NULL;
  _entry[itos] = NULL;
  _entry[ltos] = NULL;
  _entry[ftos] = NULL;
  _entry[dtos] = NULL;
  _entry[vtos] = NULL;
}


EntryPoint::EntryPoint(address bentry, address zentry, address centry, address sentry, address aentry, address ientry, address lentry, address fentry, address dentry, address ventry) {
  assert(number_of_states == 10, "check the code below");
  _entry[btos] = bentry;
  _entry[ztos] = zentry;
  _entry[ctos] = centry;
  _entry[stos] = sentry;
  _entry[atos] = aentry;
  _entry[itos] = ientry;
  _entry[ltos] = lentry;
  _entry[ftos] = fentry;
  _entry[dtos] = dentry;
  _entry[vtos] = ventry;
}


void EntryPoint::set_entry(TosState state, address entry) {
  assert(0 <= state && state < number_of_states, "state out of bounds");
  _entry[state] = entry;
}


address EntryPoint::entry(TosState state) const {
  assert(0 <= state && state < number_of_states, "state out of bounds");
  return _entry[state];
}


void EntryPoint::print() {
  tty->print("[");
  for (int i = 0; i < number_of_states; i++) {
    if (i > 0) tty->print(", ");
    tty->print(INTPTR_FORMAT, p2i(_entry[i]));
  }
  tty->print("]");
}


bool EntryPoint::operator == (const EntryPoint& y) {
  int i = number_of_states;
  while (i-- > 0) {
    if (_entry[i] != y._entry[i]) return false;
  }
  return true;
}


//------------------------------------------------------------------------------------------------------------------------
// Implementation of DispatchTable

EntryPoint DispatchTable::entry(int i) const {
  assert(0 <= i && i < length, "index out of bounds");
  return
    EntryPoint(
      _table[btos][i],
      _table[ztos][i],
      _table[ctos][i],
      _table[stos][i],
      _table[atos][i],
      _table[itos][i],
      _table[ltos][i],
      _table[ftos][i],
      _table[dtos][i],
      _table[vtos][i]
    );
}


void DispatchTable::set_entry(int i, EntryPoint& entry) {
  assert(0 <= i && i < length, "index out of bounds");
  assert(number_of_states == 10, "check the code below");
  _table[btos][i] = entry.entry(btos);
  _table[ztos][i] = entry.entry(ztos);
  _table[ctos][i] = entry.entry(ctos);
  _table[stos][i] = entry.entry(stos);
  _table[atos][i] = entry.entry(atos);
  _table[itos][i] = entry.entry(itos);
  _table[ltos][i] = entry.entry(ltos);
  _table[ftos][i] = entry.entry(ftos);
  _table[dtos][i] = entry.entry(dtos);
  _table[vtos][i] = entry.entry(vtos);
}


bool DispatchTable::operator == (DispatchTable& y) {
  int i = length;
  while (i-- > 0) {
    EntryPoint t = y.entry(i); // for compiler compatibility (BugId 4150096)
    if (!(entry(i) == t)) return false;
  }
  return true;
}

address    TemplateInterpreter::_normal_remove_activation_entry                    = NULL;
address    TemplateInterpreter::_normal_remove_activation_preserving_args_entry    = NULL;

address    TemplateInterpreter::_normal_throw_ArrayIndexOutOfBoundsException_entry = NULL;
address    TemplateInterpreter::_normal_throw_ArrayStoreException_entry            = NULL;
address    TemplateInterpreter::_normal_throw_ArithmeticException_entry            = NULL;
address    TemplateInterpreter::_normal_throw_ClassCastException_entry             = NULL;
address    TemplateInterpreter::_normal_throw_NullPointerException_entry           = NULL;
address    TemplateInterpreter::_normal_throw_StackOverflowError_entry             = NULL;
address    TemplateInterpreter::_normal_throw_exception_entry                      = NULL;

#ifndef PRODUCT
EntryPoint TemplateInterpreter::_normal_trace_code;
#endif // !PRODUCT
EntryPoint TemplateInterpreter::_normal_return_entry[TemplateInterpreter::number_of_return_entries];
EntryPoint TemplateInterpreter::_normal_earlyret_entry;
EntryPoint TemplateInterpreter::_normal_deopt_entry [TemplateInterpreter::number_of_deopt_entries ];
address    TemplateInterpreter::_normal_deopt_reexecute_return_entry;
EntryPoint TemplateInterpreter::_normal_safept_entry;

address TemplateInterpreter::_normal_invoke_return_entry[TemplateInterpreter::number_of_return_addrs];
address TemplateInterpreter::_normal_invokeinterface_return_entry[TemplateInterpreter::number_of_return_addrs];
address TemplateInterpreter::_normal_invokedynamic_return_entry[TemplateInterpreter::number_of_return_addrs];

DispatchTable TemplateInterpreter::_normal_active_table;
DispatchTable TemplateInterpreter::_normal_normal_table;
DispatchTable TemplateInterpreter::_normal_safept_table;
address    TemplateInterpreter::_normal_wentry_point[DispatchTable::length];

address    TemplateInterpreter::_mirror_remove_activation_entry                    = NULL;
address    TemplateInterpreter::_mirror_remove_activation_preserving_args_entry    = NULL;

address    TemplateInterpreter::_mirror_throw_ArrayIndexOutOfBoundsException_entry = NULL;
address    TemplateInterpreter::_mirror_throw_ArrayStoreException_entry            = NULL;
address    TemplateInterpreter::_mirror_throw_ArithmeticException_entry            = NULL;
address    TemplateInterpreter::_mirror_throw_ClassCastException_entry             = NULL;
address    TemplateInterpreter::_mirror_throw_NullPointerException_entry           = NULL;
address    TemplateInterpreter::_mirror_throw_StackOverflowError_entry             = NULL;
address    TemplateInterpreter::_mirror_throw_exception_entry                      = NULL;

#ifndef PRODUCT
EntryPoint TemplateInterpreter::_mirror_trace_code;
#endif // !PRODUCT

EntryPoint TemplateInterpreter::_mirror_return_entry[TemplateInterpreter::number_of_return_entries];
EntryPoint TemplateInterpreter::_mirror_earlyret_entry;
EntryPoint TemplateInterpreter::_mirror_deopt_entry [TemplateInterpreter::number_of_deopt_entries ];
address    TemplateInterpreter::_mirror_deopt_reexecute_return_entry;
EntryPoint TemplateInterpreter::_mirror_safept_entry;

address TemplateInterpreter::_mirror_invoke_return_entry[TemplateInterpreter::number_of_return_addrs];
address TemplateInterpreter::_mirror_invokeinterface_return_entry[TemplateInterpreter::number_of_return_addrs];
address TemplateInterpreter::_mirror_invokedynamic_return_entry[TemplateInterpreter::number_of_return_addrs];

DispatchTable TemplateInterpreter::_mirror_active_table;
DispatchTable TemplateInterpreter::_mirror_normal_table;
DispatchTable TemplateInterpreter::_mirror_safept_table;
address    TemplateInterpreter::_mirror_wentry_point[DispatchTable::length];

address    TemplateInterpreter::_jportal_unimplemented_bytecode                     = NULL;
address    TemplateInterpreter::_jportal_illegal_bytecode_sequence                  = NULL;

address    TemplateInterpreter::_jportal_remove_activation_entry                    = NULL;
address    TemplateInterpreter::_jportal_remove_activation_preserving_args_entry    = NULL;


address    TemplateInterpreter::_jportal_throw_ArrayIndexOutOfBoundsException_entry = NULL;
address    TemplateInterpreter::_jportal_throw_ArrayStoreException_entry            = NULL;
address    TemplateInterpreter::_jportal_throw_ArithmeticException_entry            = NULL;
address    TemplateInterpreter::_jportal_throw_ClassCastException_entry             = NULL;
address    TemplateInterpreter::_jportal_throw_NullPointerException_entry           = NULL;
address    TemplateInterpreter::_jportal_throw_StackOverflowError_entry             = NULL;
address    TemplateInterpreter::_jportal_throw_exception_entry                      = NULL;

EntryPoint TemplateInterpreter::_jportal_return_entry[TemplateInterpreter::number_of_return_entries];
EntryPoint TemplateInterpreter::_jportal_deopt_entry [TemplateInterpreter::number_of_deopt_entries ];
address    TemplateInterpreter::_jportal_deopt_reexecute_return_entry;

address TemplateInterpreter::_jportal_invoke_return_entry[TemplateInterpreter::number_of_return_addrs];
address TemplateInterpreter::_jportal_invokeinterface_return_entry[TemplateInterpreter::number_of_return_addrs];
address TemplateInterpreter::_jportal_invokedynamic_return_entry[TemplateInterpreter::number_of_return_addrs];

DispatchTable TemplateInterpreter::_jportal_normal_table;
address    TemplateInterpreter::_jportal_wentry_point[DispatchTable::length];


//------------------------------------------------------------------------------------------------------------------------
// Entry points

/**
 * Returns the return entry table for the given invoke bytecode.
 */
address* TemplateInterpreter::invoke_return_entry_table_for(Bytecodes::Code code, bool jportal) {
  switch (code) {
  case Bytecodes::_invokestatic:
  case Bytecodes::_invokespecial:
  case Bytecodes::_invokevirtual:
  case Bytecodes::_invokehandle:
    return Interpreter::invoke_return_entry_table(jportal);
  case Bytecodes::_invokeinterface:
    return Interpreter::invokeinterface_return_entry_table(jportal);
  case Bytecodes::_invokedynamic:
    return Interpreter::invokedynamic_return_entry_table(jportal);
  default:
    fatal("invalid bytecode: %s", Bytecodes::name(code));
    return NULL;
  }
}

/**
 * Returns the return entry address for the given top-of-stack state and bytecode.
 */
address TemplateInterpreter::return_entry(TosState state, int length, Bytecodes::Code code, bool jportal) {
  guarantee(0 <= length && length < Interpreter::number_of_return_entries, "illegal length");
  const int index = TosState_as_index(state);
  switch (code) {
  case Bytecodes::_invokestatic:
  case Bytecodes::_invokespecial:
  case Bytecodes::_invokevirtual:
  case Bytecodes::_invokehandle:
    return jportal?_jportal_invoke_return_entry[index]:_normal_invoke_return_entry[index];
  case Bytecodes::_invokeinterface:
    return jportal?_jportal_invokeinterface_return_entry[index]:_normal_invokeinterface_return_entry[index];
  case Bytecodes::_invokedynamic:
    return jportal?_jportal_invokedynamic_return_entry[index]:_normal_invokedynamic_return_entry[index];
  default:
    assert(!Bytecodes::is_invoke(code), "invoke instructions should be handled separately: %s", Bytecodes::name(code));
    address entry = jportal?_jportal_return_entry[length].entry(state):_normal_return_entry[length].entry(state);
    vmassert(entry != NULL, "unsupported return entry requested, length=%d state=%d", length, index);
    return entry;
  }
}


address TemplateInterpreter::deopt_entry(TosState state, int length, bool jportal) {
  guarantee(0 <= length && length < Interpreter::number_of_deopt_entries, "illegal length");
  address entry = jportal?_jportal_deopt_entry[length].entry(state):_normal_deopt_entry[length].entry(state);
  vmassert(entry != NULL, "unsupported deopt entry requested, length=%d state=%d", length, TosState_as_index(state));
  return entry;
}

//------------------------------------------------------------------------------------------------------------------------
// Suport for invokes

int TemplateInterpreter::TosState_as_index(TosState state) {
  assert( state < number_of_states , "Invalid state in TosState_as_index");
  assert(0 <= (int)state && (int)state < TemplateInterpreter::number_of_return_addrs, "index out of bounds");
  return (int)state;
}


//------------------------------------------------------------------------------------------------------------------------
// Safepoint suppport

static inline void copy_table(address* from, address* to, int size) {
  // Copy non-overlapping tables. The copy has to occur word wise for MT safety.
  while (size-- > 0) *to++ = *from++;
}

void TemplateInterpreter::notice_safepoints() {
  if (!_notice_safepoints) {
    // switch to safepoint dispatch table
    _notice_safepoints = true;
    copy_table((address*)&_normal_safept_table, (address*)&_normal_active_table, sizeof(_normal_active_table) / sizeof(address));
    copy_table((address*)&_mirror_safept_table, (address*)&_mirror_active_table, sizeof(_mirror_active_table) / sizeof(address));
  }
}

// switch from the dispatch table which notices safepoints back to the
// normal dispatch table.  So that we can notice single stepping points,
// keep the safepoint dispatch table if we are single stepping in JVMTI.
// Note that the should_post_single_step test is exactly as fast as the
// JvmtiExport::_enabled test and covers both cases.
void TemplateInterpreter::ignore_safepoints() {
  if (_notice_safepoints) {
    if (!JvmtiExport::should_post_single_step()) {
      // switch to normal dispatch table
      _notice_safepoints = false;
      copy_table((address*)&_normal_normal_table, (address*)&_normal_active_table, sizeof(_normal_active_table) / sizeof(address));
      copy_table((address*)&_jportal_normal_table, (address*)&_mirror_active_table, sizeof(_mirror_active_table) / sizeof(address));
    }
  }
}

//------------------------------------------------------------------------------------------------------------------------
// Deoptimization support

// If deoptimization happens, this function returns the point of next bytecode to continue execution
address TemplateInterpreter::deopt_continue_after_entry(Method* method, address bcp, int callee_parameters, bool is_top_frame) {
  return AbstractInterpreter::deopt_continue_after_entry(method, bcp, callee_parameters, is_top_frame);
}

// If deoptimization happens, this function returns the point where the interpreter reexecutes
// the bytecode.
// Note: Bytecodes::_athrow (C1 only) and Bytecodes::_return are the special cases
//       that do not return "Interpreter::deopt_entry(vtos, 0)"
address TemplateInterpreter::deopt_reexecute_entry(Method* method, address bcp) {
  assert(method->contains(bcp), "just checkin'");
  Bytecodes::Code code   = Bytecodes::code_at(method, bcp);
  if (code == Bytecodes::_return_register_finalizer) {
    // This is used for deopt during registration of finalizers
    // during Object.<init>.  We simply need to resume execution at
    // the standard return vtos bytecode to pop the frame normally.
    // reexecuting the real bytecode would cause double registration
    // of the finalizable object.
    return Interpreter::deopt_reexecute_return_entry(JPortal && method->is_jportal());
  } else {
    return AbstractInterpreter::deopt_reexecute_entry(method, bcp);
  }
}

// If deoptimization happens, the interpreter should reexecute this bytecode.
// This function mainly helps the compilers to set up the reexecute bit.
bool TemplateInterpreter::bytecode_should_reexecute(Bytecodes::Code code) {
  if (code == Bytecodes::_return) {
    //Yes, we consider Bytecodes::_return as a special case of reexecution
    return true;
  } else {
    return AbstractInterpreter::bytecode_should_reexecute(code);
  }
}

InterpreterCodelet* TemplateInterpreter::codelet_containing(address pc) {
  InterpreterCodelet* ans = (InterpreterCodelet*)_normal_code->stub_containing(pc);
  if (ans == NULL && mirror_code) {
    ans = (InterpreterCodelet*)_mirror_code->stub_containing(pc);
  }
  if (ans == NULL && jportal_code) {
    ans = (InterpreterCodelet*)_jportal_code->stub_containing(pc);
  }
  return ans;
}

#endif // !CC_INTERP

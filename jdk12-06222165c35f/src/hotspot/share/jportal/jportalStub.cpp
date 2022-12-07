#include "precompiled.hpp"
#include "code/codeCache.hpp"
#include "code/compiledIC.hpp"
#include "gc/shared/collectedHeap.inline.hpp"
#include "interpreter/interpreter.hpp"
#include "jportal/jportalStub.hpp"
#include "jportal/jportalEnable.hpp"
#include "memory/resourceArea.hpp"
#include "runtime/thread.hpp"
#include "services/memoryService.hpp"

#ifdef JPORTAL_ENABLE
DEF_STUB_INTERFACE(JPortalStub);

StubQueue               *JPortalStubBuffer::_buffer              = NULL;
JPortalStub             *JPortalStubBuffer::_bci_table           = NULL;
JPortalStub             *JPortalStubBuffer::_switch_table        = NULL;

void JPortalStubBuffer_init() {
  if (JPortal) {
    JPortalStubBuffer::initialize();
  }
}

void JPortalStub::finalize() {
}

address JPortalStub::jump_destination() const {
  return JPortalStubBuffer::jportal_jump_buffer_entry_point(code_begin());
}

void JPortalStub::set_jump_stub(address dest_addr) {
  // Assemble new stub
  JPortalStubBuffer::assemble_jportal_jump_buffer_code(code_begin(), dest_addr);
  assert(jump_destination() == dest_addr,   "can recover destination");
}

void JPortalStub::set_ret_stub() {
  // Assemble new stub
  JPortalStubBuffer::assemble_jportal_ret_buffer_code(code_begin());
}

void JPortalStub::set_table_stub() {
  // Assemble new stub
  JPortalStubBuffer::assemble_jportal_table_buffer_code(code_begin());
}

#ifndef PRODUCT
// anybody calling to this stub will trap

void JPortalStub::verify() {
}

void JPortalStub::print() {
  tty->print_cr("JPortalStub");
}
#endif

void JPortalStubBuffer::initialize() {
  if (_buffer != NULL) return; // already initialized
  _buffer = new StubQueue(new JPortalStubInterface, JPortalNonNMethodCodeHeapSize, JPortalStubBuffer_lock, "InlineCacheBuffer", true);
  assert (_buffer != NULL, "cannot allocate JPortalStubBuffer");

  _bci_table = new_jportal_table_stub();
  _bci_table->set_table_stub();

  _switch_table = new_jportal_table_stub();
  _switch_table->set_table_stub();

  JPortalEnable::dump_bci_table_stub(_bci_table->code_begin(), JPortalTableStubLimit, jportal_table_stub_entry_size());

  JPortalEnable::dump_switch_table_stub(_switch_table->code_begin(), JPortalTableStubLimit, jportal_table_stub_entry_size());
}

bool JPortalStubBuffer::contains(address instruction_address) {
  return buffer()->contains(instruction_address);
}

JPortalStub* JPortalStubBuffer::new_jportal_jump_stub() {
  JPortalStub* stub = NULL;
  stub = (JPortalStub*)buffer()->request_committed(jportal_jump_stub_code_size());
  if (stub == NULL)
    vm_exit_out_of_memory(jportal_jump_stub_code_size(), INTERNAL_ERROR, "JPortalStubBuffer: out of memory.");
  return stub;
}

JPortalStub* JPortalStubBuffer::new_jportal_ret_stub() {
  JPortalStub* stub = NULL;
  stub = (JPortalStub*)buffer()->request_committed(jportal_ret_stub_code_size());
  if (stub == NULL)
    vm_exit_out_of_memory(jportal_ret_stub_code_size(), INTERNAL_ERROR, "JPortalStubBuffer: out of memory.");
  return stub;
}

JPortalStub* JPortalStubBuffer::new_jportal_table_stub() {
  JPortalStub* stub = NULL;
  stub = (JPortalStub*)buffer()->request_committed(jportal_table_stub_code_size());
  if (stub == NULL)
    vm_exit_out_of_memory(jportal_table_stub_code_size(), INTERNAL_ERROR, "JPortalStubBuffer: out of memory.");
  return stub;
}

bool JPortalStubBuffer::is_empty() {
  return buffer()->number_of_stubs() == 0;
}
#endif

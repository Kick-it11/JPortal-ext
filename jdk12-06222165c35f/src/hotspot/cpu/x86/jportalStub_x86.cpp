#include "precompiled.hpp"
#include "asm/macroAssembler.hpp"
#include "asm/macroAssembler.inline.hpp"
#include "jportal/jportalStub.hpp"
#include "memory/resourceArea.hpp"
#include "nativeInst_x86.hpp"

#ifdef JPORTAL_ENABLE
int JPortalStubBuffer::jportal_stub_code_size() {
  // Worst case, if destination is not a near call:
  // lea scratch, lit2
  // jmp scratch

  // Best case
  // jmp lit2

  int best = NativeMovConstReg::instruction_size + NativeJump::instruction_size;
  int worst = 2 * NativeMovConstReg::instruction_size + 3;
  return MAX2(best, worst);
}

int JPortalStubBuffer::jportal_stub_code_size(uint num) {
  return jportal_stub_code_size() * num;
}

void JPortalStubBuffer::assemble_jportal_buffer_code(address code_begin, address entry_point) {
  ResourceMark rm;
  CodeBuffer      code(code_begin, jportal_stub_code_size());
  MacroAssembler* masm            = new MacroAssembler(&code);
  masm->jump(ExternalAddress(entry_point));
}

void JPortalStubBuffer::assemble_jportal_buffer_code(address code_begin, address entry_point, uint num) {
  for (uint i = 0; i < num; ++i) {
    assemble_jportal_buffer_code(code_begin, entry_point);
    code_begin += jportal_stub_code_size();
  }
}

address JPortalStubBuffer::jportal_buffer_entry_point(address code_begin) {
  NativeInstruction* ni = nativeInstruction_at(code_begin);
  if (ni->is_jump()) {
    NativeJump*        jump = nativeJump_at(code_begin);
    return jump->jump_destination();
  } else {
    assert(ni->is_far_jump(), "unexpected instruction");
    NativeFarJump*     jump = nativeFarJump_at(code_begin);
    return jump->jump_destination();
  }
}

#endif

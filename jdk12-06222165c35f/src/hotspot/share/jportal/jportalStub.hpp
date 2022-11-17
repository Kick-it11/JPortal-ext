#ifndef SHARE_JPORTAL_JPORTAL_STUB_HPP
#define SHARE_JPORTAL_JPORTAL_STUB_HPP

#include "asm/codeBuffer.hpp"
#include "code/stubs.hpp"
#include "jportal/jportalEnable.hpp"
#include "memory/allocation.hpp"
#include "memory/heap.hpp"

#ifdef JPORTAL_ENABLE
class JPortalStub: public Stub {
 private:
  int                 _size;       // total size of the stub incl. code
  /* stub code follows here */
 protected:
  friend class JPortalStubInterface;
  // This will be called only by ICStubInterface
  void    initialize(int size,
                     CodeStrings strings)        { _size = size; /*_ic_site = NULL;*/ }
  void    finalize(); // called when a method is removed

  // General info
  int     size() const                           { return _size; }
  static  int code_size_to_size(int code_size)   { return align_up((int)sizeof(JPortalStub), CodeEntryAlignment) + code_size; }

 public:
  // Creation
  void set_stub(address dest_addr);

  void set_stub(address dest_addr, uint num);

  // Code info
  address code_begin() const                     { return (address)this + align_up(sizeof(JPortalStub), CodeEntryAlignment); }
  address code_end() const                       { return (address)this + size(); }

  // stub info
  address destination() const;  // destination of jump instruction

  // Debugging
  void    verify()            PRODUCT_RETURN;
  void    print()             PRODUCT_RETURN;

  // Creation
  friend JPortalStub* JPortalStub_from_destination_address(address destination_address);
};

// ICStub Creation
inline JPortalStub* JPortalStub_from_destination_address(address destination_address) {
  JPortalStub* stub = (JPortalStub*) (destination_address - align_up(sizeof(JPortalStub), CodeEntryAlignment));
  #ifdef ASSERT
  stub->verify();
  #endif
  return stub;
}

class JPortalStubBuffer : AllStatic {
  friend class JPortalStub;
 private:

  static StubQueue* _buffer;

  static StubQueue* buffer()                         { return _buffer; }

  static address jportal_buffer_entry_point  (address code_begin);

  // Machine-dependent implementation of JPortalStub
  static void    assemble_jportal_buffer_code(address code_begin, address entry_point);

  // Machine-dependent implementation of JPortalStub
  static void    assemble_jportal_buffer_code(address code_begin, address entry_point, uint num);

 public:

  static address low_bound()                              { return _buffer->code_start(); }
  static address high_bound()                             { return _buffer->code_end();   }

  // Initialization; must be called before first usage
  static void initialize();

  // Access
  static bool contains(address instruction_address);

  // for debugging
  static bool is_empty();

  // New interface
  static JPortalStub *new_jportal_stub();

  static JPortalStub *new_jportal_stub(uint num);

  static int jportal_stub_code_size();

  static int jportal_stub_code_size(uint num);
};
#endif

#endif

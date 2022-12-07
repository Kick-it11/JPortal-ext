#ifndef SHARE_JPORTAL_JPORTAL_STUB_HPP
#define SHARE_JPORTAL_JPORTAL_STUB_HPP

#include "asm/codeBuffer.hpp"
#include "code/stubs.hpp"
#include "jportal/jportalEnable.hpp"
#include "memory/allocation.hpp"
#include "memory/heap.hpp"

#ifdef JPORTAL_ENABLE

/* JPortal Jump Stub, Every stub refers to a jump destination */
class JPortalStub: public Stub {
 private:
  int                 _size;       // total size of the stub incl. code
  /* stub code follows here */
 protected:
  friend class JPortalStubInterface;
  // This will be called only by JPortalStubInterface
  void    initialize(int size,
                     CodeStrings strings)        { _size = size; /*_ic_site = NULL;*/ }
  void    finalize(); // called when a method is removed

  // General info
  int     size() const                           { return _size; }
  static  int code_size_to_size(int code_size)   { return align_up((int)sizeof(JPortalStub), CodeEntryAlignment) + code_size; }

 public:
  void set_jump_stub(address dest_addr);

  void set_ret_stub();

  void set_table_stub();

  address jump_destination() const;  // destination of jump instruction

  // Code info
  address code_begin() const                     { return (address)this + align_up(sizeof(JPortalStub), CodeEntryAlignment); }
  address code_end() const                       { return (address)this + size(); }

  // Debugging
  void    verify()            PRODUCT_RETURN;
  void    print()             PRODUCT_RETURN;
};

class JPortalStubBuffer : AllStatic {
  friend class JPortalStub;
 private:

  static StubQueue           *_buffer;
  static JPortalStub         *_bci_table;
  static JPortalStub         *_switch_table;

  static StubQueue *buffer()                         { return _buffer; }

  static address jportal_jump_buffer_entry_point  (address code_begin);

  // Machine-dependent implementation of JPortalStub
  static void    assemble_jportal_jump_buffer_code(address code_begin, address entry_point);
  static void    assemble_jportal_ret_buffer_code(address code_begin);
  static void    assemble_jportal_table_buffer_code(address code_begin);

 public:

  static address low_bound()                              { return _buffer->code_start(); }
  static address high_bound()                             { return _buffer->code_end();   }

  // Initialization; must be called before first usage
  static void initialize();

  // Access
  static bool contains(address instruction_address);

  // for debugging
  static bool is_empty();

  static JPortalStub *new_jportal_jump_stub();

  static JPortalStub *new_jportal_ret_stub();

  static JPortalStub *new_jportal_table_stub();

  static int jportal_jump_stub_code_size();

  static int jportal_ret_stub_code_size();

  static int jportal_table_stub_code_size();

  static int jportal_table_stub_entry_size();

  static JPortalStub *bci_table() { return _bci_table; }

  static JPortalStub *switch_table() { return _switch_table; }
};
#endif

#endif

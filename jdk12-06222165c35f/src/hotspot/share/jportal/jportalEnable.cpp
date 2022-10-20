
#include "jportal/jportalEnable.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/globals_extension.hpp"
#include "runtime/thread.hpp"
#include "classfile/javaClasses.hpp"
#include "code/codeCache.hpp"

// currently JPortal is supported only on linux & _x86_64
#if defined(__linux__) && defined(__x86_64__)
#define JPORTAL_ENABLE 1
#endif

#ifdef JPORTAL_ENABLE
#include <sys/shm.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#endif

bool                       JPortalEnable::_initialized     = false;
int                        JPortalEnable::_shm_id          = -1;
address                    JPortalEnable::_shm_addr        = NULL;
GrowableArray<Method *>*   JPortalEnable::_method_array    = NULL;

inline u4 JPortalEnable::get_java_tid(JavaThread* thread) {
  oop obj = ((JavaThread*)thread)->threadObj();
  return (obj == NULL) ? 0 : java_lang_Thread::thread_id(obj);
}

inline void JPortalEnable::dump_data(address src, u8 size) {
  assert(JPortalEnable_lock->owned_by_self(), "JPortalEnable error: Own lock while copying data");

  u8 data_size = 0;
  ShmHeader *header = (ShmHeader*)_shm_addr;
  u8 data_volume = header->data_size;
  u8 data_head, data_tail;
  address data_begin = _shm_addr + sizeof(ShmHeader);

  for (;;) {
    data_head = header->data_head;
    data_tail = header->data_tail;
    if (data_head < data_tail) data_size = data_tail - data_head;
    else data_size = data_volume - (data_head - data_tail);
    if (data_size > size)
      break;
  }

  address dest = data_begin + data_head;
  if (size != 0) {
    u8 remaining = data_volume - data_head;
    if (remaining < size) {
      memcpy(dest, src, remaining);
      memcpy(data_begin, src + remaining, size - remaining);
      header->data_head = size - remaining;
    } else {
      memcpy(dest, src, size);
      header->data_head = data_head + size;
    }
  }
}

void JPortalEnable::dump_codelets() {
#ifdef CC_INTERP
  fprintf(stderr, "JPortalEnable error: cpp interpreter not supported.\n");
#else
  MutexLockerEx mu(JPortalEnable_lock, Mutex::_no_safepoint_check_flag);

  if (!_initialized) {
    fprintf(stderr, "JPortalEnable error: codelets before initialize.\n");
    return;
  }

  // don't allow shared memory buffer to be full
  u4 size = sizeof(CodeletsInfo);

  CodeletsInfo codelet(sizeof(CodeletsInfo));
  codelet.low_bound = AbstractInterpreter::jportal_code()?(u8)TemplateInterpreter::jportal_code()->code_start():0;
  codelet.high_bound = AbstractInterpreter::jportal_code()?(u8)TemplateInterpreter::jportal_code()->code_end():0;
  codelet.slow_signature_handler = (u8)TemplateInterpreter::_jportal_slow_signature_handler;
  codelet.unimplemented_bytecode_entry = (u8)TemplateInterpreter::_jportal_unimplemented_bytecode;
  codelet.illegal_bytecode_sequence_entry = (u8)TemplateInterpreter::_jportal_illegal_bytecode_sequence;
  for (int i = 0; i < TemplateInterpreter::number_of_return_entries; i++)
    for (int j = 0; j < number_of_states; j++)
      codelet.return_entry[i][j] = (u8)TemplateInterpreter::_jportal_return_entry[i].entry((TosState)j);
  for (int i = 0; i < TemplateInterpreter::number_of_return_addrs; i++) {
    codelet.invoke_return_entry[i] = (u8)TemplateInterpreter::_jportal_invoke_return_entry[i];
    codelet.invokeinterface_return_entry[i] = (u8)TemplateInterpreter::_jportal_invokeinterface_return_entry[i];
    codelet.invokedynamic_return_entry[i] = (u8)TemplateInterpreter::_jportal_invokedynamic_return_entry[i];
  }
  for (int i = 0; i < number_of_states; ++i)
    codelet.earlyret_entry[i] = (u8)TemplateInterpreter::_jportal_earlyret_entry.entry((TosState)i);
  for (int i = 0; i < TemplateInterpreter::number_of_result_handlers; i++)
    codelet.native_abi_to_tosca[i] = (u8)TemplateInterpreter::_jportal_native_abi_to_tosca[i];
  codelet.rethrow_exception_entry = (u8)TemplateInterpreter::_jportal_rethrow_exception_entry;
  codelet.throw_exception_entry = (u8)TemplateInterpreter::_jportal_throw_exception_entry;
  codelet.remove_activation_preserving_args_entry = (u8)TemplateInterpreter::_jportal_remove_activation_preserving_args_entry;
  codelet.remove_activation_entry = (u8)TemplateInterpreter::_jportal_remove_activation_entry;
  codelet.throw_ArrayIndexOutOfBoundsException_entry = (u8)TemplateInterpreter::_jportal_throw_ArrayIndexOutOfBoundsException_entry;
  codelet.throw_ArrayStoreException_entry = (u8)TemplateInterpreter::_jportal_throw_ArrayStoreException_entry;
  codelet.throw_ArithmeticException_entry = (u8)TemplateInterpreter::_jportal_throw_ArithmeticException_entry;
  codelet.throw_ClassCastException_entry = (u8)TemplateInterpreter::_jportal_throw_ClassCastException_entry;
  codelet.throw_NullPointerException_entry = (u8)TemplateInterpreter::_jportal_throw_NullPointerException_entry;
  codelet.throw_StackOverflowError_entry = (u8)TemplateInterpreter::_jportal_throw_StackOverflowError_entry;
  for (int i = 0; i < TemplateInterpreter::number_of_method_entries; i++)
    codelet.entry_table[i] = (u8)(TemplateInterpreter::_jportal_entry_table[i]);
  for (int i = 0; i < DispatchTable::length; i++) {
    for (int j = 0; j < number_of_states; j++)
      codelet.normal_table[i][j] = (u8)TemplateInterpreter::_jportal_normal_table.entry(i).entry((TosState)j);
    codelet.wentry_point[i] = (u8)TemplateInterpreter::_jportal_wentry_point[i];
  }
  for (int i = 0; i < TemplateInterpreter::number_of_deopt_entries; i++)
    for (int j = 0; j < number_of_states; j++)
      codelet.deopt_entry[i][j] = (u8)(TemplateInterpreter::_jportal_deopt_entry[i].entry((TosState)j));
  codelet.deopt_reexecute_return_entry = (u8)TemplateInterpreter::_jportal_deopt_reexecute_return_entry;

  dump_data((address)&codelet, size);
#endif
}

void JPortalEnable::dump_method_entry(JavaThread *thread, Method *moop) {
  MutexLockerEx mu(JPortalEnable_lock, Mutex::_no_safepoint_check_flag);

  if (!_initialized) {
    fprintf(stderr, "JPortalEnable error: method entry before initialize.\n");
    return;
  }

  if (!thread || !moop) {
    fprintf(stderr, "JPortalEnable error: empty method entry.\n");
    return;
  }

  u4 size = 0;
  int idx = _method_array->find(moop);
  if (idx == -1) {
    int klass_name_length = moop->klass_name()->utf8_length();
    int name_length = moop->name()->utf8_length();
    int sig_length = moop->signature()->utf8_length();
    char *klass_name = (char *)moop->klass_name()->bytes();
    char *method_name = (char *)moop->name()->bytes();
    char *method_signature = (char *)moop->signature()->bytes();

    size = sizeof(MethodEntryInitial) + klass_name_length + name_length + sig_length;
    MethodEntryInitial me(idx, klass_name_length, name_length, sig_length, get_java_tid(thread), size);

    if (JPortalShmVolume - sizeof(ShmHeader) <= size) {
      fprintf(stderr, "JPortalEnable error: ignore entry for size too big.\n");
      return;
    }

    dump_data((address)&me, sizeof(me));
    dump_data((address)klass_name, klass_name_length);
    dump_data((address)method_name, name_length);
    dump_data((address)method_signature, sig_length);
    return;
  }

  size = sizeof(MethodEntryInfo);
  MethodEntryInfo me(idx, get_java_tid(thread), size);
  dump_data((address)&me, size);
}

void JPortalEnable::dump_compiled_method_load(Method *moop, nmethod *nm) {
  MutexLockerEx mu(JPortalEnable_lock, Mutex::_no_safepoint_check_flag);

  if (!_initialized) {
    fprintf(stderr, "JPortalEnable error: method load before initialize.\n");
    return;
  }

  if (!nm || !moop) {
    fprintf(stderr, "JPortalEnable error: empty method load.\n");
    return;
  }

  address code_begin = nm->insts_begin();
  u4 code_size = nm->insts_size() + nm->stub_size();
  address scopes_pc_begin = (address)nm->scopes_pcs_begin();
  u4 scopes_pc_size = nm->scopes_pcs_size();
  address scopes_data_begin = nm->scopes_data_begin();
  u4 scopes_data_size = nm->scopes_data_size();
  Metadata **metadata_begin = nm->metadata_begin();
  int metadata_cnt = nm->metadata_count();
  address entry_point = nm->entry_point();
  address verified_entry_point = nm->verified_entry_point();
  address osr_entry_point = nm->is_osr_method()?nm->osr_entry():entry_point;
             
  u4 size = sizeof(CompiledMethodLoadInfo) + code_size + scopes_pc_size + scopes_data_size;
  int inline_method_cnt = 0;
  for (int index = 0; index < metadata_cnt; index++) {
    Method *m = (Method*)metadata_begin[index];
    if (m == Universe::non_oop_word() || m == NULL ||
        !m->is_metaspace_object() || !m->is_method())
      continue;
    
    inline_method_cnt++;
    size += (sizeof(InlineMethodInfo) + m->klass_name()->utf8_length() + m->name()->utf8_length() + m->signature()->utf8_length());
  }
  if (inline_method_cnt == 0) {
    size += (sizeof(InlineMethodInfo) + moop->klass_name()->utf8_length() + moop->name()->utf8_length() + moop->signature()->utf8_length());
  }

  CompiledMethodLoadInfo cm((u8)code_begin, (u8)entry_point, (u8)verified_entry_point, (u8)osr_entry_point,
                            code_size,  scopes_pc_size, scopes_data_size,
                            inline_method_cnt ? inline_method_cnt : 1, size);

  if (JPortalShmVolume - sizeof(ShmHeader) <= size) {
    fprintf(stderr, "JPortalEnable error: ignore compiled code for size too big.\n");
    return;
  }

  dump_data((address)&cm, sizeof(cm));

  if (inline_method_cnt == 0) {
    InlineMethodInfo mi(moop->klass_name()->utf8_length(), moop->name()->utf8_length(),
                        moop->signature()->utf8_length(), 1);
    dump_data((address)&mi, sizeof(mi));
    dump_data((address)moop->klass_name()->bytes(), moop->klass_name()->utf8_length());
    dump_data((address)moop->name()->bytes(), moop->name()->utf8_length());
    dump_data((address)moop->signature()->bytes(), moop->signature()->utf8_length());
    inline_method_cnt = 1;
  } else {
    for (int index = 0; index < metadata_cnt; index++) {
      Method *m = (Method*)metadata_begin[index];
      if (m == Universe::non_oop_word() || m == NULL || !m->is_metaspace_object() || !m->is_method())
        continue;

      InlineMethodInfo mi(m->klass_name()->utf8_length(), m->name()->utf8_length(),
                          m->signature()->utf8_length(), index+1);
      dump_data((address)&mi, sizeof(mi));
      dump_data((address)m->klass_name()->bytes(), m->klass_name()->utf8_length());
      dump_data((address)m->name()->bytes(), m->name()->utf8_length());
      dump_data((address)m->signature()->bytes(), m->signature()->utf8_length());
    }
  }
  dump_data(code_begin, code_size);
  dump_data(scopes_pc_begin, scopes_pc_size);
  dump_data(scopes_data_begin, scopes_data_size);
}

void JPortalEnable::dump_compiled_method_unload(Method *moop, nmethod *nm) {
  MutexLockerEx mu(JPortalEnable_lock, Mutex::_no_safepoint_check_flag);

  if (!_initialized) {
    fprintf(stderr, "JPortalEnable error: method unload before initialize.\n");
    return;
  }

  if (!nm || !moop) {
    fprintf(stderr, "JPortalEnable error: empty method unload.\n");
    return;
  }

  address code_begin = nm->insts_begin();

  u4 size = sizeof(CompiledMethodUnloadInfo);
  CompiledMethodUnloadInfo cmu((u8)code_begin, size);

  dump_data((address)&cmu, size);
}

void JPortalEnable::dump_thread_start(JavaThread *thread) {
  MutexLockerEx mu(JPortalEnable_lock, Mutex::_no_safepoint_check_flag);

  if (!thread) {
    fprintf(stderr, "JPortalEnable error: empty thread start.\n");
    return;
  }

  if (!_initialized) {
    // thread before initialized can be simply ignored
    return;
  }

  u4 size = sizeof(ThreadStartInfo);
  ThreadStartInfo ts(get_java_tid(thread), syscall(SYS_gettid), size);

  dump_data((address)&ts, size);
}

void JPortalEnable::dump_inline_cache_add(address src, address dest) {
  MutexLockerEx mu(JPortalEnable_lock, Mutex::_no_safepoint_check_flag);

  if (!_initialized) {
    fprintf(stderr, "JPortalEnable error: inline cache add before initialize.\n");
    return;
  }

  u4 size = sizeof(InlineCacheAdd);
  InlineCacheAdd ic((u8)src, (u8)dest, size);

  dump_data((address)&ic, size);
}

void JPortalEnable::dump_inline_cache_clear(address src) {
  MutexLockerEx mu(JPortalEnable_lock, Mutex::_no_safepoint_check_flag);

  if (!_initialized) {
    fprintf(stderr, "JPortalEnable error: inline cache clear before initialize.\n");
    return;
  }

  u8 size = sizeof(InlineCacheClear);
  InlineCacheClear ic((u8)src, size);

  dump_data((address)&ic, size);
}

void JPortalEnable::destroy() {
  MutexLockerEx mu(JPortalEnable_lock, Mutex::_no_safepoint_check_flag);

  if (!_initialized)
    return;

  delete _method_array;
  _method_array = NULL;

  shmdt(_shm_addr);

  // let subprocess delete shared memory
  _shm_addr = NULL;
  _shm_id = -1;

  _initialized = false;
}

void JPortalEnable::init() {
#ifdef JPORTAL_ENABLE
  {
    MutexLockerEx mu(JPortalEnable_lock, Mutex::_no_safepoint_check_flag);

    // initialized
    if (_initialized)
      return;

    // open pipe
    int pipe_fd[2];
    if (pipe(pipe_fd) < 0) {
      fprintf(stderr, "JPortal error: Failt to open trace pipe\n");
      FLAG_SET_ERGO(bool, JPortal, false);
      return;
    }

    // open shared memory
    if (JPortalShmVolume <= sizeof(CodeletsInfo)) {
      fprintf(stderr, "JPortal error: JPortalShmVolume too small\n");
      FLAG_SET_ERGO(bool, JPortal, false);
      return;
    }
    _shm_id = shmget(IPC_PRIVATE, JPortalShmVolume, IPC_CREAT|0600);
    if (_shm_id < 0) {
      fprintf(stderr, "JPortal error: Fail to get shared memory.\n");
      FLAG_SET_ERGO(bool, JPortal, false);
      return;
    }
    // initialize
    _shm_addr = (address)shmat(_shm_id, NULL, 0);

    struct ShmHeader *header = (ShmHeader *)_shm_addr;
    header->data_head = 0;
    header->data_tail = 0;
    header->data_size = JPortalShmVolume - sizeof(ShmHeader);

    // write JPortalTrace arguments
    char java_pid[20], write_pipe[20], _low_bound[20], _high_bound[20];
    char mmap_pages[20], aux_pages[20], shmid[20];
    if (sprintf(java_pid, "%ld", syscall(SYS_gettid)) < 0 ||
        sprintf(write_pipe, "%d", pipe_fd[1]) < 0 ||
        sprintf(_low_bound, "%p", CodeCache::low_bound(true)) < 0 ||
        sprintf(_high_bound, "%p", CodeCache::high_bound(true)) < 0 ||
        sprintf(mmap_pages, "%lu", JPortalMMAPPages) < 0 ||
        sprintf(aux_pages, "%lu", JPortalAUXPages) < 0 || 
        sprintf(shmid, "%d", _shm_id) < 0) {
      fprintf(stderr, "JPortal error: Fail to write JPortalTrace Arguments\n");

      close(pipe_fd[0]);
      close(pipe_fd[1]);

      shmdt(_shm_addr);
      shmctl(_shm_id, IPC_RMID, NULL);
      _shm_id = -1;
      _shm_addr = NULL;
      FLAG_SET_ERGO(bool, JPortal, false);

      return;
    }

    // load JPortalTrace program to begin tracing
    pid_t pid = fork();
    if (pid == 0) {
      // sub process
      prctl(PR_SET_PDEATHSIG, SIGTERM);
      close(pipe_fd[0]);

      execl("/home/jake/codes/JPortal-ext/build/trace/JPortalTrace",
            "./JPortalTrace", java_pid, write_pipe, _low_bound, _high_bound,
            mmap_pages, aux_pages, shmid, NULL);      

      // fail to load subprocess
      fprintf(stderr, "JPortal error: Fail to load JPortalTrace process\n");

      close(pipe_fd[1]);
      exit(-1);
    } else if (pid < 0) {
      // fail to fork process
      fprintf(stderr, "JPortal error: Fail to fork new process\n");

      close(pipe_fd[0]);
      close(pipe_fd[1]);

      shmdt(_shm_addr);
      shmctl(_shm_id, IPC_RMID, NULL);
      _shm_id = -1;
      _shm_addr = NULL;

      FLAG_SET_ERGO(bool, JPortal, false);
      return;
    }

    // Succeed to load JPortalTrace and this is in JVM process
    char bf;
    close(pipe_fd[1]);
    if (read(pipe_fd[0], &bf, 1) != 1) {
      fprintf(stderr, "JPortal error: Fail to read pipe from child process.\n");
      close(pipe_fd[0]);
      shmctl(_shm_id, IPC_RMID, NULL);
      shmctl(_shm_id, IPC_RMID, NULL);
      _shm_id = -1;
      _shm_addr = NULL;
      FLAG_SET_ERGO(bool, JPortal, false);
      return;
    }
    close(pipe_fd[0]);

    _method_array = new(ResourceObj::C_HEAP, mtInternal) GrowableArray<Method*> (10, true);

    // Succeed
    _initialized = true;
  }

  dump_codelets();

  dump_thread_start(JavaThread::current());

#else
  if (JPortal) {
    fprintf(stderr, "JPortalEnable: not supported\n");
    FLAG_SET_ERGO(bool, JPortal, false);
  }
#endif
}

// #endif

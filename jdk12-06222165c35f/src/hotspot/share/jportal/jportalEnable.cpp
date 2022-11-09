#include "jportal/jportalEnable.hpp"
#include "runtime/mutexLocker.hpp"
#include "runtime/globals_extension.hpp"
#include "runtime/thread.hpp"
#include "classfile/javaClasses.hpp"
#include "code/codeCache.hpp"

#ifdef JPORTAL_ENABLE
#include <sys/shm.h>
#include <sys/prctl.h>
#include <sys/syscall.h>

bool                       JPortalEnable::_initialized     = false;
bool                       JPortalEnable::_tracing         = false;
int                        JPortalEnable::_shm_id          = -1;
address                    JPortalEnable::_shm_addr        = NULL;

void JPortalEnable_init() {
  JPortalEnable::init();
}

void JPortalEnable_exit() {
  JPortalEnable::destroy();
}

inline u4 JPortalEnable::get_java_tid(JavaThread* thread) {
  oop obj = ((JavaThread*)thread)->threadObj();
  return (obj == NULL) ? 0 : java_lang_Thread::thread_id(obj);
}

inline bool JPortalEnable::check_data(u4 size) {
  assert(JPortalEnable_lock->owned_by_self(), "JPortalEnable error: Own lock while copying data");

  u8 data_head, data_tail, data_size, data_volume;
  ShmHeader *header = (ShmHeader*)_shm_addr;

  // might not be atomic for non-64bit platform
  data_volume = header->data_size;
  data_head = header->data_head;
  data_tail = header->data_tail;

  if (data_head < data_tail)
    return (data_tail - data_head) > size;
  else
    return (data_volume - (data_head - data_tail)) > size;
}

inline void JPortalEnable::dump_data(address src, u4 size) {
  assert(JPortalEnable_lock->owned_by_self(), "JPortalEnable error: Own lock while copying data");

  u8 data_size = 0;
  ShmHeader *header = (ShmHeader*)_shm_addr;
  u8 data_volume = header->data_size;
  u8 data_head, data_tail;
  address data_begin = _shm_addr + sizeof(ShmHeader);

  // might not be atomic for non-64bit platform
  data_head = header->data_head;
  OrderAccess::fence();

  address dest = data_begin + data_head;
  if (size != 0) {
    u8 remaining = data_volume - data_head;
    if (remaining < size) {
      memcpy(dest, src, remaining);
      memcpy(data_begin, src + remaining, size - remaining);
      OrderAccess::fence();
      Atomic::store(size-remaining, &header->data_head);
    } else {
      memcpy(dest, src, size);
      OrderAccess::fence();
      Atomic::store(data_head+size, &header->data_head);
    }
  }
}

void JPortalEnable::dump_method_entry(Method *moop) {
  MutexLockerEx mu(JPortalEnable_lock, Mutex::_no_safepoint_check_flag);

  if (!_initialized) {
    warning("JPortalEnable error: method load before initialize");
    return;
  }

  if (!moop) {
    warning("JPortalEnable error: empty method load");
    return;
  }

  u4 size = 0;
  int klass_name_length = moop->klass_name()->utf8_length();
  int name_length = moop->name()->utf8_length();
  int sig_length = moop->signature()->utf8_length();
  char *klass_name = (char *)moop->klass_name()->bytes();
  char *method_name = (char *)moop->name()->bytes();
  char *method_signature = (char *)moop->signature()->bytes();

  size = sizeof(MethodEntryInfo) + klass_name_length + name_length + sig_length;
  MethodEntryInfo me(klass_name_length, name_length, sig_length, (u8)moop->interpreter_entry(), size);

  if (!check_data(size)) {
    warning("JPortalEnable error: ignore entry for size too big");
    return;
  }

  dump_data((address)&me, sizeof(me));
  dump_data((address)klass_name, klass_name_length);
  dump_data((address)method_name, name_length);
  dump_data((address)method_signature, sig_length);
  return;
}

void JPortalEnable::dump_branch_taken(address addr) {
  MutexLockerEx mu(JPortalEnable_lock, Mutex::_no_safepoint_check_flag);

  if (!_initialized) {
    warning("JPortalEnable error: dump branch taken before initialize");
    return;
  }

  u4 size = sizeof(struct BranchTakenInfo);
  BranchTakenInfo bti((u8)addr, size);

  if (!check_data(size)) {
    warning("JPortalEnable error: ignore branch taken for size too big");
    return;
  }

  dump_data((address)&bti, sizeof(bti));
  return;
}

void JPortalEnable::dump_branch_not_taken(address addr) {
  MutexLockerEx mu(JPortalEnable_lock, Mutex::_no_safepoint_check_flag);

  if (!_initialized) {
    warning("JPortalEnable error: dump branch not taken before initialize");
    return;
  }

  u4 size = sizeof(struct BranchNotTakenInfo);
  BranchTakenInfo bnti((u8)addr, size);

  if (!check_data(size)) {
    warning("JPortalEnable error: ignore branch not taken for size too big");
    return;
  }

  dump_data((address)&bnti, sizeof(bnti));
  return;
}

void JPortalEnable::dump_exception_handling(JavaThread *thread, Method *moop, int current_bci, int handler_bci) {
  MutexLockerEx mu(JPortalEnable_lock, Mutex::_no_safepoint_check_flag);

  if (!_initialized) {
    warning("JPortalEnable error: dump exception before initialize");
    return;
  }

  if (!thread || !moop) {
    warning("JPortalEnable error: empty exception");
    return;
  }

  u4 size = sizeof(struct ExceptionHandlingInfo);
  ExceptionHandlingInfo ehi(current_bci, handler_bci, get_java_tid(thread), (u8)moop->interpreter_entry(), size);

  if (!check_data(size)) {
    warning("JPortalEnable error: ignore exception for size too big");
    return;
  }

  dump_data((address)&ehi, sizeof(ehi));
  return;
}

void JPortalEnable::dump_deoptimization(JavaThread *thread, Method *moop, int bci) {
  MutexLockerEx mu(JPortalEnable_lock, Mutex::_no_safepoint_check_flag);

  if (!_initialized) {
    warning("JPortalEnable error: dump exception before initialize");
    return;
  }

  if (!thread || !moop) {
    warning("JPortalEnable error: empty exception");
    return;
  }

  u4 size = sizeof(struct DeoptimizationInfo);
  DeoptimizationInfo di(get_java_tid(thread), bci, (u8)moop->interpreter_entry(), size);

  if (!check_data(size)) {
    warning("JPortalEnable error: ignore branch not taken for size too big");
    return;
  }

  dump_data((address)&di, sizeof(di));
  return;
}

void JPortalEnable::dump_compiled_method_load(Method *moop, nmethod *nm) {
  MutexLockerEx mu(JPortalEnable_lock, Mutex::_no_safepoint_check_flag);

  if (!_initialized) {
    warning("JPortalEnable error: method load before initialize");
    return;
  }

  if (!nm || !moop) {
    warning("JPortalEnable error: empty method load");
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
                            inline_method_cnt ? inline_method_cnt : 1, code_size,
                            scopes_pc_size, scopes_data_size, size);

  if (!check_data(size)) {
    warning("JPortalEnable error: ignore compiled code load for size too big");
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
    warning("JPortalEnable error: method unload before initialize");
    return;
  }

  if (!nm || !moop) {
    warning("JPortalEnable error: empty method unload.");
    return;
  }

  address code_begin = nm->insts_begin();

  u4 size = sizeof(CompiledMethodUnloadInfo);
  CompiledMethodUnloadInfo cmu((u8)code_begin, size);

  if (!check_data(size)) {
    warning("JPortalEnable error: ignore compiled code unload for size too big");
    return;
  }

  dump_data((address)&cmu, size);
}

void JPortalEnable::dump_thread_start(JavaThread *thread) {
  MutexLockerEx mu(JPortalEnable_lock, Mutex::_no_safepoint_check_flag);

  if (!_initialized) {
    warning("JPortalEnable error: thread start before initialize");
    return;
  }

  if (!thread) {
    warning("JPortalEnable error: empty thread start");
    return;
  }

  u4 size = sizeof(ThreadStartInfo);
  ThreadStartInfo ts(get_java_tid(thread), syscall(SYS_gettid), size);

  if (!check_data(size)) {
    warning("JPortalEnable error: ignore thread start for size too big");
    return;
  }

  dump_data((address)&ts, size);
}

void JPortalEnable::dump_inline_cache_add(address src, address dest) {
  MutexLockerEx mu(JPortalEnable_lock, Mutex::_no_safepoint_check_flag);

  if (!_initialized) {
    warning("JPortalEnable error: inline cache add before initialize");
    return;
  }

  u4 size = sizeof(InlineCacheAdd);
  InlineCacheAdd ic((u8)src, (u8)dest, size);

  if (!check_data(size)) {
    warning("JPortalEnable error: ignore inline cache add for size too big");
    return;
  }

  dump_data((address)&ic, size);
}

void JPortalEnable::dump_inline_cache_clear(address src) {
  MutexLockerEx mu(JPortalEnable_lock, Mutex::_no_safepoint_check_flag);

  if (!_initialized) {
    warning("JPortalEnable error: inline cache clear before initialize");
    return;
  }

  u8 size = sizeof(InlineCacheClear);
  InlineCacheClear ic((u8)src, size);

  if (!check_data(size)) {
    warning("JPortalEnable error: ignore inline cache clear for size too big");
    return;
  }

  dump_data((address)&ic, size);
}

void JPortalEnable::trace() {
  MutexLockerEx mu(JPortalEnable_lock, Mutex::_no_safepoint_check_flag);

  if (!JPortal || !_initialized || _tracing)
    return;

  // open pipe
  int pipe_fd[2];
  if (pipe(pipe_fd) < 0) {
    warning("JPortalEnable Error: Fail to open pipe, abort");
    vm_exit(1);
  }

  // write JPortalTrace arguments
  char java_pid[20], write_pipe[20], _low_bound[20], _high_bound[20];
  char mmap_pages[20], aux_pages[20], shmid[20];
  if (sprintf(java_pid, "%ld", syscall(SYS_gettid)) < 0 ||
      sprintf(write_pipe, "%d", pipe_fd[1]) < 0 ||
      sprintf(_low_bound, "%p", CodeCache::low_bound(true)) < 0 ||
      sprintf(_high_bound, "%p", CodeCache::high_bound(true)) < 0 ||
      sprintf(mmap_pages, "%lu", JPortalMMAPPages) < 0 ||
      sprintf(aux_pages, "%lu", JPortalAUXPages) < 0 || 
        sprintf(aux_pages, "%lu", JPortalAUXPages) < 0 || 
      sprintf(aux_pages, "%lu", JPortalAUXPages) < 0 || 
      sprintf(shmid, "%d", _shm_id) < 0) {
    close(pipe_fd[0]);
    close(pipe_fd[1]);

    warning("JPortalEnable Error: Fail to write arguments, abort");
    vm_exit(1);
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
    warning("JPortal error: Fail to load JPortalTrace process");

    close(pipe_fd[1]);
    exit(1);
  } else if (pid < 0) {
    // fail to fork process
    close(pipe_fd[0]);
    close(pipe_fd[1]);

    warning("JPortalEnable Error: Fail to fork process, abort");
    vm_exit(1);
  }

  // Succeed to load JPortalTrace and this is in JVM process
  char bf;
  close(pipe_fd[1]);
  if (read(pipe_fd[0], &bf, 1) != 1) {
    close(pipe_fd[0]);

    warning("JPortalEnable Error: Fail to read pipe, abort");
    vm_exit(1);
  }
  close(pipe_fd[0]);
  _tracing = true;
}

void JPortalEnable::destroy() {
  MutexLockerEx mu(JPortalEnable_lock, Mutex::_no_safepoint_check_flag);

  if (!_initialized)
    return;

  shmdt(_shm_addr);
  if (!_tracing)
    shmctl(_shm_id, IPC_RMID, NULL);

  // let subprocess delete shared memory
  _shm_addr = NULL;
  _shm_id = -1;

  _initialized = false;
  _tracing = false;
}

void JPortalEnable::init() {
  MutexLockerEx mu(JPortalEnable_lock, Mutex::_no_safepoint_check_flag);

  // initialized
  if (!JPortal || _initialized)
    return;

  // open shared memory
  _shm_id = shmget(IPC_PRIVATE, JPortalShmVolume, IPC_CREAT|0600);
  if (_shm_id < 0) {
    vm_exit_out_of_memory(JPortalShmVolume, INTERNAL_ERROR, "JPortalEnable: out of shared memory");
  }
  // initialize
  _shm_addr = (address)shmat(_shm_id, NULL, 0);

  struct ShmHeader *header = (ShmHeader *)_shm_addr;
  header->data_head = 0;
  header->data_tail = 0;
  header->data_size = JPortalShmVolume - sizeof(ShmHeader);

  _initialized = true;
}

#endif // JPORTAL_ENABLE

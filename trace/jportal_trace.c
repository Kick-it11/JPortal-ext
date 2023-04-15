/* An Intel PT tracing tool using system call in linux
 * of perf_event_open()
 * It only supports tracing user code space code.
 * It configures to trace one process/thread execution
 * on each CPU and supports inherition
 */

#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <fcntl.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <stdarg.h>
#include <inttypes.h>
#include <cpuid.h>

#define PERF_RECORD_AUXTRACE 71
#define PERF_RECORD_JVMRUNTIME 72

#define MSR_IA32_RTIT_CTL 0x00000570
#define MSR_IA32_RTIT_STATUS 0x00000571
#define MSR_IA32_RTIT_ADDR0_A 0x00000580
#define MSR_IA32_RTIT_ADDR0_B 0x00000581
#define MSR_IA32_RTIT_ADDR1_A 0x00000582
#define MSR_IA32_RTIT_ADDR1_B 0x00000583
#define MSR_IA32_RTIT_ADDR2_A 0x00000584
#define MSR_IA32_RTIT_ADDR2_B 0x00000585
#define MSR_IA32_RTIT_ADDR3_A 0x00000586
#define MSR_IA32_RTIT_ADDR3_B 0x00000587
#define MSR_IA32_RTIT_CR3_MATCH 0x00000572
#define MSR_IA32_RTIT_OUTPUT_BASE 0x00000560
#define MSR_IA32_RTIT_OUTPUT_MASK 0x00000561

#define barrier() __asm__ __volatile__("" \
                                       :  \
                                       :  \
                                       : "memory")

static __always_inline void __read_once_size(const volatile void *p, void *res, int size)
{
    switch (size)
    {
    case 1:
        *(__u8 *)res = *(volatile __u8 *)p;
        break;
    case 2:
        *(__u16 *)res = *(volatile __u16 *)p;
        break;
    case 4:
        *(__u32 *)res = *(volatile __u32 *)p;
        break;
    case 8:
        *(__u64 *)res = *(volatile __u64 *)p;
        break;
    default:
        barrier();
        __builtin_memcpy((void *)res, (const void *)p, size);
        barrier();
    }
}

static __always_inline void __write_once_size(volatile void *p, void *res, int size)
{
    switch (size)
    {
    case 1:
        *(volatile __u8 *)p = *(__u8 *)res;
        break;
    case 2:
        *(volatile __u16 *)p = *(__u16 *)res;
        break;
    case 4:
        *(volatile __u32 *)p = *(__u32 *)res;
        break;
    case 8:
        *(volatile __u64 *)p = *(__u64 *)res;
        break;
    default:
        barrier();
        __builtin_memcpy((void *)p, (const void *)res, size);
        barrier();
    }
}

#define READ_ONCE(x)                                \
    ({                                              \
        union                                       \
        {                                           \
            typeof(x) __val;                        \
            char __c[1];                            \
        } __u =                                     \
            {.__c = {0}};                           \
        __read_once_size(&(x), __u.__c, sizeof(x)); \
        __u.__val;                                  \
    })

#define WRITE_ONCE(x, val)                           \
    ({                                               \
        union                                        \
        {                                            \
            typeof(x) __val;                         \
            char __c[1];                             \
        } __u =                                      \
            {.__val = (val)};                        \
        __write_once_size(&(x), __u.__c, sizeof(x)); \
        __u.__val;                                   \
    })

#define mb() asm volatile("mfence" :: \
                              : "memory")
#define rmb() asm volatile("lfence" :: \
                               : "memory")
#define wmb() asm volatile("sfence" :: \
                               : "memory")
#define smp_rmb() barrier()
#define smp_wmb() barrier()
#define smp_mb() asm volatile("lock; addl $0,-132(%%rsp)" :: \
                                  : "memory", "cc")

static int page_size;
static volatile int done = 0;
static volatile unsigned long long record_samples = 0;
__u64 total_size = 0;

static const char *const cpu_vendors[] = {
    "",
    "GenuineIntel"};

enum
{
    pt_cpuid_vendor_size = 12
};

union cpu_vendor
{
    /* The raw data returned from cpuid. */
    struct
    {
        __u32 ebx;
        __u32 edx;
        __u32 ecx;
    } cpuid;

    /* The resulting vendor string. */
    char vendor_string[pt_cpuid_vendor_size];
};

/** A cpu vendor. */
enum pt_cpu_vendor
{
    pcv_unknown,
    pcv_intel
};

/** A cpu identifier. */
struct pt_cpu
{
    /** The cpu vendor. */
    enum pt_cpu_vendor vendor;

    /** The cpu family. */
    __u16 family;

    /** The cpu model. */
    __u8 model;

    /** The stepping. */
    __u8 stepping;
};

/* Supported address range configurations. */
enum pt_addr_cfg
{
    pt_addr_cfg_disabled = 0,
    pt_addr_cfg_filter = 1,
    pt_addr_cfg_stop = 2
};

struct shm_header
{
    volatile __u64 data_head;
    volatile __u64 data_tail;
    __u64 data_size;
};

struct cpu_mmap
{
    /* file descriptor of perf_event_open */
    int perf_fd;

    /* mmap base and aux base for each cpu*/
    void *mmap_base;
    void *aux_base;

    /* start of write */
    __u64 mmap_start;
    __u64 aux_start;
};

struct shm_record
{
    /* shared memory address for JVM */
    int shm_id;
    void *shm_addr;
    __u64 shm_start;
};

/* global tracing info */
struct trace_record
{
    struct perf_event_attr attr;

    /* cpu number : acquired by syscall */
    int nr_cpus;

    /* tracing process */
    pid_t pid;

    /* needed to be power of 2*/
    int mmap_pages;
    int aux_pages;

    /* file descriptor returned by open file */
    int fd;

    struct cpu_mmap *mmaps;
    struct shm_record jvmshm;
};

/** TraceData header */
struct trace_header
{
    /** Trace header size: in case of wrong trace data */
    __u64 header_size;

    /** PT CPU configurations: The filter. */
    __u32 filter; /*    0 for stop, 1 for filter*/

    /** PT CPU configurations: The cpu vendor. */
    __u32 vendor; /*    0 for pcv_unknown, 1 for pcv_intel*/

    /** PT CPU configurations: The cpu family. */
    __u16 family;

    /** PT CPU configurations: The cpu model. */
    __u8 model;

    /** PT CPU configurations: The stepping. */
    __u8 stepping;

    /** PT CPU configurations: The cpu numbers */
    __u32 nr_cpus;

    /** PT configurations: mtc frequency */
    __u8 mtc_freq;

    /** PT configurations: normal frequency */
    __u8 nom_freq;

    /** Sideband configurations: time shift */
    __u16 time_shift;

    /** PT configurations: cpuid 15 eax */
    __u32 cpuid_0x15_eax;

    /** PT configurations: cpuid 15 ebx */
    __u32 cpuid_0x15_ebx;

    /** Sideband configurations: cpuid 15 eax */
    __u32 time_mult;

    /** PT configurations: filter address lower bound */
    __u64 addr0_a;

    /** PT configurations: filter address higher bound */
    __u64 addr0_b;

    /** Sideband configurations: time zero */
    __u64 time_zero;

    /** Sideband configurations: sample type */
    __u64 sample_type;

    /** trace_ type **/
    __u64 trace_type;
};

struct auxtrace_event
{
    struct perf_event_header header;
    __u64 size;
    __u64 offset;
    __u64 reference;
    __u32 idx;
    __u32 tid;
    __u32 cpu;
    __u32 reservered__; /* for alignment */
};

struct jvmruntime_event
{
    struct perf_event_header header;
    __u64 size;
};

static __always_inline int perf_event_open(struct perf_event_attr *hw_event,
                                           pid_t pid, int cpu, int group_fd,
                                           unsigned long flags)
{
    return syscall(__NR_perf_event_open, hw_event, pid, cpu,
                   group_fd, flags);
}

static enum pt_cpu_vendor cpu_vendor(void)
{
    union cpu_vendor vendor;
    __u32 eax;
    size_t i;

    memset(&vendor, 0, sizeof(vendor));
    eax = 0;

    __get_cpuid(0u, &eax, &vendor.cpuid.ebx, &vendor.cpuid.ecx,
                &vendor.cpuid.edx);

    for (i = 0; i < sizeof(cpu_vendors) / sizeof(*cpu_vendors); i++)
        if (strncmp(vendor.vendor_string,
                    cpu_vendors[i], pt_cpuid_vendor_size) == 0)
            return (enum pt_cpu_vendor)i;

    return pcv_unknown;
}

static __u32 cpu_info(void)
{
    __u32 eax, ebx, ecx, edx;

    eax = 0;
    ebx = 0;
    ecx = 0;
    edx = 0;
    __get_cpuid(1u, &eax, &ebx, &ecx, &edx);

    return eax;
}

static int pt_cpu_read(struct pt_cpu *cpu)
{
    __u32 info;
    __u16 family;

    if (!cpu)
        return -1;

    cpu->vendor = cpu_vendor();

    info = cpu_info();

    cpu->family = family = (info >> 8) & 0xf;
    if (family == 0xf)
        cpu->family += (info >> 20) & 0xf;

    cpu->model = (info >> 4) & 0xf;
    if (family == 0x6 || family == 0xf)
        cpu->model += (info >> 12) & 0xf0;

    cpu->stepping = (info >> 0) & 0xf;

    return 0;
}

static __always_inline void wrmsr_on_cpu(__u32 reg, int cpu, __u64 data)
{
    int fd;
    char msr_file_name[64];

    sprintf(msr_file_name, "/dev/cpu/%d/msr", cpu);
    fd = open(msr_file_name, O_WRONLY);
    if (fd < 0)
    {
        if (errno == ENXIO)
        {
            fprintf(stderr, "JPortalTrace wrmsr: No CPU %d.\n", cpu);
            exit(2);
        }
        else if (errno == EIO)
        {
            fprintf(stderr, "JPortalTrace wrmsr: CPU %d doesn't support MSRs.\n",
                    cpu);
            exit(3);
        }
        else
        {
            perror("JPortalTrace wrmsr: open error.\n");
            exit(127);
        }
    }

    if (pwrite(fd, &data, sizeof data, reg) != sizeof data)
    {
        if (errno == EIO)
        {
            fprintf(stderr,
                    "JPortal wrmsr: CPU %d cannot set MSR %u to %llx.\n",
                    cpu, reg, data);
            exit(4);
        }
        else
        {
            perror("JPortalTrace wrmsr: pwrite error.\n");
            exit(127);
        }
    }

    close(fd);

    return;
}

static void wrmsr_on_all_cpus(int nr_cpus, __u32 reg, __u64 data)
{
    int cpu = 0;

    for (; cpu < nr_cpus; cpu++)
    {
        wrmsr_on_cpu(reg, cpu, data);
    }
}

static void trace_record_free(struct trace_record *record)
{
    if (!record)
        return;

    if (record->jvmshm.shm_addr)
        shmdt(record->jvmshm.shm_addr);

    if (record->jvmshm.shm_id != -1)
        shmctl(record->jvmshm.shm_id, IPC_RMID, NULL);

    if (record->mmaps)
        free(record->mmaps);

    free(record);
}

static struct trace_record *trace_record_alloc()
{
    struct trace_record *record = malloc(sizeof(struct trace_record));
    if (!record)
        return NULL;
    memset(record, 0, sizeof(*record));

    int cpu;

    int nr_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (nr_cpus < 0)
        nr_cpus = 0;
    record->nr_cpus = nr_cpus;
    record->fd = -1;

    record->jvmshm.shm_id = -1;

    record->mmaps = malloc(sizeof(struct cpu_mmap) * nr_cpus);
    memset(record->mmaps, 0, sizeof(struct cpu_mmap) * nr_cpus);
    for (cpu = 0; cpu < nr_cpus; ++cpu)
        record->mmaps[cpu].perf_fd = -1;

    return record;
}

static __always_inline int trace_event_enable_per_cpu(struct cpu_mmap *cpumm)
{
    return ioctl(cpumm->perf_fd, PERF_EVENT_IOC_ENABLE, 0);
}

static __always_inline int trace_event_enable(struct trace_record *record)
{
    if (!record)
        return -1;

    int cpu;
    for (cpu = 0; cpu < record->nr_cpus; cpu++)
    {
        int err = trace_event_enable_per_cpu(&record->mmaps[cpu]);
        if (err < 0)
        {
            return err;
        }
    }

    return 0;
}

static __always_inline int trace_event_disable_per_cpu(struct cpu_mmap *cpumm)
{
    return ioctl(cpumm->perf_fd, PERF_EVENT_IOC_DISABLE, 0);
}

static __always_inline int trace_event_disable(struct trace_record *record)
{
    if (!record)
        return -1;

    int cpu;
    for (cpu = 0; cpu < record->nr_cpus; cpu++)
    {
        int err = trace_event_disable_per_cpu(&record->mmaps[cpu]);
        if (err < 0)
        {
            return err;
        }
    }

    return 0;
}

static int read_tsc_conversion(const struct perf_event_mmap_page *pc,
                               __u32 *time_mult,
                               __u16 *time_shift,
                               __u64 *time_zero)
{
    bool cap_user_time_zero;
    __u32 seq;
    int i = 0;

    while (1)
    {
        seq = pc->lock;
        rmb();
        *time_mult = pc->time_mult;
        *time_shift = pc->time_shift;
        *time_zero = pc->time_zero;
        cap_user_time_zero = pc->cap_user_time_zero;
        rmb();
        if (pc->lock == seq && !(seq & 1))
            break;
        if (++i > 10000)
        {
            /*("failed to get perf_event_mmap_page lock\n");*/
            return -EINVAL;
        }
    }

    if (!cap_user_time_zero)
        return -EOPNOTSUPP;

    return 0;
}

static void tsc_ctc_ratio(__u32 *n, __u32 *d)
{
    unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;

    __get_cpuid(0x15, &eax, &ebx, &ecx, &edx);
    *n = ebx;
    *d = eax;
}

static __always_inline FILE *pt_open_file(const char *name)
{
    struct stat st;
    char path[4096];

    snprintf(path, 4096,
             "%s%s", "/sys/bus/event_source/devices/intel_pt/", name);

    if (stat(path, &st) < 0)
        return NULL;

    return fopen(path, "r");
}

static int pt_scan_file(const char *name, const char *fmt,
                        ...)
{
    va_list args;
    FILE *file;
    int ret = EOF;

    va_start(args, fmt);
    file = pt_open_file(name);
    if (file)
    {
        ret = vfscanf(file, fmt, args);
        fclose(file);
    }
    va_end(args);
    return ret;
}

static __always_inline int pt_pick_bit(int bits, int target)
{
    int pos, pick = -1;

    for (pos = 0; bits; bits >>= 1, pos++)
    {
        if (bits & 1)
        {
            if (pos <= target || pick < 0)
                pick = pos;
            if (pos >= target)
                break;
        }
    }

    return pick;
}

/* default config int perf_event_attr for Intel PT event*/
static __u64 pt_default_config()
{
    int mtc, mtc_periods = 0, mtc_period;
    int psb_cyc, psb_periods, psb_period;
    __u64 config = 0;
    char c;

    /* pt bit */
    config |= 1;
    /* tsc */
    config |= (1 << 10);
    /* disable ret compress */
    config |= (1 << 11);
    if (pt_scan_file("caps/mtc", "%d",
                     &mtc) != 1)
        mtc = 1;

    if (mtc)
    {
        /* mtc enable */
        config |= (1 << 9);
        if (pt_scan_file("caps/mtc_periods", "%x",
                         &mtc_periods) != 1)
            mtc_periods = 0;
        if (mtc_periods)
            mtc_period = pt_pick_bit(mtc_periods, 3);

        /* mtc period */
        config |= ((mtc_period & 0xf) << 14);
    }

    if (pt_scan_file("caps/psb_cyc", "%d",
                     &psb_cyc) != 1)
        psb_cyc = 1;

    if (psb_cyc && mtc_periods)
    {
        if (pt_scan_file("caps/psb_periods", "%x",
                         &psb_periods) != 1)
            psb_periods = 0;
        if (psb_periods)
        {
            psb_period = pt_pick_bit(psb_periods, 3);
        }
        config |= ((psb_period & 0xf) << 24);
    }

    /* branch enable */
    if (pt_scan_file("format/pt", "%c", &c) == 1 &&
        pt_scan_file("format/branch", "%c", &c) == 1)
        config |= (1 << 13);

    return config;
}

/* default perf_event_attr config for Intel PT*/
static int pt_default_attr(struct perf_event_attr *attr)
{
    if (!attr)
        return -1;

    attr->size = sizeof(struct perf_event_attr);
    attr->exclude_kernel = 1;
    attr->exclude_hv = 1;
    attr->sample_period = 1;
    attr->sample_freq = 1;
    attr->sample_type = 0x10087;
    attr->sample_id_all = 1;
    attr->read_format = 4;
    attr->inherit = 1;
    attr->context_switch = 1;

    __u64 config = pt_default_config();
    attr->config = config;

    int type;
    if (pt_scan_file("type", "%d", &type) != 1)
        return -1;
    attr->type = type;
    attr->enable_on_exec = 1;
    attr->disabled = 1;
    return 0;
}

/* trace open : perf_event_open()*/
static int trace_event_open(struct trace_record *record)
{
    if (!record)
        return -1;

    int nr_cpus = record->nr_cpus;
    pid_t pid = record->pid;
    int cpu;
    pt_default_attr(&record->attr);
    record->attr.aux_watermark = record->aux_pages * page_size / 4;
    record->attr.wakeup_events = 1;

    for (cpu = 0; cpu < nr_cpus; cpu++)
    {
        record->mmaps[cpu].perf_fd = perf_event_open(&record->attr,
                                                     pid, cpu, -1,
                                                     PERF_FLAG_FD_CLOEXEC);
        if (record->mmaps[cpu].perf_fd < 0)
            return -1;
    }

    for (cpu = 0; cpu < nr_cpus; cpu++)
    {
        struct perf_event_mmap_page *header;

        record->mmaps[cpu].mmap_base = mmap(NULL, (record->mmap_pages + 1) * page_size,
                                            PROT_WRITE | PROT_READ, MAP_SHARED,
                                            record->mmaps[cpu].perf_fd, 0);

        if (record->mmaps[cpu].mmap_base == MAP_FAILED)
            return -1;

        header = record->mmaps[cpu].mmap_base;
        header->aux_offset = header->data_offset + header->data_size;
        header->aux_size = record->aux_pages * page_size;

        record->mmaps[cpu].aux_base = mmap(NULL, header->aux_size,
                                           PROT_READ | PROT_WRITE, MAP_SHARED,
                                           record->mmaps[cpu].perf_fd,
                                           header->aux_offset);
        if (record->mmaps[cpu].aux_base == MAP_FAILED)
            return -1;

        /*printf("mmap : %p %p %p %llx %llx %llx %llx %llx %llx %llx %llx\n", record->mmap_base[cpu],
                record->mmap_base[cpu] + header->data_offset, record->aux_base[cpu],
                header->data_head, header->data_offset, header->data_size, header->data_tail,
                header->aux_head, header->aux_offset, header->aux_size, header->aux_tail);*/
    }

    record->fd = open("JPortalTrace.data", O_CREAT | O_RDWR | O_TRUNC | O_CLOEXEC, S_IRUSR | S_IWUSR);
    if (record->fd < 0)
    {
        return -1;
    }

    return 0;
}

/* trace close */
static void trace_event_close(struct trace_record *record)
{
    if (!record)
        return;

    int nr_cpus = record->nr_cpus;
    pid_t pid = record->pid;
    int cpu, ev;

    for (cpu = 0; cpu < nr_cpus; cpu++)
        if (record->mmaps[cpu].perf_fd >= 0)
            close(record->mmaps[cpu].perf_fd);

    for (cpu = 0; cpu < nr_cpus; cpu++)
    {
        if (record->mmaps[cpu].mmap_base > 0)
        {
            struct perf_event_mmap_page *header = record->mmaps[cpu].mmap_base;
            if (record->mmaps[cpu].aux_base > 0)
            {
                munmap(record->mmaps[cpu].aux_base, header->aux_size);
            }
            munmap(header, header->data_size + header->data_offset);
        }
    }

    if (record->fd >= 0)
    {
        close(record->fd);
    }
}

static __always_inline ssize_t ion(bool is_read, int fd, void *buf, size_t n)
{
    void *buf_start = buf;
    size_t left = n;

    while (left)
    {
        /* buf must be treated as const if !is_read. */
        ssize_t ret = is_read ? read(fd, buf, left) : write(fd, buf, left);

        if (ret < 0 && errno == EINTR)
        {
            continue;
        }

        if (ret <= 0)
        {
            return ret;
        }

        left -= ret;
        buf += ret;
    }

    return n;
}

static __always_inline ssize_t record_write(int fd, const void *buf, size_t n)
{
    record_samples++;
    total_size += n;
    return ion(false, fd, (void *)buf, n);
}

/* these reads and writes might be non-atomic in a non-64 bit platform */
static __always_inline __u64 auxtrace_mmap_read_head(struct perf_event_mmap_page *header)
{
    __u64 head = READ_ONCE(header->aux_head);

    /* To ensure all read are done after read head; */
    smp_rmb();
    return head;
}

static __always_inline void auxtrace_mmap_write_tail(struct perf_event_mmap_page *header,
                                                     __u64 tail)
{
    /* To ensure all read are done before write tail */
    smp_mb();

    WRITE_ONCE(header->aux_tail, tail);
}

static __always_inline __u64 mmap_read_head(struct perf_event_mmap_page *header)
{
    __u64 head = READ_ONCE(header->data_head);

    /* To ensure all read are done after read head; */
    smp_rmb();

    return head;
}

static __always_inline void mmap_write_tail(struct perf_event_mmap_page *header,
                                            __u64 tail)
{
    /* To ensure all read are done before write tail */
    smp_mb();

    WRITE_ONCE(header->data_tail, tail);
}

static __always_inline __u64 shm_read_head(struct shm_header *header)
{
    __u64 head = READ_ONCE(header->data_head);

    /* To ensure all read are done after read head; */
    smp_rmb();

    return head;
}

static __always_inline void shm_write_tail(struct shm_header *header,
                                           __u64 tail)
{
    /* To ensure all read are done before write tail */
    smp_mb();

    WRITE_ONCE(header->data_tail, tail);
}

static __always_inline int auxtrace_mmap_record(struct cpu_mmap *cpumm, int cpu, int fd)
{
    struct perf_event_mmap_page *header = cpumm->mmap_base;
    __u64 head, old;
    old = cpumm->aux_start;
    __u64 offset;
    unsigned char *data = cpumm->aux_base;
    size_t size, head_off, old_off, len1, len2;
    void *data1, *data2;
    size_t len = header->aux_size;

    head = auxtrace_mmap_read_head(header);

    if (old == head)
    {
        return 0;
    }

    int mask = len - 1;
    head_off = head & mask;
    old_off = old & mask;

    if (head_off > old_off)
    {
        size = head_off - old_off;
    }
    else
    {
        size = len - (old_off - head_off);
    }

    if (head > old || size <= head || mask)
    {
        offset = head - size;
    }
    else
    {
        /*
         * When the buffer size is not a power of 2, 'head' wraps at the
         * highest multiple of the buffer size, so we have to subtract
         * the remainder here.
         */
        __u64 rem = (0ULL - len) % len;

        offset = head - size - rem;
    }

    if (size > head_off)
    {
        len1 = size - head_off;
        data1 = &data[len - len1];
        len2 = head_off;
        data2 = &data[0];
    }
    else
    {
        len1 = size;
        data1 = &data[head_off - len1];
        len2 = 0;
        data2 = NULL;
    }

    /* printf("aux r: %p %p %llx %llx %llx %llx %llx %llx\n",
             mmap_base, aux_base, old, head,
             len, mask, old_off, head_off); */

    struct auxtrace_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.header.type = PERF_RECORD_AUXTRACE;
    ev.header.size = sizeof(ev);
    ev.size = size;
    ev.offset = offset;
    ev.cpu = cpu;
    ev.tid = -1;

    if (record_write(fd, &ev, sizeof(ev)) < 0)
    {
        return -1;
    }

    if (record_write(fd, data1, len1) < 0)
    {
        return -1;
    }

    if (len2 && record_write(fd, data2, len2) < 0)
    {
        return -1;
    }

    cpumm->aux_start = head;
    auxtrace_mmap_write_tail(header, head);

    /*enable*/
    return trace_event_enable_per_cpu(cpumm);
}

static __always_inline int mmap_record(struct cpu_mmap *cpumm, int cpu, int fd)
{
    struct perf_event_mmap_page *header = cpumm->mmap_base;
    __u64 head, old;
    old = cpumm->mmap_start;
    head = mmap_read_head(header);
    __u64 size = head - old;
    int mask = header->data_size - 1;
    unsigned char *data = cpumm->mmap_base + header->data_offset;
    void *buff;

    if (old == head)
    {
        return 0;
    }

    /*printf("mmap r: %p %llx %llx %llx %llx %llx %llx\n",
             mmap_base, old, head, size, mask);*/

    if ((old & mask) + size != (head & mask))
    {
        buff = &data[old & mask];
        size = mask + 1 - (old & mask);
        old += size;

        if (record_write(fd, buff, size) < 0)
        {
            return -1;
        }
    }

    buff = &data[old & mask];
    size = (head & mask) - (old & mask);
    old += size;

    if (record_write(fd, buff, size) < 0)
    {
        return -1;
    }

    cpumm->mmap_start = head;
    mmap_write_tail(header, head);

    return 0;
}

static __always_inline int shm_record(struct shm_record *jvmshm, int fd)
{
    struct shm_header *header = (struct shm_header *)jvmshm->shm_addr;
    __u64 head, old;
    void *data_begin = jvmshm->shm_addr + sizeof(struct shm_header);
    size_t size;

    old = jvmshm->shm_start;
    head = shm_read_head(header);

    if (head == old)
        return 0;

    if (old < head)
    {
        size = head - old;
    }
    else
    {
        size = header->data_size - old + head;
    }

    struct jvmruntime_event event;
    event.header.type = PERF_RECORD_JVMRUNTIME;
    event.header.size = sizeof(event);
    event.size = size;

    if (record_write(fd, &event, sizeof(event)) < 0)
    {
        return -1;
    }

    if (old < head)
    {
        if (record_write(fd, data_begin + old, head - old) < 0)
        {
            return -1;
        }
    }
    else
    {
        if (record_write(fd, data_begin + old, header->data_size - old) < 0)
        {
            return -1;
        }

        if (record_write(fd, data_begin, head) < 0)
        {
            return -1;
        }
    }

    jvmshm->shm_start = head;

    shm_write_tail(header, head);

    return 0;
}

static __always_inline int record_all(struct trace_record *record)
{
    int nr_cpus = record->nr_cpus;
    int cpu;

    for (cpu = 0; cpu < nr_cpus; cpu++)
    {
        if (mmap_record(&record->mmaps[cpu], cpu, record->fd) < 0)
        {
            return -1;
        }

        if (auxtrace_mmap_record(&record->mmaps[cpu], cpu, record->fd) < 0)
        {
            return -1;
        }
    }

    if (shm_record(&record->jvmshm, record->fd) < 0)
    {
        return -1;
    }

    return 0;
}

static void sig_handler(int sig)
{
    done = 1;
}

int main(int argc, char *argv[])
{
    if (argc != 9)
    {
        fprintf(stderr, "JPortalTrace: arguments error.\n");
        exit(-1);
    }

    int ret, write_pipe;
    __u64 _low_bound, _high_bound;
    struct trace_record *rec = trace_record_alloc();
    int cpu;
    int trace_type;
    page_size = __getpagesize();

    if (!rec)
    {
        fprintf(stderr, "JPortalTrace: fail to alloc record.\n");
        return -1;
    }
    sscanf(argv[1], "%d", &rec->pid);
    sscanf(argv[2], "%d", &write_pipe);
    sscanf(argv[3], "%llx", &_low_bound);
    sscanf(argv[4], "%llx", &_high_bound);
    sscanf(argv[5], "%d", &rec->mmap_pages);
    sscanf(argv[6], "%d", &rec->aux_pages);
    sscanf(argv[7], "%d", &rec->jvmshm.shm_id);
    sscanf(argv[8], "%d", &trace_type);

    rec->jvmshm.shm_addr = shmat(rec->jvmshm.shm_id, NULL, 0);

    /* write msr to set ip filter */
    wrmsr_on_all_cpus(rec->nr_cpus, MSR_IA32_RTIT_ADDR0_A, _low_bound);
    wrmsr_on_all_cpus(rec->nr_cpus, MSR_IA32_RTIT_ADDR0_B, _high_bound);

    if (trace_event_open(rec) < 0)
    {
        fprintf(stderr, "JPortalTrace: trace open error.\n");
        goto err;
    }

    for (cpu = 0; cpu < rec->nr_cpus; cpu++)
    {
        int errr = ioctl(rec->mmaps[cpu].perf_fd,
                         PERF_EVENT_IOC_SET_FILTER,
                         "filter 0x580/580@/bin/bash");
        if (errr < 0)
        {
            fprintf(stderr, "JPortalTrace: set filter error.\n");
            goto err;
        }
    }

    struct trace_header attr;
    struct pt_cpu cpuinfo;
    int max_nonturbo_ratio;
    read_tsc_conversion(rec->mmaps->mmap_base, &attr.time_mult,
                        &attr.time_shift, &attr.time_zero);
    pt_cpu_read(&cpuinfo);
    attr.header_size = sizeof(attr);
    attr.filter = (__u32)pt_addr_cfg_filter;
    attr.vendor = (__u32)cpuinfo.vendor;
    attr.family = cpuinfo.family;
    attr.model = cpuinfo.model;
    attr.stepping = cpuinfo.stepping;
    attr.trace_type = trace_type;
    tsc_ctc_ratio(&attr.cpuid_0x15_ebx, &attr.cpuid_0x15_eax);
    pt_scan_file("max_nonturbo_ratio", "%d", &max_nonturbo_ratio);
    attr.nom_freq = (__u8)max_nonturbo_ratio;
    attr.mtc_freq = ((rec->attr.config >> 14) & 0xf);
    attr.sample_type = 0x10087;
    attr.nr_cpus = rec->nr_cpus;
    attr.addr0_a = _low_bound;
    attr.addr0_b = _high_bound;

    if (record_write(rec->fd, &attr, sizeof(attr)) < 0)
    {
        goto err;
    }

    signal(SIGTERM, sig_handler);

    trace_event_enable(rec);

    /* notify parent process */
    char bf;
    if (write(write_pipe, &bf, 1) < 0)
    {
        fprintf(stderr, "JPortalTrace: write pipe error\n");
        goto err;
    }
    close(write_pipe);

    printf("JPortalTrace starts(process: %d  ip filter: %llx-%llx).\n",
           rec->pid, _low_bound, _high_bound);

    for (;;)
    {
        unsigned long long hits = record_samples;

        if (record_all(rec) < 0)
        {
            fprintf(stderr, "JPortalTrace: record error\n");
            goto err;
        }

        if (done && record_samples == hits)
        {
            printf("JPortalTrace end with %llu data generated\n", total_size);
            break;
        }
    }

out:
    trace_event_disable(rec);
    trace_event_close(rec);
    trace_record_free(rec);
    exit(0);

err:
    trace_event_disable(rec);
    trace_event_close(rec);
    trace_record_free(rec);
    exit(-1);
}

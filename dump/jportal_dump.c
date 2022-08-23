#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/shm.h>
#include <sys/prctl.h>
#include <signal.h>

typedef unsigned char u_char;
typedef u_char* address;

struct ShmHeader {
    volatile uint64_t data_head;
    volatile uint64_t data_tail;
};

static volatile int dump_fd;
static void sig_handler(int sig)
{
    close(dump_fd);
    dump_fd = -1;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "JPortalDump: argument error\n");
        exit(-1);
    }
    int shm_id;
    size_t shm_volume;
    sscanf(argv[1], "%d", &shm_id);
    sscanf(argv[2], "%d", &dump_fd);
    sscanf(argv[3], "%lu", &shm_volume);
    
    FILE *dumper_file = fopen("JPortalDump.data", "wb");
    if (!dumper_file) {
      close(dump_fd);
      exit(-1);
    }
    printf("JPortalDump process starts.\n");
    address shm_addr = (address)shmat(shm_id, NULL, 0);
    address data_begin = shm_addr + sizeof(struct ShmHeader);
    address data_end = shm_addr + shm_volume;
    size_t data_volume = shm_volume - sizeof(struct ShmHeader);

    signal(SIGTERM, sig_handler);

    while (1) {
        char bf;
        if (read(dump_fd, &bf, 1) != 1) {
            break;
        }
        struct ShmHeader *header = (struct ShmHeader *)shm_addr;
        uint64_t data_head = header->data_head;
        uint64_t data_tail = header->data_tail;
        if (data_head == data_tail) { continue; }
        if (data_tail < data_head)
            fwrite(data_begin + data_tail, 
                   data_head - data_tail, 1, dumper_file);
        else {
            fwrite(data_begin + data_tail, 
                   data_volume - data_tail, 1, dumper_file);
            fwrite(data_begin, data_head, 1, dumper_file);
        }
        header->data_tail = data_head;
    }
    struct ShmHeader *header = (struct ShmHeader *)shm_addr;
    uint64_t data_head = header->data_head;
    uint64_t data_tail = header->data_tail;
    if (data_tail < data_head)
        fwrite(data_begin + data_tail, 
               data_head - data_tail, 1, dumper_file);
    else if (data_tail > data_head) {
        fwrite(data_begin + data_tail, 
               data_volume - data_tail, 1, dumper_file);
        fwrite(data_begin, data_head, 1, dumper_file);
    }
    header->data_tail = data_head;
    printf("JPortalDump process ends.\n");
out:
    fclose(dumper_file);
    shmdt(shm_addr);
    exit(0);
}

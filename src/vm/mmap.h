#define PROT_NONE 0x0
#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_EXEC 0x4

#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02

u64 mmap(void *addr, int length, int prot, int flags, int fd, usize offset);
int munmap(void *addr, usize length);
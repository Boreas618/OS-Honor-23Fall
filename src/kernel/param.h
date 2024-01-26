#define MAX_PAGES 1048576
#define INPUT_BUF_SIZE 128
#define NCPU 4
#define STACK_SIZE 8 * 4096
#define HEAP_SIZE 8 * 4096
#define STACK_BASE 0x0000ffff00000000
#define STACK_PAGES_INIT 8
#define MAX_ARG 32
#define MAX_ENV 128
#define SLAB_MAX_ORDER 11
#define NR_SYSCALL 512
#define SLICE_LEN 1
#define PID_POOL_SIZE 1 << 20
#define MMAP_LAZY
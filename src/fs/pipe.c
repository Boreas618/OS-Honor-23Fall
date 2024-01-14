#include <kernel/mem.h>
#include <proc/sched.h>
#include <fs/pipe.h>
#include <lib/string.h>

int pipeAlloc(File** f0, File** f1) {
    // TODO
}

void pipeClose(Pipe* pi, int writable) {
    // TODO
}

int pipeWrite(Pipe* pi, u64 addr, int n) {
    // TODO
}

int pipeRead(Pipe* pi, u64 addr, int n) {
    // TODO
}
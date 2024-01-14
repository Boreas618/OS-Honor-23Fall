#include <driver/interrupt.h>
#include <driver/uart.h>
#include <kernel/console.h>
#include <kernel/init.h>
#include <lib/sem.h>
#include <lib/spinlock.h>
#include <proc/sched.h>

struct console cons;

define_init(console) {
    set_interrupt_handler(IRQ_AUX, console_intr);
    init_spinlock(&cons.lock);
}

isize console_write(struct inode *ip, char *buf, isize n) {
    inodes.unlock(ip);
    acquire_spinlock(&cons.lock);
    for (isize i = 0; i < n; i++)
        uart_put_char(buf[i]);
    release_spinlock(&cons.lock);
    inodes.lock(ip);
    return n;
}

isize console_read(struct inode *ip, char *dst, isize n) {
    usize target = n;
    inodes.unlock(ip);
    acquire_spinlock(&cons.lock);
    while (n > 0) {
        while (cons.read_idx == cons.write_idx) {
            if (is_killed(thisproc())) {
                release_spinlock(&cons.lock);
                return -1;
            }
            sleep(&cons.read_idx, &cons.lock);
        }

        // Read the char from the buffer.
        char c = cons.buf[cons.read_idx++ % INPUT_BUF_SIZE];
        
        // End-of-file
        if (c == C('D'))
            break;

        dst[target-n] = c;
        n--;

        // A whole line has arrived.
        if (c == '\n')
            break;
    }
     _release_spinlock(&cons.lock);
    inodes.lock(ip);
    return target-n;
}

void console_intr(char (*getc)()) {
    // TODO
    (void)getc;
}

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
    init_sem(&cons.sem, 0);
}

isize console_write(struct inode *ip, char *buf, isize n) {
    // The inode should represent a device.
    if (ip->entry.type != INODE_DEVICE)
        return -1;
    
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
            _lock_sem(&cons.sem);
            release_spinlock(&cons.lock);
            if (!_wait_sem(&cons.sem, false)) {
                inodes.lock(ip);
                return -1;
            }
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
    acquire_spinlock(&cons.lock);
    char c = getc();
    switch(c) {
        case C('U'):
            while (cons.edit_idx != cons.write_idx && cons.buf[(cons.edit_idx-1) % INPUT_BUF_SIZE] != '\n') {
                cons.edit_idx--;
                uart_put_char('\b'); 
                uart_put_char(' '); 
                uart_put_char('\b');
            }
        break;
        case C('H'): // Backspace
        case '\x7f': // Delete key
            if (cons.edit_idx != cons.write_idx) {
                cons.edit_idx--;
                uart_put_char('\b'); 
                uart_put_char(' '); 
                uart_put_char('\b');
            }
        break;
        default:
            if (c != 0 && cons.edit_idx - cons.read_idx < INPUT_BUF_SIZE) {
                c = (c == '\r') ? '\n' : c;
                // Echo back to the user.
                uart_put_char(c);
                cons.buf[cons.edit_idx++ % INPUT_BUF_SIZE] = c;
                
                if (c == '\n' || c == C('D') || cons.edit_idx - cons.read_idx == INPUT_BUF_SIZE) {
                    cons.write_idx = cons.edit_idx;
                    post_all_sem(&cons.sem);
                }
            }
        break;
  }
}

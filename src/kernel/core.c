#include <kernel/cpu.h>
#include <lib/printk.h>
#include <kernel/init.h>
#include <proc/sched.h>
#include <test/test.h>

void disk_init();

bool panic_flag;

NO_RETURN void 
idle_entry() 
{
    set_cpu_on();
    while (1) {
        yield();
        if (panic_flag)
            break;
        arch_with_trap {
            arch_wfi();
        }
    }
    set_cpu_off();
    arch_stop_cpu();
}

NO_RETURN void 
kernel_entry() 
{
    printk("Hello, world!\n");
    disk_init();
    // sd_test();
    // proc_test();
    // vm_test();
    // user_proc_test();
    pgfault_first_test();
    pgfault_second_test();
    
    do_rest_init();

    while (1)
        yield();
}

NO_INLINE NO_RETURN void _panic(const char* file, int line) {
    printk("[CPU %d PANIC] ", cpuid());
    panic_flag = true;
    set_cpu_off();
    for (int i = 0; i < NCPU; i++) {
        if (cpus[i].online)
            i--;
    }
    printk("Kernel PANIC invoked at %s:%d.\n", file, line);
    arch_stop_cpu();
}

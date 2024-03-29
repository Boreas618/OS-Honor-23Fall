#include <lib/format.h>
#include <lib/spinlock.h>
#include <kernel/init.h>
#include <lib/printk.h>

static SpinLock printk_lock;

define_early_init(printk)
{
	init_spinlock(&printk_lock);
}

static void _put_char(void *_ctx, char c)
{
	(void)_ctx;
	putch(c);
}

static void _vprintf(const char *fmt, va_list arg)
{
	acquire_spinlock(&printk_lock);
	vformat(_put_char, NULL, fmt, arg);
	release_spinlock(&printk_lock);
}

void printk(const char *fmt, ...)
{
	va_list arg;
	va_start(arg, fmt);
	_vprintf(fmt, arg);
	va_end(arg);
}
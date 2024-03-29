#include <kernel/init.h>

extern char early_init[];
extern char rest_init[];
extern char init[];
extern char einit[];

void do_early_init()
{
	for (u64 *p = (u64 *)&early_init; p < (u64 *)&rest_init; p++)
		((void (*)()) * p)();
}

void do_rest_init()
{
	for (u64 *p = (u64 *)&rest_init; p < (u64 *)&init; p++)
		((void (*)()) * p)();
}

void do_init()
{
	for (u64 *p = (u64 *)&init; p < (u64 *)&einit; p++)
		((void (*)()) * p)();
}

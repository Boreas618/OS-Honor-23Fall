#pragma once

#include <lib/defines.h>

/* BSS available */
#define define_early_init(name)                                               \
	static void init_##name();                                            \
	static __attribute__((section(".init.early"),                         \
			      used)) volatile const void *__initcall_##name = \
		&init_##name;                                                 \
	static void init_##name()

/* Allocator available */
#define define_init(name)                                                     \
	static void init_##name();                                            \
	static __attribute__((section(".init"),                               \
			      used)) volatile const void *__initcall_##name = \
		&init_##name;                                                 \
	static void init_##name()

/* Scheduler available */
#define define_rest_init(name)                                                \
	static void init_##name();                                            \
	static __attribute__((section(".init.rest"),                          \
			      used)) volatile const void *__initcall_##name = \
		&init_##name;                                                 \
	static void init_##name()

void do_early_init();
void do_init();
void do_rest_init();

#pragma once

#include <lib/checker.h>
#include <lib/defines.h>

typedef struct ref_count RefCount;

struct ref_count {
    isize count;
};

void increment_rc(RefCount *);
bool decrement_rc(RefCount *);
void init_rc(RefCount *);
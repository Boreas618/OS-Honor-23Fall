#pragma once

#include <lib/defines.h>
#include <lib/checker.h>

typedef struct refcount RefCount;
struct refcount{
    isize count;
};

void increment_rc(RefCount*);
bool decrement_rc(RefCount*);
void init_rc(RefCount*);
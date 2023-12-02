#pragma once

#include <common/defines.h>
#include <common/checker.h>

typedef struct {
    isize count;
} RefCount;

void increment_rc(RefCount*);
bool decrement_rc(RefCount*);
void init_rc(RefCount*);
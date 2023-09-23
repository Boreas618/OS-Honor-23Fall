#include <aarch64/intrinsic.h>
#include <test/test.h>

NO_RETURN void idle_entry()
{
    //sing_alloc_test();
    alloc_test();
    arch_stop_cpu();
}

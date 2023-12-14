file(GLOB vm_sources CONFIGURE_DEPENDS "*.c")

add_library(vm STATIC ${vm_sources})
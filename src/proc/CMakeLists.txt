file(GLOB proc_sources CONFIGURE_DEPENDS "*.c")

add_library(proc STATIC ${proc_sources})

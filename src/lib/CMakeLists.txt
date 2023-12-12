file(GLOB lib_sources CONFIGURE_DEPENDS "*.c")

add_library(lib STATIC ${lib_sources})

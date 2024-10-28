cmake_minimum_required(VERSION 3.5)
project(netfr LANGUAGES C)

include_directories(include)

set(NETFR_SOURCES
  src/common/nfr.c
  src/common/nfr_mem.c
  src/common/nfr_log.c
  src/common/nfr_resource.c

  src/host/nfr_host_callback.c
  src/host/nfr_host.c

  src/client/nfr_client_callback.c
  src/client/nfr_client.c
)

add_compile_options(
  "-Wall"
  "-Werror"
  "-Wstrict-prototypes"
  "-Wfatal-errors"
  "-ffast-math"
  "-fdata-sections"
  "-ffunction-sections"
  "$<$<CONFIG:DEBUG>:-O0;-g3;-ggdb>"
  "-fsanitize=address"
  "-fsanitize=undefined"
)

add_link_options(
  "-fsanitize=address"
  "-fsanitize=undefined"
)

add_library(netfr STATIC ${NETFR_SOURCES})

target_link_libraries(netfr PRIVATE fabric)
target_include_directories(netfr
	INTERFACE
		include
	PRIVATE
		src
)
cmake_minimum_required(VERSION 3.5)
project(netfr-client)

get_filename_component(NETFR_TOP "${PROJECT_SOURCE_DIR}/../../.." ABSOLUTE)
include_directories(${NETFR_TOP}/include)
add_subdirectory(${NETFR_TOP}/netfr ${CMAKE_CURRENT_BINARY_DIR}/netfr)

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

add_executable(netfr-client client.c)
target_link_libraries(netfr-client netfr)

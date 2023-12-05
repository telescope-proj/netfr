cmake_minimum_required(VERSION 3.5)
project(netfr LANGUAGES C CXX)

include_directories(
  ${PROJECT_SOURCE_DIR}/include
)

add_subdirectory("${PROJECT_SOURCE_DIR}/../tcm/tcm" "${CMAKE_BINARY_DIR}/tcm")
  
set(NETFR_SOURCES
src/nfr_client.cpp
src/nfr_server.cpp
src/nfr_util.cpp
src/nfr_vmsg.cpp
)
  
add_library(netfr STATIC ${NETFR_SOURCES})

set_property(TARGET netfr PROPERTY C_STANDARD   11)
set_property(TARGET netfr PROPERTY CXX_STANDARD 11)
target_link_libraries(netfr tcm)
target_include_directories(netfr
  PUBLIC
    include
  PRIVATE
    src
)
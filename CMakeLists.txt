cmake_minimum_required(VERSION 3.16)
project(ddump)

include(FindPkgConfig)
pkg_check_modules(CURL libcurl REQUIRED)

include_directories(${CURL_INCLUDE_DIRS})

set(CMAKE_CXX_STANDARD 20)

add_executable(ddump ddump.cpp)

target_link_libraries(ddump ${CURL_LIBRARIES})
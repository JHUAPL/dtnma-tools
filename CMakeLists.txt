cmake_minimum_required(VERSION 3.10)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
project(nm LANGUAGES C)

option(BUILD_TESTING "Enable test fixtures and libraries" ON)
option(BUILD_AGENT "Build the Agent library and executable" ON)
option(BUILD_MANAGER "Build the Manager library and executable" ON)

# Language options
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
add_compile_options(
    -Wall
    -Wextra
    #-Wpedantic
    $<$<COMPILE_LANGUAGE:C>:-Werror=implicit-function-declaration>
    $<$<COMPILE_LANGUAGE:C>:-Werror=incompatible-pointer-types>
)
add_compile_options(
    -ffunction-sections
    -fdata-sections
    -fno-omit-frame-pointer
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-enforce-eh-specs>
    $<$<COMPILE_LANGUAGE:CXX>:-fnothrow-opt>
)
IF(CMAKE_COMPILER_IS_GNUCC)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0")
ENDIF(CMAKE_COMPILER_IS_GNUCC)

# Library dependencies
include(CheckSymbolExists)
check_symbol_exists(posix_memalign "stdlib.h" HAVE_POSIX_MEMALIGN)
check_symbol_exists(memalign "malloc.h" HAVE_MEMALIGN)
check_symbol_exists(timespec_get "time.h" HAVE_TIMESPEC_GET)
check_symbol_exists(clock_gettime "time.h" HAVE_CLOCK_GETTIME)
add_definitions(
  -DHAVE_POSIX_MEMALIGN=${HAVE_POSIX_MEMALIGN}
  -DHAVE_MEMALIGN=${HAVE_MEMALIGN}
)

# Testing config
include(CTest)

include(FindPkgConfig)
add_subdirectory(deps)

# Libaries in deps but built separately
find_package(ION)
find_package(QCBOR)
find_package(MLIB)
if(BUILD_MANAGER)
  pkg_search_module(MYSQLCLIENT mysqlclient)
  find_package(civetweb)
  find_package(cJSON)
endif(BUILD_MANAGER)

#set(BUILD_SHARED_LIBS ON)
add_subdirectory(src)

if(BUILD_TESTING)
  add_subdirectory(test)
endif(BUILD_TESTING)
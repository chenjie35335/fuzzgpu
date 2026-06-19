cmake_minimum_required(VERSION 3.22)
project(difftest_gpu)

option(ENABLE_DPI "Enable DPI interface" ON)

set(CMAKE_C_COMPILER "gcc-13")
set(CMAKE_CXX_COMPILER "g++-13")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "-fPIC")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

## Debug
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -g") # -fsanitize=address")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g") # -fsanitize=address")

# Release
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

if(NOT DEFINED DIFFTEST_XLEN)
message(FATAL_ERROR "Please set DIFFTEST_XLEN (to 32 or 64)")
endif()
if(NOT DEFINED ARCH)
  message(FATAL_ERROR "Please set ARCH (to vortex, ventus)")
endif()

set(LIBNAME "difftest_gpu${DIFFTEST_XLEN}")

set(DIFFTEST_SV_SOURCES
  ${CMAKE_SOURCE_DIR}/vsrc/Difftest_pkg.sv
  ${CMAKE_SOURCE_DIR}/vsrc/ResetEventListener.sv
  ${CMAKE_SOURCE_DIR}/vsrc/DecodeEventListener.sv
  ${CMAKE_SOURCE_DIR}/vsrc/WritebackRegEventListener.sv
  ${CMAKE_SOURCE_DIR}/vsrc/DcacheShmemReqEventListener.sv
  ${CMAKE_SOURCE_DIR}/vsrc/DcacheShmemRspEventListener.sv
  ${CMAKE_SOURCE_DIR}/vsrc/L2DcacheReqEventListener.sv
)

add_library(${LIBNAME}
  ${CMAKE_SOURCE_DIR}/csrc/dpi.cc
  ${CMAKE_SOURCE_DIR}/csrc/api.cc
  ${CMAKE_SOURCE_DIR}/csrc/config.cc
  ${CMAKE_SOURCE_DIR}/csrc/rob.cc
  ${CMAKE_SOURCE_DIR}/csrc/arch_state.cc
  ${CMAKE_SOURCE_DIR}/csrc/per_warp.cc
  ${CMAKE_SOURCE_DIR}/csrc/difftest.cc
  ${CMAKE_SOURCE_DIR}/csrc/util.cc
  ${CMAKE_SOURCE_DIR}/csrc/debugger.cc
  ${CMAKE_SOURCE_DIR}/csrc/fp_check.cc
  ${CMAKE_SOURCE_DIR}/csrc/mem_axioms_checker.cc
  ${CMAKE_SOURCE_DIR}/csrc/thread_throttle.cc
)

set(DEFS )
if("${ARCH}" STREQUAL "vortex")
  set(DEFS ${DEFS} ARCH_VORTEX)
elseif("${ARCH}" STREQUAL "ventus")
  set(DEFS ${DEFS} ARCH_VENTUS)
else()
  message(FATAL_ERROR "unsupported ARCH ${ARCH}")
endif()

if(ENABLE_DPI)
  set(DEFS ${DEFS} ENABLE_DPI)
endif()

target_compile_definitions(${LIBNAME} PRIVATE DIFFTEST_XLEN_${DIFFTEST_XLEN} ${DEFS})


set_source_files_properties(
  ${CMAKE_SOURCE_DIR}/csrc/dpi.cc 
  PROPERTIES
    OBJECT_DEPENDS "${DIFFTEST_SV_SOURCES}"
)

include_directories(csrc/include)


## Requires

find_package(Verilator REQUIRED)
message(STATUS "Verilator version: ${Verilator_VERSION}")
include_directories(${VERILATOR_ROOT}/include
                    ${VERILATOR_ROOT}/include/vltstd
                    ${CMAKE_SOURCE_DIR}/thirdparty/concurrentqueue)

if(NOT TARGET spdlog)
    # Stand-alone build
    find_package(spdlog REQUIRED)
endif()
target_link_libraries(${LIBNAME} PRIVATE spdlog::spdlog_header_only)

# readline: prefer pkg-config, fall back to Find*/manual lookup
find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_READLINE QUIET readline)
else()
  message(FATAL_ERROR "Pkg-config not found.")
endif()

if(DEFINED PC_READLINE_FOUND AND PC_READLINE_FOUND)
  target_include_directories(${LIBNAME} PRIVATE ${PC_READLINE_INCLUDE_DIRS})
  target_link_libraries(${LIBNAME} PRIVATE ${PC_READLINE_LIBRARIES})
else()
  # Try to find headers/libraries directly (portable fallback)
  find_path(READLINE_INCLUDE_DIR readline/readline.h)
  find_library(READLINE_LIBRARY NAMES readline)
  include(FindPackageHandleStandardArgs)
  find_package_handle_standard_args(Readline DEFAULT_MSG READLINE_LIBRARY READLINE_INCLUDE_DIR)
  if(READLINE_FOUND)
    target_include_directories(${LIBNAME} PRIVATE ${READLINE_INCLUDE_DIR})
    target_link_libraries(${LIBNAME} PRIVATE ${READLINE_LIBRARY})
     message(STATUS 2222211111111111111)
  else()
    message(FATAL_ERROR "Readline not found. Install development package (e.g. libreadline-dev) or ensure pkg-config can find 'readline'.")
  endif()
endif()
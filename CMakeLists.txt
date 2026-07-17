cmake_minimum_required(VERSION 3.22)
project(fuzz_gpu)

option(ENABLE_COVERAGE "Enable coverage" OFF)
option(ENABLE_EMU_DEBUG "Enable ISA emulator debug mode" OFF)
option(ENABLE_EMU_COVERAGE_LINE "Enable ISA emulator line coverage" OFF)
option(ENABLE_EMU_COVERAGE_TOGGLE "Enable ISA emulator toggle coverage" OFF)
option(ENABLE_EMU_COVERAGE_EXPR "Enable ISA emulator expression coverage" OFF)
option(ENABLE_DIFFTEST "Enable RTL difftest for ventus DUT" OFF)
option(ENABLE_ADDRESS_SANITIZER "Enable address sanitizer" OFF)
option(ENABLE_DOCS "Build Doxygen documentation" OFF)

if(NOT DEFINED ARCH)
set(ARCH "ventus")
endif()

#
# Vortex configs
#
set(VORTEX_NUM_CORES 2)
set(VORTEX_NUM_CLUSTERS 2)
set(VORTEX_EXT_TCU_ENABLE ON)
set(VORTEX_GBAR_ENABLE ON)

if("${ARCH}" STREQUAL "ventus")
  set(XLEN 32)
  message(WARNING "Ventus currently only supports rv32")
else()
  if(NOT DEFINED XLEN)
    message(FATAL_ERROR "Please set XLEN (to 32 or 64)")
  endif()
endif()


set(CMAKE_CXX_STANDARD 20)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set(CMAKE_CXX_FLAGS "-fPIC -Wall")
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

  ## Debug
  set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -g")
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g")

  if (ENABLE_ADDRESS_SANITIZER)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
  endif()

  # Release
  if(NOT ENABLE_COVERAGE)
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
  elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    message(WARNING "will not use -O3 when ENABLE_COVERAGE")
  endif()

  # Coverage
  if(ENABLE_COVERAGE)
    set(COVERAGE_FLAGS "-g --coverage -fprofile-arcs -ftest-coverage")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COVERAGE_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
  endif()
else()
  message(FATAL_ERROR "unsupported compiler ${CMAKE_CXX_COMPILER_ID}")
endif()

## Requires

#
# spglog
#
if(NOT TARGET spdlog)
  # Stand-alone build
  find_package(spdlog REQUIRED)
endif()

#
# googletest
#
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(FUZZGPU_GOOGLETEST_SOURCE_DIR
    "${CMAKE_SOURCE_DIR}/thirdparty/googletest"
    CACHE PATH "Local googletest source directory")
if(EXISTS "${FUZZGPU_GOOGLETEST_SOURCE_DIR}/CMakeLists.txt")
  message(STATUS "Using local googletest: ${FUZZGPU_GOOGLETEST_SOURCE_DIR}")
  add_subdirectory(
    "${FUZZGPU_GOOGLETEST_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/thirdparty/googletest-build"
    EXCLUDE_FROM_ALL)
else()
  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
    DOWNLOAD_EXTRACT_TIMESTAMP true
  )
  FetchContent_MakeAvailable(googletest)
endif()

if(ENABLE_COVERAGE)
  execute_process(
    COMMAND ${CMAKE_CXX_COMPILER} -dumpversion
    OUTPUT_VARIABLE COMPILER_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )

  message(STATUS "g++ Version: ${COMPILER_VERSION}")

  set(GCOV_CANDIDATES
    "gcov-${COMPILER_VERSION}"
    "gcov"
  )

  foreach(gcov_candidate ${GCOV_CANDIDATES})
    find_program(TEMP_GCOV ${gcov_candidate})
    if(TEMP_GCOV)
      execute_process(
        COMMAND ${TEMP_GCOV} --version
        OUTPUT_VARIABLE GCOV_VERSION_OUTPUT
        ERROR_VARIABLE GCOV_VERSION_ERROR
        RESULT_VARIABLE GCOV_VERSION_RESULT
      )
      if(GCOV_VERSION_RESULT EQUAL 0)
        set(GCOV_PATH ${TEMP_GCOV})
        message(STATUS "Found gcov: ${GCOV_PATH}")
        break()
      endif()
    endif()
  endforeach()

  find_program(GCOVR_PATH gcovr)
endif()

#
# capstone
#
set(CAPSTONE_ARCHITECTURE_DEFAULT no)
set(CAPSTONE_RISCV_SUPPORT yes)
set(CAPSTONE_BUILD_SHARED_LIBS no)
set(CAPSTONE_BUILD_TESTS no)
set(CAPSTONE_BUILD_CSTOOL no)
add_subdirectory(thirdparty/capstone)

#
# difftest-gpu
#
set(DIFFTEST_HOME ${CMAKE_SOURCE_DIR}/difftest-gpu)

if("${ARCH}" STREQUAL "vortex")
  #
  # vortex emulator
  #
  message(STATUS "Building for Vortex architecture")

  set(VORTEX_HOME ${CMAKE_SOURCE_DIR}/targets/vortex)

  # Determine coverage type
  if(ENABLE_EMU_COVERAGE_LINE)
    set(VORTEX_COV_TYPE "line")
  elseif(ENABLE_EMU_COVERAGE_TOGGLE)
    set(VORTEX_COV_TYPE "toggle")
  elseif(ENABLE_EMU_COVERAGE_EXPR)
    set(VORTEX_COV_TYPE "expr")
  else()
    set(VORTEX_COV_TYPE "none")
  endif()

  # Determine difftest enabled
  if(ENABLE_DIFFTEST)
    set(VORTEX_DFT_ENABLED "1")
  else()
    set(VORTEX_DFT_ENABLED "0")
  endif()

  set(VORTEX_BUILD_DIR ${VORTEX_HOME}/build${XLEN}-cov${VORTEX_COV_TYPE}-dft${VORTEX_DFT_ENABLED})
  if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND ENABLE_EMU_DEBUG)
    string(APPEND VORTEX_BUILD_DIR "-debug")
  endif()
  set(VORTEX_ISA_EMULATOR_LIB ${VORTEX_BUILD_DIR}/runtime/libemulator_vortex.a)
  set(VORTEX_DUT_LIB ${VORTEX_BUILD_DIR}/sim/difftest/libdifftest.so)

  add_custom_command(
    OUTPUT ${VORTEX_BUILD_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${VORTEX_BUILD_DIR}
  )

  if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND ENABLE_EMU_DEBUG)
    set(VORTEX_DEBUG_VAR "DEBUG=1")
    message(STATUS "Debug build for the vortex emulator")
  else()
    set(VORTEX_DEBUG_VAR "")
  endif()

  if(ENABLE_ADDRESS_SANITIZER)
    set(VORTEX_ASAN_VAR "ASAN=1")
    message(STATUS "Enable Address Sanitizer for the vortex emulator")
  else()
    set(VORTEX_ASAN_VAR "")
  endif()

  set(VORTEX_CONFIGS "CONFIGS=-DNUM_CORES=${VORTEX_NUM_CORES} -DNUM_CLUSTERS=${VORTEX_NUM_CLUSTERS}")
  if(VORTEX_EXT_TCU_ENABLE)
    set(VORTEX_CONFIGS "${VORTEX_CONFIGS} -DEXT_TCU_ENABLE")
  endif()
  if(VORTEX_GBAR_ENABLE)
    set(VORTEX_CONFIGS "${VORTEX_CONFIGS} -DGBAR_ENABLE")
  endif()

  if(NOT ENABLE_DIFFTEST)
    set(VORTEX_CONFIGS "${VORTEX_CONFIGS} -DDISABLE_DIFFTEST")
  endif()

  if(ENABLE_EMU_COVERAGE_LINE)
    set(VORTEX_CONFIGS "${VORTEX_CONFIGS} -DCOVERAGE_LINE")
  elseif(ENABLE_EMU_COVERAGE_TOGGLE)
    set(VORTEX_CONFIGS "${VORTEX_CONFIGS} -DCOVERAGE_TOGGLE")
  elseif(ENABLE_EMU_COVERAGE_EXPR)
    set(VORTEX_CONFIGS "${VORTEX_CONFIGS} -DCOVERAGE_EXPR")
  endif()

  add_custom_target(vortex-emu ALL
    DEPENDS ${VORTEX_BUILD_DIR}
    WORKING_DIRECTORY ${VORTEX_BUILD_DIR}
    # TODO: config tools dir
    COMMAND ../configure --xlen=${XLEN} --tooldir=$$HOME/tools
    COMMAND ${CMAKE_COMMAND}
      -DVORTEX_BUILD_DIR=${VORTEX_BUILD_DIR}
      -DXLEN=${XLEN}
      -P ${CMAKE_SOURCE_DIR}/cmake/EnsureVortexToolchain.cmake
    COMMAND ${CMAKE_COMMAND}
      -DVERILATOR_SOURCE_DIR=${CMAKE_SOURCE_DIR}/thirdparty/verilator
      -DVERILATOR_BUILD_DIR=${CMAKE_BINARY_DIR}/thirdparty-verilator-autotools-build
      -P ${CMAKE_SOURCE_DIR}/cmake/EnsureVerilator.cmake
    # 1. build runtime/difftest
    # 2. build sim/difftest lib
    COMMAND bash -c 'source ./ci/toolchain_env.sh && make -s -j`nproc` && DIFFTEST_HOME="${DIFFTEST_HOME}" ${VORTEX_DEBUG_VAR} ${VORTEX_ASAN_VAR} ${VORTEX_CONFIGS} make -C runtime/difftest -j`nproc` && DIFFTEST_HOME="${DIFFTEST_HOME}" ${VORTEX_DEBUG_VAR} ${VORTEX_ASAN_VAR} ${VORTEX_CONFIGS} make -C sim/difftest lib -j`nproc`'
    COMMENT "Making vortex ISA emulator library"
  )

elseif("${ARCH}" STREQUAL "ventus")
  #
  # ventus emulator
  #
  message(STATUS "Building for Ventus architecture")

  set(VENTUS_HOME ${CMAKE_SOURCE_DIR}/targets/ventus-env)

  set(VENTUS_SPIKE_BUILD_DIR ${VENTUS_HOME}/spike/build${XLEN})
  set(VENTUS_ISA_EMULATOR_LIB ${VENTUS_SPIKE_BUILD_DIR}/install/lib/libfuzzgpu.so)

  if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND ENABLE_EMU_DEBUG)
    set(RELEASE 0)
  else()
    set(RELEASE 1)
  endif()
  
  if(ENABLE_EMU_COVERAGE_LINE)
    set(VENTUS_COVERAGE_LINE 1)
    set(VENTUS_COVERAGE_TOGGLE 0)
    set(VENTUS_COVERAGE_EXPR 0)
    set(VENTUS_COV_TYPE "line")
    message(STATUS "Enable line coverage build for the ventus DUT")
  elseif(ENABLE_EMU_COVERAGE_TOGGLE)
    set(VENTUS_COVERAGE_LINE 0)
    set(VENTUS_COVERAGE_TOGGLE 1)
    set(VENTUS_COVERAGE_EXPR 0)
    set(VENTUS_COV_TYPE "toggle")
    message(STATUS "Enable toggle coverage build for the ventus DUT")
  elseif(ENABLE_EMU_COVERAGE_EXPR)
    set(VENTUS_COVERAGE_LINE 0)
    set(VENTUS_COVERAGE_TOGGLE 0)
    set(VENTUS_COVERAGE_EXPR 1)
    set(VENTUS_COV_TYPE "expr")
    message(STATUS "Enable expression coverage build for the ventus DUT")
  else()
    set(VENTUS_COVERAGE_LINE 0)
    set(VENTUS_COVERAGE_TOGGLE 0)
    set(VENTUS_COVERAGE_EXPR 0)
    set(VENTUS_COV_TYPE "none")
  endif()

  if(ENABLE_DIFFTEST)
    set(VENTUS_RTL_DIFFTEST_ENABLED "true")
    set(VENTUS_DFT_ENABLED "1")
    set(VENTUS_ENABLE_DPI "ON")
    message(STATUS "Enable RTL difftest for the ventus DUT")
  else()
    set(VENTUS_RTL_DIFFTEST_ENABLED "false")
    set(VENTUS_DFT_ENABLED "0")
    set(VENTUS_ENABLE_DPI "OFF")
  endif()

  # Construct DUT library path based on configuration
  set(VENTUS_DUT_LIB ${VENTUS_HOME}/gpgpu/sim-verilator/build-cov${VENTUS_COV_TYPE}-dft${VENTUS_DFT_ENABLED}/libDUT/libdut.so)

  # rules for the dut
  add_custom_target(ventus-dut ALL
    WORKING_DIRECTORY ${VENTUS_HOME}/gpgpu/sim-verilator
    COMMAND ${CMAKE_COMMAND}
      -DVERILATOR_SOURCE_DIR=${CMAKE_SOURCE_DIR}/thirdparty/verilator
      -DVERILATOR_BUILD_DIR=${CMAKE_BINARY_DIR}/thirdparty-verilator-autotools-build
      -P ${CMAKE_SOURCE_DIR}/cmake/EnsureVerilator.cmake
    COMMAND bash -c 'source ${VENTUS_HOME}/../ventus_env.sh && DIFFTEST_HOME="${DIFFTEST_HOME}" RELEASE="${RELEASE}" COVERAGE_LINE="${VENTUS_COVERAGE_LINE}" COVERAGE_TOGGLE="${VENTUS_COVERAGE_TOGGLE}" COVERAGE_EXPR="${VENTUS_COVERAGE_EXPR}" RTL_DIFFTEST_ENABLED="${VENTUS_RTL_DIFFTEST_ENABLED}" THREADS=`nproc` make -f dut.mk lib -j`nproc`'
    COMMENT "Making ventus DUT"
  )

  # rules for the isa emulator
  add_custom_command(
    OUTPUT ${VENTUS_SPIKE_BUILD_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${VENTUS_SPIKE_BUILD_DIR}
  )

  set(VENTUS_CXXFLAGS -DFUZZGPU_ISA_EMULATOR -I${DIFFTEST_HOME}/csrc/include -DDIFFTEST_XLEN_${XLEN} -DARCH_VENTUS)
  
  if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND ENABLE_EMU_DEBUG)
    set(VENTUS_CXXFLAGS ${VENTUS_CXXFLAGS} -g -O0)
    if (ENABLE_ADDRESS_SANITIZER)
      set(VENTUS_CXXFLAGS ${VENTUS_CXXFLAGS} -fsanitize=address)
      set(VENTUS_LDFLAGS ${VENTUS_LDFLAGS} -fsanitize=address)
    endif()
    message(STATUS "Debug build for the ventus emulator")
  endif()
  
  add_custom_target(ventus-emu ALL
    DEPENDS ${VENTUS_SPIKE_BUILD_DIR}
    WORKING_DIRECTORY ${VENTUS_SPIKE_BUILD_DIR}
    COMMAND bash -c 'CXXFLAGS="${VENTUS_CXXFLAGS}" CFLAGS="${VENTUS_CXXFLAGS}" LDFLAGS="${VENTUS_LDFLAGS}" ../configure --prefix=${VENTUS_SPIKE_BUILD_DIR}/install' # --enable-commitlog
    COMMAND make -j`nproc`
    COMMAND make install
    COMMENT "Making ventus ISA emulator library"
  )

else()
  message(FATAL_ERROR "Unsupported ARCH: ${ARCH}")
endif()

#
# Documentation
#
find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
  set(DOXYGEN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/docs" CACHE PATH "Doxygen output directory")
  set(DOXYGEN_HTML_OUTPUT "html" CACHE STRING "Doxygen HTML output directory relative to DOXYGEN_OUTPUT_DIR")
  find_program(DOXYGEN_DOT_EXECUTABLE dot)
  if(DOXYGEN_DOT_EXECUTABLE)
    set(DOXYGEN_HAVE_DOT YES)
  else()
    set(DOXYGEN_HAVE_DOT NO)
  endif()
  configure_file(${CMAKE_SOURCE_DIR}/Doxyfile.in
                 ${CMAKE_BINARY_DIR}/Doxyfile @ONLY)

  if(ENABLE_DOCS)
    set(DOCS_ALL ALL)
  else()
    set(DOCS_ALL)
  endif()

  add_custom_target(docs ${DOCS_ALL}
    COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_BINARY_DIR}/Doxyfile
    COMMAND ${CMAKE_COMMAND}
            -DDOCS_INDEX_FILE=${DOXYGEN_OUTPUT_DIR}/index.html
            -DDOCS_REDIRECT_TARGET=${DOXYGEN_HTML_OUTPUT}/index.html
            -P ${CMAKE_SOURCE_DIR}/cmake/write_docs_redirect.cmake
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Generating Doxygen documentation"
    VERBATIM
  )
  add_custom_target(doc DEPENDS docs)
elseif(ENABLE_DOCS)
  message(FATAL_ERROR "ENABLE_DOCS requires Doxygen")
endif()


#
# fuzz_gpu library and executable
#
add_library(lib_fuzz_gpu STATIC
  ${CMAKE_SOURCE_DIR}/src/isa/riscv/riscv_meta_list.cc
  ${CMAKE_SOURCE_DIR}/src/isa/riscv/riscv_instr.cc
  ${CMAKE_SOURCE_DIR}/src/isa/riscv/riscv_encode.cc
  ${CMAKE_SOURCE_DIR}/src/isa/riscv/riscv_floating_point_meta.cc
  ${CMAKE_SOURCE_DIR}/src/isa/riscv/riscv_amo_meta.cc
  ${CMAKE_SOURCE_DIR}/src/isa/riscv/riscv_vector_meta.cc
  ${CMAKE_SOURCE_DIR}/src/isa/riscv/riscv_meta.cc
  ${CMAKE_SOURCE_DIR}/src/isa/riscv/rv32a_instr.cc
  ${CMAKE_SOURCE_DIR}/src/isa/riscv/rv32d_instr.cc
  ${CMAKE_SOURCE_DIR}/src/isa/riscv/rv32f_instr.cc
  ${CMAKE_SOURCE_DIR}/src/isa/riscv/rv32i_instr.cc
  ${CMAKE_SOURCE_DIR}/src/isa/riscv/rv32m_instr.cc
  ${CMAKE_SOURCE_DIR}/src/isa/riscv/rv64a_instr.cc
  ${CMAKE_SOURCE_DIR}/src/isa/riscv/rv64d_instr.cc
  ${CMAKE_SOURCE_DIR}/src/isa/riscv/rv64f_instr.cc
  ${CMAKE_SOURCE_DIR}/src/isa/riscv/rv64i_instr.cc
  ${CMAKE_SOURCE_DIR}/src/isa/riscv/rv64m_instr.cc
  ${CMAKE_SOURCE_DIR}/src/isa/riscv/rvv_instr.cc
  ${CMAKE_SOURCE_DIR}/src/isa/riscv/rv_zicond_instr.cc
  ${CMAKE_SOURCE_DIR}/src/isa/riscv/vortex/vortex_instr.cc
  ${CMAKE_SOURCE_DIR}/src/isa/riscv/ventus/ventus_instr.cc
  ${CMAKE_SOURCE_DIR}/src/register_sampler.cc
  ${CMAKE_SOURCE_DIR}/src/instr_pool.cc
  ${CMAKE_SOURCE_DIR}/src/gen_warp.cc
  ${CMAKE_SOURCE_DIR}/src/gen_bcc.cc
  ${CMAKE_SOURCE_DIR}/src/gen_thread_ctrl.cc
  ${CMAKE_SOURCE_DIR}/src/gen_thread_merge.cc
  ${CMAKE_SOURCE_DIR}/src/gen_tensor_core.cc
  ${CMAKE_SOURCE_DIR}/src/gen_refresh.cc
  ${CMAKE_SOURCE_DIR}/src/gen_regext.cc
  ${CMAKE_SOURCE_DIR}/src/gen_csr.cc
  ${CMAKE_SOURCE_DIR}/src/random_mem_allocator.cc
  ${CMAKE_SOURCE_DIR}/src/generator.cc
  ${CMAKE_SOURCE_DIR}/src/gen_barrier.cc
  ${CMAKE_SOURCE_DIR}/src/gen_load_store.cc
  ${CMAKE_SOURCE_DIR}/src/ldst_operand_history.cc
  ${CMAKE_SOURCE_DIR}/src/scheduler_vortex.cc
  ${CMAKE_SOURCE_DIR}/src/scheduler_ventus.cc
  ${CMAKE_SOURCE_DIR}/src/reg_dep_tracker.cc
  ${CMAKE_SOURCE_DIR}/src/taint_prop.cc
  ${CMAKE_SOURCE_DIR}/src/fuzzer_state.cc
  ${CMAKE_SOURCE_DIR}/src/gen_initial_block.cc
  ${CMAKE_SOURCE_DIR}/src/bug_filter.cc
  ${CMAKE_SOURCE_DIR}/src/config.cc
  ${CMAKE_SOURCE_DIR}/src/util.cc
  ${CMAKE_SOURCE_DIR}/src/types.cc
  ${CMAKE_SOURCE_DIR}/src/scratchpad.cc
)

set(COMMON_LIBS
  spdlog::spdlog_header_only
  capstone
)
set(COMMON_DEFS
  DIFFTEST_XLEN_${XLEN}
)
#
# Config for vortex
#
if("${ARCH}" STREQUAL "vortex")
  link_directories(${VORTEX_HOME}/third_party/ramulator)
  set(COMMON_LIBS ${COMMON_LIBS}
    ${VORTEX_ISA_EMULATOR_LIB}
    ${VORTEX_HOME}/third_party/softfloat/build/Linux-x86_64-GCC/softfloat.a
    ramulator)
  set(COMMON_DEFS ${COMMON_DEFS} ARCH_VORTEX)
  set(DUT_LIBS ${VORTEX_DUT_LIB})
  add_dependencies(lib_fuzz_gpu vortex-emu)
endif()
#
# Config for ventus
#
if("${ARCH}" STREQUAL "ventus")
  set(COMMON_LIBS ${COMMON_LIBS} ${VENTUS_ISA_EMULATOR_LIB})
  set(COMMON_DEFS ${COMMON_DEFS} ARCH_VENTUS)
  set(DIFFTEST_LIB ${DIFFTEST_HOME}/build${XLEN}-ventus-dpi-${VENTUS_ENABLE_DPI}/libdifftest_gpu${XLEN}.a)
  set(DUT_LIBS ${VENTUS_DUT_LIB} ${DIFFTEST_LIB})
  add_dependencies(lib_fuzz_gpu ventus-emu)
endif()
target_compile_definitions(lib_fuzz_gpu PUBLIC ${COMMON_DEFS})


include_directories(${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/thirdparty/magic_enum-v0.9.7/include
  ${CMAKE_SOURCE_DIR}/thirdparty/json/include
  ${LLVM_INCLUDE_DIRS}
  ${DIFFTEST_HOME}/csrc/include
  ${CMAKE_SOURCE_DIR}/thirdparty/capstone/include
  ${CMAKE_SOURCE_DIR}/thirdparty/argparse/include
)

add_executable(fuzz_gpu
  ${CMAKE_SOURCE_DIR}/src/fuzzgpu_main.cc
)
target_link_libraries(fuzz_gpu
  PRIVATE
  lib_fuzz_gpu
  ${COMMON_LIBS}
)

add_executable(emulator
  ${CMAKE_SOURCE_DIR}/src/emulator_main.cc
  ${CMAKE_SOURCE_DIR}/src/emulator_dbg_cmd.cc
)
target_link_libraries(emulator
  PRIVATE
  lib_fuzz_gpu
  ${COMMON_LIBS}
  ${DUT_LIBS}
  readline
  -Wl,--no-as-needed
)
target_compile_definitions(emulator PUBLIC ${COMMON_DEFS})
if("${ARCH}" STREQUAL "ventus")
add_dependencies(emulator ventus-dut)
elseif("${ARCH}" STREQUAL "vortex")
add_dependencies(emulator vortex-emu)
endif()

add_executable(get_arch_config
  ${CMAKE_SOURCE_DIR}/src/get_arch_config.cc
)
target_link_libraries(get_arch_config
  PRIVATE
  lib_fuzz_gpu
  ${COMMON_LIBS}
  ${DUT_LIBS}
  readline
)
target_compile_definitions(get_arch_config PUBLIC ${COMMON_DEFS})
if("${ARCH}" STREQUAL "ventus")
add_dependencies(get_arch_config ventus-dut)
elseif("${ARCH}" STREQUAL "vortex")
add_dependencies(get_arch_config vortex-emu)
endif()

#
# Testing targets
#
enable_testing()

add_executable(
  tests
  #tests/tst_random_memory_allocator.cc
)
target_link_libraries(
  tests
  PRIVATE
  lib_fuzz_gpu
  ${COMMON_LIBS}
  GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(tests)

if(ENABLE_COVERAGE)
  set(COVERAGE_REPORT_DIR ${CMAKE_BINARY_DIR}/coverage_report)
  if(GCOVR_PATH)
    add_custom_target(coverage
      COMMAND ${CMAKE_COMMAND} -E make_directory ${COVERAGE_REPORT_DIR}
      COMMAND find ${CMAKE_BINARY_DIR} -name "*.gcda" -delete
      COMMAND $<TARGET_FILE:tests>
      COMMAND ${GCOVR_PATH}
      --root "${CMAKE_SOURCE_DIR}"
      --filter "${CMAKE_SOURCE_DIR}/src"
      --exclude=".*tests.*"
      --exclude=".*googletest.*"
      --exclude=".*build.*"
      --html-details
      --output ${COVERAGE_REPORT_DIR}/index.html
      --print-summary
      --xml ${COVERAGE_REPORT_DIR}/coverage.xml
      --gcov-executable ${GCOV_PATH}
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      COMMENT "Generating coverage report"
    )
  else()
    message(WARNING "Cannot find coverage tools: gcovr")
  endif()
endif()

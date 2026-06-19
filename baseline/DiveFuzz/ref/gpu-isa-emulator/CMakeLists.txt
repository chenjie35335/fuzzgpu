cmake_minimum_required(VERSION 3.22)

# If run standalone, we need to define project and ARCH/XLEN
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(spike_engine)
    if(NOT DEFINED ARCH)
        message(FATAL_ERROR "ARCH not defined")
    endif()
    if(NOT DEFINED XLEN)
        message(FATAL_ERROR "XLEN not defined")
    endif()
    get_filename_component(PROJECT_ROOT_DIR "../../../../" ABSOLUTE)
else()
    set(PROJECT_ROOT_DIR ${CMAKE_SOURCE_DIR})
endif()

option(ENABLE_EMU_DEBUG "Enable ISA emulator debug mode" OFF)
option(ENABLE_ADDRESS_SANITIZER "Enable address sanitizer" OFF)
option(ENABLE_COVERAGE "Enable coverage" OFF)
option(ENABLE_EMU_COVERAGE_LINE "Enable ISA emulator line coverage" OFF)
option(ENABLE_EMU_COVERAGE_TOGGLE "Enable ISA emulator toggle coverage" OFF)
option(ENABLE_EMU_COVERAGE_EXPR "Enable ISA emulator expression coverage" OFF)
option(ENABLE_DIFFTEST "Enable RTL difftest for ventus DUT" OFF)

set(DIFFTEST_HOME ${PROJECT_ROOT_DIR}/difftest-gpu)

# Find Python and Pybind11
set(Python_FIND_VIRTUALENV FIRST)
find_package(Python 3.10 COMPONENTS Interpreter Development REQUIRED)
message(STATUS "Python_EXECUTABLE: ${Python_EXECUTABLE}")

# Try to find pybind11 via the found Python interpreter
execute_process(
    COMMAND "${Python_EXECUTABLE}" -c "import pybind11; print(pybind11.get_cmake_dir())"
    OUTPUT_VARIABLE _pybind11_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _pybind11_status
)

if(_pybind11_status EQUAL 0 AND IS_DIRECTORY "${_pybind11_dir}")
    message(STATUS "Found pybind11 via python: ${_pybind11_dir}")
    list(APPEND CMAKE_PREFIX_PATH "${_pybind11_dir}")
else()
    message(WARNING "Could not find pybind11 via python command. Status: ${_pybind11_status}")
endif()

find_package(pybind11 REQUIRED)

# --- User provided logic for emulators ---

if("${ARCH}" STREQUAL "vortex")
  #
  # vortex emulator
  #
  message(STATUS "Building for Vortex architecture")

  set(VORTEX_HOME ${PROJECT_ROOT_DIR}/targets/vortex)

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

  if(NOT DEFINED VORTEX_NUM_CORES)
      set(VORTEX_NUM_CORES 2)
  endif()
  if(NOT DEFINED VORTEX_NUM_CLUSTERS)
      set(VORTEX_NUM_CLUSTERS 2)
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
    COMMAND bash -c 'source ./ci/toolchain_env.sh && make -s -j`nproc` && DIFFTEST_HOME="${DIFFTEST_HOME}" ${VORTEX_DEBUG_VAR} ${VORTEX_CONFIGS} make -C runtime/difftest -j`nproc` && DIFFTEST_HOME="${DIFFTEST_HOME}" make -C sim/difftest lib'
    COMMENT "Making vortex ISA emulator library"
  )

elseif("${ARCH}" STREQUAL "ventus")
  #
  # ventus emulator
  #
  message(STATUS "Building for Ventus architecture")

  set(VENTUS_HOME ${PROJECT_ROOT_DIR}/targets/ventus-env)

  # Determine coverage type
  if(ENABLE_EMU_COVERAGE_LINE)
    set(VENTUS_COV_TYPE "line")
  elseif(ENABLE_EMU_COVERAGE_TOGGLE)
    set(VENTUS_COV_TYPE "toggle")
  elseif(ENABLE_EMU_COVERAGE_EXPR)
    set(VENTUS_COV_TYPE "expr")
  else()
    set(VENTUS_COV_TYPE "none")
  endif()

  # Determine difftest enabled
  if(ENABLE_DIFFTEST)
    set(VENTUS_DFT_ENABLED "1")
  else()
    set(VENTUS_DFT_ENABLED "0")
  endif()

  set(VENTUS_SPIKE_BUILD_DIR ${VENTUS_HOME}/spike/build${XLEN}-cov${VENTUS_COV_TYPE}-dft${VENTUS_DFT_ENABLED})
  set(VENTUS_ISA_EMULATOR_LIB ${VENTUS_SPIKE_BUILD_DIR}/install/lib/libfuzzgpu.so)

  if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND ENABLE_EMU_DEBUG)
    set(RELEASE 0)
  else()
    set(RELEASE 1)
  endif()

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

# --- End of user provided logic ---

# Define spike_engine module
pybind11_add_module(spike_engine spike_engine.cpp bindings.cpp)

# Include directories
target_include_directories(spike_engine PRIVATE
    ${DIFFTEST_HOME}/csrc/include
)

# Compiler definitions
string(TOUPPER ${ARCH} ARCH_UPPER)
target_compile_definitions(spike_engine PRIVATE
    FUZZGPU_ISA_EMULATOR
    DIFFTEST_XLEN_${XLEN}
    ARCH_${ARCH_UPPER}
)

# Link libraries
if("${ARCH}" STREQUAL "vortex")
    target_link_libraries(spike_engine PRIVATE ${VORTEX_ISA_EMULATOR_LIB} ${VORTEX_HOME}/third_party/softfloat/build/Linux-x86_64-GCC/softfloat.a ${VORTEX_HOME}/third_party/ramulator/libramulator.so)
    add_dependencies(spike_engine vortex-emu)
elseif("${ARCH}" STREQUAL "ventus")
    target_link_libraries(spike_engine PRIVATE ${VENTUS_ISA_EMULATOR_LIB})
    add_dependencies(spike_engine ventus-emu)
endif()

# System libs
target_link_libraries(spike_engine PRIVATE pthread dl boost_regex boost_system)

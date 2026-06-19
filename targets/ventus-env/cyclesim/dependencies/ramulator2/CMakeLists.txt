cmake_minimum_required(VERSION 3.14)

project(
  Ramulator
  VERSION 2.0
  LANGUAGES CXX
)

#
# dependency libs
#
include(FetchContent)
find_package(yaml-cpp REQUIRED)
find_package(spdlog REQUIRED)
FetchContent_Declare(argparse
    GIT_REPOSITORY https://github.com/p-ranav/argparse.git
    GIT_TAG        v2.9     
)
FetchContent_MakeAvailable(argparse)
include_directories(${argparse_SOURCE_DIR}/include)

#
# global settings
#
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DRAMULATOR_DEBUG")
include_directories(${PROJECT_SOURCE_DIR}/src)
# TODO: this will effect top-level CMakeLists.txt, need to be fixed
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

#
# target libramulator.so
#
add_library(ramulator SHARED)
target_link_libraries(ramulator 
  PUBLIC yaml-cpp
  PRIVATE spdlog
  PRIVATE fmt
)
target_include_directories(ramulator 
  PUBLIC "${PROJECT_SOURCE_DIR}/src"
)
install(TARGETS ramulator 
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
)

#
# target ramulator2 exe
#
add_executable(ramulator-exe)
target_link_libraries(ramulator-exe 
  # PRIVATE -Wl,--whole-archive ramulator -Wl,--no-whole-archive 
  PRIVATE ramulator
  PRIVATE spdlog
  PRIVATE fmt
  # PRIVATE argparse # header-only lib
)
set_target_properties(ramulator-exe PROPERTIES 
  OUTPUT_NAME ramulator2
)

#
# include subdirectories
#
add_subdirectory(src)
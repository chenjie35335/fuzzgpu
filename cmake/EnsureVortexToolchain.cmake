if(NOT DEFINED VORTEX_BUILD_DIR)
  message(FATAL_ERROR "VORTEX_BUILD_DIR is required")
endif()

if(NOT DEFINED XLEN)
  message(FATAL_ERROR "XLEN is required")
endif()

set(toolchain_gcc "$ENV{HOME}/tools/riscv${XLEN}-gnu-toolchain/bin/riscv${XLEN}-unknown-elf-gcc")

if(NOT EXISTS "${toolchain_gcc}")
  message(STATUS "Preparing Vortex official toolchain with ./ci/toolchain_install.sh --all. This requires network access.")
  execute_process(
    COMMAND ./ci/toolchain_install.sh --all
    WORKING_DIRECTORY "${VORTEX_BUILD_DIR}"
    RESULT_VARIABLE toolchain_install_result
  )
endif()

if(NOT EXISTS "${toolchain_gcc}")
  message(FATAL_ERROR
    "Vortex official toolchain download failed or did not complete. "
    "The official toolchain executable was not produced at ${toolchain_gcc} "
    "after ./ci/toolchain_install.sh --all. "
    "This is the official Vortex toolchain download step and requires network access; "
    "check the container network/proxy or preinstall it under $ENV{HOME}/tools."
  )
endif()

if(NOT DEFINED VERILATOR_SOURCE_DIR)
  message(FATAL_ERROR "VERILATOR_SOURCE_DIR is required")
endif()

if(NOT DEFINED VERILATOR_BUILD_DIR)
  message(FATAL_ERROR "VERILATOR_BUILD_DIR is required")
endif()

set(expected_verilator_version "5.034")
set(expected_verilator_revision "436d3d76f0ff841cc0d1e3fdf0c888ebf40137b2")
set(expected_verilator_pattern "Verilator[ \t]+${expected_verilator_version}([ \t]|$)")
set(verilator_prefix "$ENV{HOME}/tools/verilator")
set(verilator_bin "${verilator_prefix}/bin/verilator")
set(verilator_build_source_dir "${VERILATOR_BUILD_DIR}/verilator-v${expected_verilator_version}")
include(ProcessorCount)
ProcessorCount(verilator_jobs)
if(NOT verilator_jobs)
  set(verilator_jobs 1)
endif()

function(read_verilator_version_text out_var)
  set(version_text "")
  if(EXISTS "${verilator_bin}")
    execute_process(
      COMMAND "${verilator_bin}" --version
      OUTPUT_VARIABLE version_output
      ERROR_VARIABLE version_error
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_STRIP_TRAILING_WHITESPACE
    )
    set(version_text "${version_output} ${version_error}")
  endif()
  set(${out_var} "${version_text}" PARENT_SCOPE)
endfunction()

function(verify_verilator_version version_text context)
  if(NOT version_text MATCHES "${expected_verilator_pattern}")
    message(FATAL_ERROR
      "${context}: expected '${verilator_bin} --version' to report Verilator ${expected_verilator_version}, "
      "got '${version_text}'"
    )
  endif()
endfunction()

read_verilator_version_text(current_verilator_text)

if(current_verilator_text)
  if(current_verilator_text MATCHES "${expected_verilator_pattern}")
    message(STATUS "Using pinned Verilator ${expected_verilator_version} at ${verilator_bin}: ${current_verilator_text}")
    return()
  endif()
  message(STATUS "Replacing existing Verilator at ${verilator_bin}; it is not pinned Verilator ${expected_verilator_version}.")
else()
  message(STATUS "Installing pinned Verilator ${expected_verilator_version} to ${verilator_prefix}.")
endif()

execute_process(
  COMMAND git -C "${VERILATOR_SOURCE_DIR}" rev-parse --verify HEAD
  OUTPUT_VARIABLE verilator_source_head
  ERROR_VARIABLE verilator_source_head_error
  RESULT_VARIABLE verilator_source_head_result
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE
)
if(NOT verilator_source_head_result EQUAL 0)
  message(FATAL_ERROR
    "thirdparty/verilator is not an initialized git submodule. "
    "Run 'git submodule update --init --recursive thirdparty/verilator', then rerun the build. "
    "${verilator_source_head_error}"
  )
endif()
if(NOT "${verilator_source_head}" STREQUAL "${expected_verilator_revision}")
  message(FATAL_ERROR
    "thirdparty/verilator is checked out at ${verilator_source_head}, but this project requires "
    "Verilator ${expected_verilator_version} at ${expected_verilator_revision}."
  )
endif()

file(MAKE_DIRECTORY "${VERILATOR_BUILD_DIR}")

if(EXISTS "${verilator_build_source_dir}/.git")
  execute_process(
    COMMAND git -C "${verilator_build_source_dir}" rev-parse --verify HEAD
    OUTPUT_VARIABLE verilator_worktree_head
    ERROR_VARIABLE verilator_worktree_head_error
    RESULT_VARIABLE verilator_worktree_head_result
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
  )
  if(NOT verilator_worktree_head_result EQUAL 0)
    message(FATAL_ERROR
      "Existing Verilator build worktree at ${verilator_build_source_dir} is not usable: "
      "${verilator_worktree_head_error}"
    )
  endif()
  if(NOT "${verilator_worktree_head}" STREQUAL "${expected_verilator_revision}")
    message(FATAL_ERROR
      "Existing Verilator build worktree at ${verilator_build_source_dir} is checked out at "
      "${verilator_worktree_head}, but ${expected_verilator_revision} is required. "
      "Use a fresh build directory."
    )
  endif()
else()
  if(EXISTS "${verilator_build_source_dir}")
    message(FATAL_ERROR
      "Verilator build source directory already exists but is not a git worktree: "
      "${verilator_build_source_dir}. Use a fresh build directory."
    )
  endif()
  execute_process(
    COMMAND git -C "${VERILATOR_SOURCE_DIR}" worktree add --detach
            "${verilator_build_source_dir}" "${expected_verilator_revision}"
    OUTPUT_VARIABLE worktree_output
    ERROR_VARIABLE worktree_error
    RESULT_VARIABLE worktree_result
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
  )
  if(NOT worktree_result EQUAL 0)
    message(FATAL_ERROR
      "Failed to create Verilator build worktree at ${verilator_build_source_dir}. "
      "${worktree_output} ${worktree_error}"
    )
  endif()
endif()

execute_process(
  COMMAND autoconf
  WORKING_DIRECTORY "${verilator_build_source_dir}"
  OUTPUT_VARIABLE autoconf_output
  ERROR_VARIABLE autoconf_error
  RESULT_VARIABLE autoconf_result
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE
)
if(NOT autoconf_result EQUAL 0)
  message(FATAL_ERROR
    "Failed to run autoconf for pinned Verilator ${expected_verilator_version} in "
    "${verilator_build_source_dir}. ${autoconf_output} ${autoconf_error}"
  )
endif()

execute_process(
  COMMAND "${verilator_build_source_dir}/configure" --prefix=${verilator_prefix}
  WORKING_DIRECTORY "${verilator_build_source_dir}"
  RESULT_VARIABLE configure_result
)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR "Failed to configure pinned Verilator ${expected_verilator_version} from ${VERILATOR_SOURCE_DIR}.")
endif()

execute_process(
  COMMAND make -j${verilator_jobs}
  WORKING_DIRECTORY "${verilator_build_source_dir}"
  RESULT_VARIABLE build_result
)
if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Failed to build pinned Verilator ${expected_verilator_version} from ${VERILATOR_SOURCE_DIR}.")
endif()

execute_process(
  COMMAND make install
  WORKING_DIRECTORY "${verilator_build_source_dir}"
  RESULT_VARIABLE install_result
)
if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "Failed to install pinned Verilator ${expected_verilator_version} to ${verilator_prefix}.")
endif()

read_verilator_version_text(installed_verilator_text)
verify_verilator_version("${installed_verilator_text}" "Pinned Verilator install")
message(STATUS "Installed pinned Verilator ${expected_verilator_version} at ${verilator_bin}: ${installed_verilator_text}")

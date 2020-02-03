
macro(group_sources_in source_dir curdir)
  file(GLOB children RELATIVE ${source_dir}/${curdir}
    ${source_dir}/${curdir}/*)
  foreach (child ${children})
    if (IS_DIRECTORY ${source_dir}/${curdir}/${child})
      group_sources_in(${source_dir} ${curdir}/${child})
    else()
      string(REPLACE "/" "\\" groupname ${curdir})
      source_group(${groupname} FILES
        ${source_dir}/${curdir}/${child})
    endif()
  endforeach()
endmacro()

macro(group_sources curdir)
  group_sources_in(${PROJECT_SOURCE_DIR} ${curdir})
endmacro()

macro (exclude_from_default target_)
  set_target_properties (${target_} PROPERTIES EXCLUDE_FROM_ALL ON)
  set_target_properties (${target_} PROPERTIES EXCLUDE_FROM_DEFAULT_BUILD ON)
endmacro ()

macro (exclude_if_included target_)
  get_directory_property(has_parent PARENT_DIRECTORY)
  if (has_parent)
    exclude_from_default (${target_})
  endif ()
endmacro ()

function (print_ep_logs _target)
  ExternalProject_Get_Property (${_target} STAMP_DIR)
  add_custom_command(TARGET ${_target} POST_BUILD
    COMMENT "${_target} BUILD OUTPUT"
    COMMAND ${CMAKE_COMMAND}
      -DIN_FILE=${STAMP_DIR}/${_target}-build-out.log
      -P ${CMAKE_SOURCE_DIR}/Builds/CMake/echo_file.cmake
    COMMAND ${CMAKE_COMMAND}
      -DIN_FILE=${STAMP_DIR}/${_target}-build-err.log
      -P ${CMAKE_SOURCE_DIR}/Builds/CMake/echo_file.cmake)
endfunction ()

#[=========================================================[
  This is a function override for one function in the
  standard ExternalProject module. We want to change
  the generated build script slightly to include printing
  the build logs in the case of failure. Those modifications
  have been made here. This function override could break
  in the future if the ExternalProject module changes internal
  function names or changes the way it generates the build
  scripts.
   See:
    https://gitlab.kitware.com/cmake/cmake/blob/df1ddeec128d68cc636f2dde6c2acd87af5658b6/Modules/ExternalProject.cmake#L1855-1952
#]=========================================================]

function(_ep_write_log_script name step cmd_var)
  ExternalProject_Get_Property(${name} stamp_dir)
  set(command "${${cmd_var}}")

  set(make "")
  set(code_cygpath_make "")
  if(command MATCHES "^\\$\\(MAKE\\)")
    # GNU make recognizes the string "$(MAKE)" as recursive make, so
    # ensure that it appears directly in the makefile.
    string(REGEX REPLACE "^\\$\\(MAKE\\)" "\${make}" command "${command}")
    set(make "-Dmake=$(MAKE)")

    if(WIN32 AND NOT CYGWIN)
      set(code_cygpath_make "
if(\${make} MATCHES \"^/\")
  execute_process(
    COMMAND cygpath -w \${make}
    OUTPUT_VARIABLE cygpath_make
    ERROR_VARIABLE cygpath_make
    RESULT_VARIABLE cygpath_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(NOT cygpath_error)
    set(make \${cygpath_make})
  endif()
endif()
")
    endif()
  endif()

  set(config "")
  if("${CMAKE_CFG_INTDIR}" MATCHES "^\\$")
    string(REPLACE "${CMAKE_CFG_INTDIR}" "\${config}" command "${command}")
    set(config "-Dconfig=${CMAKE_CFG_INTDIR}")
  endif()

  # Wrap multiple 'COMMAND' lines up into a second-level wrapper
  # script so all output can be sent to one log file.
  if(command MATCHES "(^|;)COMMAND;")
    set(code_execute_process "
${code_cygpath_make}
execute_process(COMMAND \${command} RESULT_VARIABLE result)
if(result)
  set(msg \"Command failed (\${result}):\\n\")
  foreach(arg IN LISTS command)
    set(msg \"\${msg} '\${arg}'\")
  endforeach()
  message(FATAL_ERROR \"\${msg}\")
endif()
")
    set(code "")
    set(cmd "")
    set(sep "")
    foreach(arg IN LISTS command)
      if("x${arg}" STREQUAL "xCOMMAND")
        if(NOT "x${cmd}" STREQUAL "x")
          string(APPEND code "set(command \"${cmd}\")${code_execute_process}")
        endif()
        set(cmd "")
        set(sep "")
      else()
        string(APPEND cmd "${sep}${arg}")
        set(sep ";")
      endif()
    endforeach()
    string(APPEND code "set(command \"${cmd}\")${code_execute_process}")
    file(GENERATE OUTPUT "${stamp_dir}/${name}-${step}-$<CONFIG>-impl.cmake" CONTENT "${code}")
    set(command ${CMAKE_COMMAND} "-Dmake=\${make}" "-Dconfig=\${config}" -P ${stamp_dir}/${name}-${step}-$<CONFIG>-impl.cmake)
  endif()

  # Wrap the command in a script to log output to files.
  set(script ${stamp_dir}/${name}-${step}-$<CONFIG>.cmake)
  set(logbase ${stamp_dir}/${name}-${step})
  set(code "
${code_cygpath_make}
function (_echo_file _fil)
  file (READ \${_fil} _cont)
  execute_process (COMMAND \${CMAKE_COMMAND} -E echo \"\${_cont}\")
endfunction ()
set(command \"${command}\")
execute_process(
  COMMAND \${command}
  RESULT_VARIABLE result
  OUTPUT_FILE \"${logbase}-out.log\"
  ERROR_FILE \"${logbase}-err.log\"
  )
if(result)
  set(msg \"Command failed: \${result}\\n\")
  foreach(arg IN LISTS command)
    set(msg \"\${msg} '\${arg}'\")
  endforeach()
  execute_process (COMMAND \${CMAKE_COMMAND} -E echo \"Build output for ${logbase} : \")
  _echo_file (\"${logbase}-out.log\")
  _echo_file (\"${logbase}-err.log\")
  set(msg \"\${msg}\\nSee above\\n\")
  message(FATAL_ERROR \"\${msg}\")
else()
  set(msg \"${name} ${step} command succeeded.  See also ${logbase}-*.log\")
  message(STATUS \"\${msg}\")
endif()
")
  file(GENERATE OUTPUT "${script}" CONTENT "${code}")
  set(command ${CMAKE_COMMAND} ${make} ${config} -P ${script})
  set(${cmd_var} "${command}" PARENT_SCOPE)
endfunction()

find_package(Git)

# function that calls git log to get current hash
function (git_hash hash_val)
  # note: optional second extra string argument not in signature
  if (NOT GIT_FOUND)
    return ()
  endif ()
  set (_hash "")
  set (_format "%H")
  if (ARGC GREATER_EQUAL 2)
    string (TOLOWER ${ARGV1} _short)
    if (_short STREQUAL "short")
      set (_format "%h")
    endif ()
  endif ()
  execute_process (COMMAND ${GIT_EXECUTABLE} "log" "--pretty=${_format}" "-n1"
                   WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                   RESULT_VARIABLE _git_exit_code
                   OUTPUT_VARIABLE _temp_hash
                   OUTPUT_STRIP_TRAILING_WHITESPACE
                   ERROR_QUIET)
  if (_git_exit_code EQUAL 0)
    set (_hash ${_temp_hash})
  endif ()
  set (${hash_val} "${_hash}" PARENT_SCOPE)
endfunction ()

function (git_branch branch_val)
  if (NOT GIT_FOUND)
    return ()
  endif ()
  set (_branch "")
  execute_process (COMMAND ${GIT_EXECUTABLE} "rev-parse" "--abbrev-ref" "HEAD"
                   WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                   RESULT_VARIABLE _git_exit_code
                   OUTPUT_VARIABLE _temp_branch
                   OUTPUT_STRIP_TRAILING_WHITESPACE
                   ERROR_QUIET)
  if (_git_exit_code EQUAL 0)
    set (_branch ${_temp_branch})
  endif ()
  set (${branch_val} "${_branch}" PARENT_SCOPE)
endfunction ()


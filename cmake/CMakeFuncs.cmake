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

find_package(Git)

function (git_branch branch_val)
  if (NOT GIT_FOUND)
    return ()
  endif ()
  set (_branch "")
  execute_process (COMMAND ${GIT_EXECUTABLE} "rev-parse" "--abbrev-ref" "HEAD"
                   WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                   RESULT_VARIABLE _git_exit_code
                   OUTPUT_VARIABLE _temp_branch
                   OUTPUT_STRIP_TRAILING_WHITESPACE
                   ERROR_QUIET)
  if (_git_exit_code EQUAL 0)
    set (_branch ${_temp_branch})
  endif ()
  set (${branch_val} "${_branch}" PARENT_SCOPE)
endfunction ()

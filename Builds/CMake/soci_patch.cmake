# This patches unsigned-types.h in the soci official sources
# so as to remove type range check exceptions that cause
# us trouble when using boost::optional to select int values

# Soci's CMake setup leaves flags in place that will cause warnings to
# be treated as errors, but some compiler versions throw "new" warnings
# that then cause the build to fail. Simplify that until soci fixes
# those warnings.
if (RIPPLED_SOURCE)
  execute_process( COMMAND ${CMAKE_COMMAND} -E copy_if_different
      ${RIPPLED_SOURCE}/Builds/CMake/SociConfig.cmake.patched
      cmake/SociConfig.cmake )
endif ()

# Some versions of CMake erroneously patch external projects on every build.
# If the patch makes no changes, skip it. This workaround can be
# removed once we stop supporting vulnerable versions of CMake.
# https://gitlab.kitware.com/cmake/cmake/-/issues/21086
file (STRINGS include/soci/unsigned-types.h sourcecode)
# Delete the .patched file if it exists, so it doesn't end up duplicated.
# Trying to remove a file that does not exist is not a problem.
file (REMOVE include/soci/unsigned-types.h.patched)
foreach (line_ ${sourcecode})
  if (line_ MATCHES "^[ \\t]+throw[ ]+soci_error[ ]*\\([ ]*\"Value outside of allowed.+$")
    set (line_ "//${CMAKE_MATCH_0}")
  endif ()
  file (APPEND include/soci/unsigned-types.h.patched "${line_}\n")
endforeach ()
execute_process( COMMAND ${CMAKE_COMMAND} -E compare_files
                 include/soci/unsigned-types.h include/soci/unsigned-types.h.patched
                 RESULT_VARIABLE compare_result
)
if( compare_result EQUAL 0)
  message(DEBUG "The soci source and patch files are identical. Make no changes.")
  file (REMOVE include/soci/unsigned-types.h.patched)
  return()
endif()
file (RENAME include/soci/unsigned-types.h include/soci/unsigned-types.h.orig)
file (RENAME include/soci/unsigned-types.h.patched include/soci/unsigned-types.h)
# also fix Boost.cmake so that it just returns when we override the Boost_FOUND var
file (APPEND cmake/dependencies/Boost.cmake.patched "if (Boost_FOUND)\n")
file (APPEND cmake/dependencies/Boost.cmake.patched "  return ()\n")
file (APPEND cmake/dependencies/Boost.cmake.patched "endif ()\n")
file (STRINGS cmake/dependencies/Boost.cmake sourcecode)
foreach (line_ ${sourcecode})
  file (APPEND cmake/dependencies/Boost.cmake.patched "${line_}\n")
endforeach ()
file (RENAME cmake/dependencies/Boost.cmake.patched cmake/dependencies/Boost.cmake)


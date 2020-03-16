#[===================================================================[
   docs target (optional)
#]===================================================================]

find_package (Doxygen)
if (NOT TARGET Doxygen::doxygen)
  message (STATUS "doxygen executable not found -- skipping docs target")
  return ()
endif ()

set (doxygen_output_directory "${CMAKE_BINARY_DIR}/html_doc")
set (doxygen_index_file "${doxygen_output_directory}/index.html")
set (doxyfile_in "${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile")
set (doxyfile_out "${CMAKE_BINARY_DIR}/Doxyfile")

file (GLOB_RECURSE doxygen_input
  docs/*.md
  src/ripple/*.h
  src/ripple/*.md
  src/test/*.h
  src/test/*.md
  Builds/*/README.md)
list (APPEND doxygen_input
  README.md
  RELEASENOTES.md
  src/README.md)
set (dependencies "${doxygen_input}" docs/Doxyfile)

# Substitute doxygen_output_directory.
# TODO: Generate this file at build time, not configure time.
configure_file ("${doxyfile_in}" "${doxyfile_out}" @ONLY)

add_custom_command (
  OUTPUT "${doxygen_index_file}"
  COMMAND "${DOXYGEN_EXECUTABLE}" "${doxyfile_out}"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/docs"
  DEPENDS "${dependencies}")
add_custom_target (docs
  DEPENDS "${doxygen_index_file}"
  SOURCES "${dependencies}")
if (is_multiconfig)
  set_property (
    SOURCE ${dependencies}
    APPEND PROPERTY
    HEADER_FILE_ONLY true)
endif ()

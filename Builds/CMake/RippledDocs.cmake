#[===================================================================[
   docs target (optional)
#]===================================================================]

find_package (Doxygen)
if (NOT TARGET Doxygen::doxygen)
  message (STATUS "doxygen executable not found -- skipping docs target")
  return ()
endif ()

set (doxygen_output_directory "${CMAKE_BINARY_DIR}/docs/html")
set (doxygen_index_file "${doxygen_output_directory}/index.html")
set (doxyfile_in "${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile")
set (doxyfile_out "${CMAKE_BINARY_DIR}/docs/Doxyfile")

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

# https://en.cppreference.com/w/Cppreference:Archives
# https://stackoverflow.com/questions/60822559/how-to-move-a-file-download-from-configure-step-to-build-step
set (download_script "${CMAKE_BINARY_DIR}/docs/download-cppreference.cmake")
file (WRITE
  "${download_script}"
  "file (DOWNLOAD \
    http://upload.cppreference.com/mwiki/images/b/b2/html_book_20190607.zip \
    ${CMAKE_BINARY_DIR}/docs/cppreference.zip \
    EXPECTED_HASH MD5=82b3a612d7d35a83e3cb1195a63689ab \
  )\n \
  execute_process ( \
    COMMAND \"${CMAKE_COMMAND}\" -E tar -xf cppreference.zip \
  )\n"
)
set (tagfile "${CMAKE_BINARY_DIR}/docs/cppreference-doxygen-web.tag.xml")
add_custom_command (
  OUTPUT "${tagfile}"
  COMMAND "${CMAKE_COMMAND}" -P "${download_script}"
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/docs"
)
set (doxygen_tag_files "${tagfile}=http://en.cppreference.com/w/")

# Substitute doxygen_output_directory.
# TODO: Generate this file at build time, not configure time.
configure_file ("${doxyfile_in}" "${doxyfile_out}" @ONLY)

add_custom_command (
  OUTPUT "${doxygen_index_file}"
  COMMAND "${DOXYGEN_EXECUTABLE}" "${doxyfile_out}"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  DEPENDS "${dependencies}" "${tagfile}")
add_custom_target (docs
  DEPENDS "${doxygen_index_file}"
  SOURCES "${dependencies}")
if (is_multiconfig)
  set_property (
    SOURCE ${dependencies}
    APPEND PROPERTY
    HEADER_FILE_ONLY true)
endif ()

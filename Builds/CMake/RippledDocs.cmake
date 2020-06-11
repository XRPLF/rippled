#[===================================================================[
   docs target (optional)
#]===================================================================]

find_package (Doxygen)
if (NOT TARGET Doxygen::doxygen)
  message (STATUS "doxygen executable not found -- skipping docs target")
  return ()
endif ()

set (doxygen_output_directory "${CMAKE_BINARY_DIR}/docs")
set (doxygen_include_path "${CMAKE_CURRENT_SOURCE_DIR}/src")
set (doxygen_index_file "${doxygen_output_directory}/html/index.html")
set (doxyfile "${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile")

file (GLOB_RECURSE doxygen_input
  docs/*.md
  src/ripple/*.h
  src/ripple/*.cpp
  src/ripple/*.md
  src/test/*.h
  src/test/*.md
  Builds/*/README.md)
list (APPEND doxygen_input
  README.md
  RELEASENOTES.md
  src/README.md)
set (dependencies "${doxygen_input}" "${doxyfile}")

function (verbose_find_path variable name)
  # find_path sets a CACHE variable, so don't try using a "local" variable.
  find_path (${variable} "${name}" ${ARGN})
  if (NOT ${variable})
    message (WARNING "could not find ${name}")
  else ()
    message (STATUS "found ${name}: ${${variable}}/${name}")
  endif ()
endfunction ()

verbose_find_path (doxygen_plantuml_jar_path plantuml.jar PATH_SUFFIXES share/plantuml)
verbose_find_path (doxygen_dot_path dot)

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
set (doxygen_tagfiles "${tagfile}=http://en.cppreference.com/w/")

add_custom_command (
  OUTPUT "${doxygen_index_file}"
  COMMAND "${CMAKE_COMMAND}" -E env
    "DOXYGEN_OUTPUT_DIRECTORY=${doxygen_output_directory}"
    "DOXYGEN_INCLUDE_PATH=${doxygen_include_path}"
    "DOXYGEN_TAGFILES=${doxygen_tagfiles}"
    "DOXYGEN_PLANTUML_JAR_PATH=${doxygen_plantuml_jar_path}"
    "DOXYGEN_DOT_PATH=${doxygen_dot_path}"
    "${DOXYGEN_EXECUTABLE}" "${doxyfile}"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  DEPENDS "${dependencies}" "${tagfile}")
add_custom_target (docs
  DEPENDS "${doxygen_index_file}"
  SOURCES "${dependencies}")

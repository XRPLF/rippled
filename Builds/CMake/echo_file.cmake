#[=========================================================[
  This is a CMake script file that is used to write
  the contents of a file to stdout (using the cmake
  echo command). The input file is passed via the
  IN_FILE variable.
#]=========================================================]

file (READ ${IN_FILE} contents)
## only print files that actually have some text in them
if (contents MATCHES "[a-z0-9A-Z]+")
  execute_process(
    COMMAND
      ${CMAKE_COMMAND} -E echo "${contents}")
endif ()


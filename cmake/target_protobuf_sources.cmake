find_package(Protobuf REQUIRED)

# .proto files import each other like this:
#
# import "path/to/file.proto";
#
# For the protobuf compiler to find these imports,
# the parent directory of "path" must be in the import path.
#
# When generating C++,
# it turns into an include statement like this:
#
# #include "path/to/file.pb.h"
#
# and the header is generated at a path relative to the output directory
# that matches the given .proto path relative to the source directory
# minus the first matching prefix on the import path.
#
# In other words, a file `include/package/path/to/file.proto`
# with import path [`include/package`, `include`]
# will generate files `output/path/to/file.pb.{h,cc}`
# with includes like `#include "path/to/file.pb.h".
#
# During build, the generated files can find each other if the output
# directory is an include directory, but we want to install that directory
# under our package's include directory (`include/package`), not as a sibling.
# After install, they can find each other if that subdirectory is an include
# directory.

# Add protocol buffer sources to an existing library target.
# target:
#     The name of the library target.
# prefix:
#     The install prefix for headers relative to `CMAKE_INSTALL_INCLUDEDIR`.
#     This prefix should appear at the start of all your consumer includes.
# ARGN:
#     A list of .proto files.
function(target_protobuf_sources target prefix)
  set(dir "${CMAKE_CURRENT_BINARY_DIR}/pb-${target}")
  file(MAKE_DIRECTORY "${dir}/${prefix}")

  protobuf_generate(
    TARGET ${target}
    PROTOC_OUT_DIR "${dir}/${prefix}"
    "${ARGN}"
  )
  target_include_directories(${target} SYSTEM PUBLIC
    # Allows #include <package/path/to/file.proto> used by consumer files.
    $<BUILD_INTERFACE:${dir}>
    # Allows #include "path/to/file.proto" used by generated files.
    $<BUILD_INTERFACE:${dir}/${prefix}>
    # Allows #include <package/path/to/file.proto> used by consumer files.
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    # Allows #include "path/to/file.proto" used by generated files.
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${prefix}>
  )
  install(
    DIRECTORY ${dir}/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
  )
endfunction()

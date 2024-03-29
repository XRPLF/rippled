# Consider include directory B nested under prefix A:
#
#     /path/to/A/then/to/B/...
#
# Call C the relative path from A to B.
# C is what we want to write in `#include` directives:
#
#     #include <then/to/B/...>
#
# Examples, all from the `jobqueue` module:
#
#   - Library public headers:
#     B = /include/xrpl/jobqueue
#     A = /include/
#     C = xrpl/jobqueue
#
#   - Library private headers:
#     B = /src/libxrpl/jobqueue
#     A = /src/
#     C = libxrpl/jobqueue
#
#   - Test private headers:
#     B = /tests/jobqueue
#     A = /
#     C = tests/jobqueue
#
# To isolate headers from each other,
# we want to create a symlink Y that points to B,
# within a subdirectory X of the `CMAKE_BINARY_DIR`,
# that has the same relative path C between X and Y,
# and then add X as an include directory of the target,
# sometimes `PUBLIC` and sometimes `PRIVATE`.
# The Cs are all guaranteed to be unique.
# We can guarantee a unique X per target by using
# `${CMAKE_CURRENT_BINARY_DIR}/include/${target}`.
#
# isolate_headers(target A B scope)
function(isolate_headers target A B scope)
  file(RELATIVE_PATH C "${A}" "${B}")
  set(X "${CMAKE_CURRENT_BINARY_DIR}/include/${target}")
  set(Y "${X}/${C}")
  cmake_path(GET Y PARENT_PATH parent)
  file(MAKE_DIRECTORY "${parent}")
  file(CREATE_LINK "${B}" "${Y}" SYMBOLIC)
  target_include_directories(${target} ${scope} "$<BUILD_INTERFACE:${X}>")
endfunction()

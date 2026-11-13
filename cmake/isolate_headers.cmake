include(create_symbolic_link)

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
#     B = /src/tests/libxrpl/jobqueue
#     A = /src/
#     C = tests/libxrpl/jobqueue
#
# To isolate headers from each other,
# we want to create a symlink Y that points to B,
# within a subdirectory X of the `CMAKE_BINARY_DIR`,
# that has the same relative path C between X and Y,
# and then add X as an include directory of the target,
# sometimes `PUBLIC` and sometimes `PRIVATE`.
# The Cs are all guaranteed to be unique.
# We can guarantee a unique X per target by using
# `${CMAKE_CURRENT_BINARY_DIR}/modules/${target}`.
#
# A must be a directory of this project, derived from PROJECT_SOURCE_DIR or
# CMAKE_CURRENT_SOURCE_DIR, never from CMAKE_SOURCE_DIR. When xrpld is added
# to another project with add_subdirectory, CMAKE_SOURCE_DIR is that project's
# root, so C would climb out of X and the symlink would land outside it.
#
# isolate_headers(target A B scope)
function(isolate_headers target A B scope)
    file(RELATIVE_PATH C "${A}" "${B}")
    # Keep in sync with ExcludeHeaderFilterRegex in .clang-tidy.
    set(X "${CMAKE_CURRENT_BINARY_DIR}/modules/${target}")
    set(Y "${X}/${C}")
    cmake_path(GET Y PARENT_PATH parent)
    file(MAKE_DIRECTORY "${parent}")
    create_symbolic_link("${B}" "${Y}")
    target_include_directories(${target} ${scope} "$<BUILD_INTERFACE:${X}>")
endfunction()

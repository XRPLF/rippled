include(isolate_headers)

# Define a benchmark executable for the module `name`.
#
# This mirrors `xrpl_add_test` (see XrplAddTest.cmake), but the target is
# prefixed `bench` instead of `test`, and no `add_test(...)` is registered -
# benchmarks are slow and run on demand, not as part of `ctest`.
#
# `isolate_headers` exposes only `src/benchmarks/${name}` on the include path,
# rooted at `src`, so a benchmark's own headers are reached as
# `<benchmarks/${name}/...>` and nothing else in the tree leaks in.
function(xrpl_add_benchmark name)
    set(target ${PROJECT_NAME}.bench.${name})

    file(
        GLOB_RECURSE sources
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/${name}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/${name}.cpp"
    )
    add_executable(${target} ${ARGN} ${sources})

    isolate_headers(
        ${target}
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/${name}"
        PRIVATE
    )
endfunction()

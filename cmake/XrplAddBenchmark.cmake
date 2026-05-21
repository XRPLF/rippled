include(isolate_headers)

# Define a benchmark executable for the module `name`.
#
# This mirrors `xrpl_add_test` (see XrplAddTest.cmake) with two differences:
#   - the target is prefixed `bench` instead of `test`;
#   - no `add_test(...)` is registered - benchmarks are slow and run on demand,
#     not as part of `ctest`.
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
        "${CMAKE_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/benchmarks/${name}"
        PRIVATE
    )
endfunction()

include_guard()

include(isolate_headers)

# Define a benchmark executable for the module `name`.
#
# This follows the same general pattern as other build helpers in this repo
# (e.g. `add_module`): create a target and isolate headers, but here the target
# is a benchmark executable and no `add_test(...)` is registered.
#
# `isolate_headers` exposes only `${CMAKE_CURRENT_SOURCE_DIR}/${name}` on the
# include path, rooted at `src`, so a benchmark's own headers are reached as
# `<benchmarks/.../${name}/...>` and nothing else in the tree leaks in.
function(xrpl_add_benchmark name)
    set(target ${PROJECT_NAME}.bench.${name})

    file(
        GLOB_RECURSE sources
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/${name}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/${name}.cpp"
    )
    add_executable(${target} ${ARGN} ${sources})

    # Benchmark sources register cases through Google Benchmark's static
    # registrars (anonymous-namespace lambdas). Merging several such files into
    # one unity translation unit collides those internal-linkage entities, so
    # keep benchmarks out of the unity build - mirroring xrpl.libpb in
    # XrplCore.cmake. Each file compiles fine on its own.
    set_target_properties(${target} PROPERTIES UNITY_BUILD OFF)

    isolate_headers(
        ${target}
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/${name}"
        PRIVATE
    )
endfunction()

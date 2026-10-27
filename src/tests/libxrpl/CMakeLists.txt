include(GoogleTest)
include(isolate_headers)
include(verify_headers)

# Test requirements.
find_package(GTest REQUIRED)

# A single gtest binary, xrpl_tests, built from one object library per source
# directory below. Each directory's headers are reachable only through its own
# target, as <tests/libxrpl/<dir>/...>, so a test that needs another directory's
# headers has to link that directory's target: the dependencies between test
# directories are declared in this file and nowhere else, the same way the
# libxrpl modules declare theirs. The shared `helpers` directory is available
# to every other directory without declaring it.
#
# The object libraries link their dependencies only to compile against them.
# Their object files go into the binary as sources, so the binary links each
# library exactly once; linking the object libraries themselves would repeat
# every dependency on the link line, which the Apple linker rejects.
add_executable(xrpl_tests main.cpp)
patch_nix_binary(xrpl_tests)
set_target_properties(
    xrpl_tests
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
target_link_libraries(xrpl_tests PRIVATE GTest::gtest GTest::gmock xrpl.libxrpl)
if(rust)
    target_link_libraries(xrpl_tests PRIVATE rs_hello_world_cxxbridge)
endif()

# Compiles ${CMAKE_CURRENT_SOURCE_DIR}/${name} into the object library
# xrpl_tests.${name}, exposes its headers under their canonical path, verifies
# them against that target, and adds the objects to xrpl_tests.
function(xrpl_add_test_dir name)
    set(target xrpl_tests.${name})
    file(
        GLOB_RECURSE sources
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/${name}/*.cpp"
    )
    if(NOT rust)
        # Tests of the Rust interop include generated cxxbridge headers, which
        # do not exist without the crates, so keep them out of the build tree
        # entirely. They are named `Rust<something>.cpp`.
        list(FILTER sources EXCLUDE REGEX "/Rust[^/]*\\.cpp$")
    endif()
    add_library(${target} OBJECT ${sources})
    target_link_libraries(
        ${target}
        PRIVATE GTest::gtest GTest::gmock xrpl.libxrpl
    )
    if(NOT name STREQUAL "helpers")
        target_link_libraries(${target} PRIVATE xrpl_tests.helpers)
    endif()
    isolate_headers(
        ${target}
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/${name}"
        PUBLIC
    )
    # The test headers are not built with add_module, so verify them against
    # their own target's compile environment.
    if(verify_headers)
        verify_target_headers(${target} "${CMAKE_CURRENT_SOURCE_DIR}/${name}")
    endif()
    target_sources(xrpl_tests PRIVATE $<TARGET_OBJECTS:${target}>)
endfunction()

# The shared helpers first, since every other directory links them.
xrpl_add_test_dir(helpers)

# The CSF (Consensus Simulation Framework), used by the consensus tests. The
# consensus engine itself lives in libxrpl, so no xrpld sources or include paths
# are needed here.
xrpl_add_test_dir(csf)

# One source subdirectory per module. Network unit tests are currently not
# supported on Windows.
set(test_modules
    basics
    beast
    consensus
    crypto
    json
    ledger
    nodestore
    peerfinder
    protocol
    resource
    shamap
    tx
    protocol_autogen
    server
)
if(NOT WIN32)
    list(APPEND test_modules net)
endif()
foreach(module IN LISTS test_modules)
    xrpl_add_test_dir(${module})
endforeach()

# Dependencies between test directories, beyond the shared helpers.
target_link_libraries(xrpl_tests.consensus PRIVATE xrpl_tests.csf)
if(rust)
    target_link_libraries(xrpl_tests.basics PRIVATE rs_hello_world_cxxbridge)
endif()

gtest_discover_tests(xrpl_tests DISCOVERY_TIMEOUT 60)

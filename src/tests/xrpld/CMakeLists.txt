include(GoogleTest)
include(isolate_headers)

find_package(GTest REQUIRED)

# kept separate from xrpl_tests so that the libxrpl test binary stays free of server code.
add_executable(xrpld_tests main.cpp)
patch_nix_binary(xrpld_tests)
set_target_properties(
    xrpld_tests
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
target_include_directories(
    xrpld_tests
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} "${CMAKE_SOURCE_DIR}/src"
)
# Lets tests include the shared helpers of xrpl_tests as <helpers/...>.
# Only that directory is exposed, not the rest of src/tests/libxrpl. The
# helpers these tests use are header-only, so no helper source is built in.
isolate_headers(
    xrpld_tests
    "${CMAKE_SOURCE_DIR}/src/tests/libxrpl"
    "${CMAKE_SOURCE_DIR}/src/tests/libxrpl/helpers"
    PRIVATE
)
target_link_libraries(
    xrpld_tests
    PRIVATE GTest::gtest GTest::gmock xrpl.libxrpl rpcspec::rpcspec
)

set(test_modules rpc ledger telemetry)

foreach(module IN LISTS test_modules)
    file(
        GLOB_RECURSE sources
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/${module}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/${module}.cpp"
    )
    target_sources(xrpld_tests PRIVATE ${sources})
endforeach()

gtest_discover_tests(xrpld_tests DISCOVERY_TIMEOUT 60)

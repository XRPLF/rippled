include(GoogleTest)

find_package(GTest REQUIRED)

# kept separate from xrpl_tests so that the libxrpl test binary stays free of server code.
add_executable(xrpld_tests main.cpp)
patch_nix_binary(xrpld_tests)
set_target_properties(
    xrpld_tests
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
# The server headers are not a library, so the whole src tree is on the include
# path. That also makes this binary's own test headers available under their
# canonical path, <tests/xrpld/<module>/...>, like the libxrpl tests.
target_include_directories(xrpld_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(
    xrpld_tests
    PRIVATE GTest::gtest GTest::gmock xrpl.libxrpl rpcspec::rpcspec
)

set(test_modules rpc)

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

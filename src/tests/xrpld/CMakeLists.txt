include(GoogleTest)

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
target_link_libraries(
    xrpld_tests
    PRIVATE GTest::gtest GTest::gmock xrpl.libxrpl rpcspec::rpcspec
)

set(test_modules rpc telemetry)

foreach(module IN LISTS test_modules)
    file(
        GLOB_RECURSE sources
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/${module}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/${module}.cpp"
    )
    target_sources(xrpld_tests PRIVATE ${sources})
endforeach()

# ValidationTracker is server code with no library of its own: xrpld builds
# its one source file directly. Build it into this binary too, so its tests
# can call it. The class has no telemetry guards, so this is unconditional.
target_sources(
    xrpld_tests
    PRIVATE ${CMAKE_SOURCE_DIR}/src/xrpld/telemetry/detail/ValidationTracker.cpp
)

gtest_discover_tests(xrpld_tests DISCOVERY_TIMEOUT 60)

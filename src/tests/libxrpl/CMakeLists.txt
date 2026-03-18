include(XrplAddTest)

# Test requirements.
find_package(GTest REQUIRED)

# Custom target for all tests defined in this file
add_custom_target(xrpl.tests)

# Test helpers
add_library(xrpl.helpers.test STATIC)
target_sources(xrpl.helpers.test PRIVATE helpers/TestSink.cpp)
target_include_directories(xrpl.helpers.test PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(xrpl.helpers.test PRIVATE xrpl.libxrpl)

# Common library dependencies for the rest of the tests.
add_library(xrpl.imports.test INTERFACE)
target_link_libraries(
    xrpl.imports.test
    INTERFACE gtest::gtest xrpl.libxrpl xrpl.helpers.test
)

# One test for each module.
xrpl_add_test(basics)
target_link_libraries(xrpl.test.basics PRIVATE xrpl.imports.test)
add_dependencies(xrpl.tests xrpl.test.basics)

xrpl_add_test(crypto)
target_link_libraries(xrpl.test.crypto PRIVATE xrpl.imports.test)
add_dependencies(xrpl.tests xrpl.test.crypto)

xrpl_add_test(json)
target_link_libraries(xrpl.test.json PRIVATE xrpl.imports.test)
add_dependencies(xrpl.tests xrpl.test.json)

# protocol_autogen tests use explicit source list (not GLOB) because sources are generated
# Mark generated sources so CMake knows they'll be created at build time
set_source_files_properties(
    ${PROTOCOL_AUTOGEN_TEST_SOURCES}
    PROPERTIES GENERATED TRUE
)
add_executable(xrpl.test.protocol_autogen ${PROTOCOL_AUTOGEN_TEST_SOURCES})
target_link_libraries(xrpl.test.protocol_autogen PRIVATE xrpl.imports.test)
add_dependencies(xrpl.tests xrpl.test.protocol_autogen)
add_test(NAME xrpl.test.protocol_autogen COMMAND xrpl.test.protocol_autogen)
# Ensure code generation runs before compiling tests
if(TARGET protocol_autogen_generate)
    add_dependencies(xrpl.test.protocol_autogen protocol_autogen_generate)
endif()

# Network unit tests are currently not supported on Windows
if(NOT WIN32)
    xrpl_add_test(net)
    target_link_libraries(xrpl.test.net PRIVATE xrpl.imports.test)
    add_dependencies(xrpl.tests xrpl.test.net)
endif()

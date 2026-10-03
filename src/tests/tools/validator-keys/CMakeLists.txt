find_package(GTest REQUIRED)

add_executable(
    validator_keys_tests
    Commands.cpp
    ListSigning.cpp
    SigningKeys.cpp
    "${CMAKE_SOURCE_DIR}/src/tests/libxrpl/main.cpp"
)
patch_nix_binary(validator_keys_tests)
set_target_properties(
    validator_keys_tests
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
target_include_directories(
    validator_keys_tests
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(
    validator_keys_tests
    PRIVATE GTest::gtest GTest::gmock xrpl.validator-keys
)
include(GoogleTest)
gtest_discover_tests(validator_keys_tests)

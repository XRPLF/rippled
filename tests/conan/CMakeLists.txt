cmake_minimum_required(VERSION 3.21)

set(name validator-keys-conan-test)
set(version 0.1.0)

project(${name} VERSION ${version} LANGUAGES CXX)

find_package(xrpl CONFIG REQUIRED)

# Build the in-repo validator-keys-tool source instead of fetching it from
# GitHub. Keep it out of the default build; the test recipe builds the target
# explicitly.
add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../validator-keys-tool
    ${CMAKE_BINARY_DIR}/validator-keys-tool
    EXCLUDE_FROM_ALL
)

set_target_properties(
    validator-keys
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

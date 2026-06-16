cmake_minimum_required(VERSION 3.11)
project(validator-keys-tool)

#[===========================================[
  This project is built as part of the rippled
  repository's Conan test package. The parent
  project calls find_package(xrpl) and adds this
  directory, providing the xrpl::libxrpl target.
#]===========================================]
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(NOT TARGET xrpl::libxrpl)
    find_package(xrpl CONFIG REQUIRED)
endif()

include(KeysSanity)
include(KeysCov)
include(KeysInterface)

add_executable(
    validator-keys
    src/ValidatorKeys.cpp
    src/ValidatorKeysTool.cpp
    # UNIT TESTS:
    src/test/ValidatorKeys_test.cpp
    src/test/ValidatorKeysTool_test.cpp
)
target_include_directories(validator-keys PRIVATE src)
target_link_libraries(validator-keys xrpl::libxrpl Keys::opts)

include(CTest)
if(BUILD_TESTING)
    add_test(test validator-keys --unittest)
endif()

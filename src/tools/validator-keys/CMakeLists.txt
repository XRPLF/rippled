# The validator-keys tool: validator and publisher key files, manifests,
# tokens, revocations and validator-list signing. Built only when the
# validator_keys option is ON (see cmake/XrplValidatorKeys.cmake).
include(GNUInstallDirs)

add_library(xrpl.validator-keys STATIC)
target_sources(
    xrpl.validator-keys
    PRIVATE Commands.cpp ListSigning.cpp OwnerOnlyFile.cpp SigningKeys.cpp
)
target_include_directories(
    xrpl.validator-keys
    PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
)
target_link_libraries(
    xrpl.validator-keys
    PUBLIC Xrpl::boost Xrpl::opts Xrpl::libs xrpl.libxrpl
)

add_executable(validator-keys Main.cpp)
target_link_libraries(validator-keys PRIVATE xrpl.validator-keys)
patch_nix_binary(validator-keys)
set_target_properties(
    validator-keys
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    "${CMAKE_BINARY_DIR}/validator-keys-LICENSE"
    COPYONLY
)
install(TARGETS validator-keys RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

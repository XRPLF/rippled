# The validator-keys tool: validator and publisher key files, manifests,
# tokens, revocations and validator-list signing. Built only when the
# validator_keys option is ON (see cmake/XrplValidatorKeys.cmake).
add_executable(validator-keys)
target_sources(
    validator-keys
    PRIVATE
        ListSigning.cpp
        SigningKeys.cpp
        ValidatorKeysTool.cpp
        # Unit tests run with `validator-keys --unittest`.
        test/ListSigning_test.cpp
        test/SigningKeys_test.cpp
        test/ValidatorKeysTool_test.cpp
)
target_include_directories(
    validator-keys
    PRIVATE $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
)
target_link_libraries(
    validator-keys
    PRIVATE Xrpl::boost Xrpl::opts Xrpl::libs xrpl.libxrpl
)

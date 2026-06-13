# Links the Lean 4 formal-verification FFI into xrpld when the
# formal_verification_tests option is on (default OFF).
# See docs/formal-verification/README.md for the full picture.

if(NOT formal_verification_tests)
    return()
endif()

if(NOT TARGET xrpld OR NOT tests)
    message(
        FATAL_ERROR
        "formal_verification_tests=ON requires xrpld and tests to be enabled"
    )
endif()

# Pulls in xrpl-lean4-deps and lean4 transitively (see the Conan recipe).
find_package(xrpl-lean4 REQUIRED)

message(STATUS "Lean FFI: enabled (formal_verification_tests=ON)")
target_link_libraries(xrpld xrpl-lean4::xrpl-lean4)

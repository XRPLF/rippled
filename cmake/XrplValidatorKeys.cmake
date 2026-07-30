option(
    validator_keys
    "Enables building of validator-keys tool as a separate target (imported via FetchContent)"
    OFF
)

if(validator_keys)
    # Pinned to an exact commit, not a branch: the tool ships inside our
    # packages, so the same xrpld version must always package the same
    # validator-keys. Bump this deliberately.
    set(validator_keys_commit "4c0fb75eec9601c711645998c904507e87e910ae")
    message(STATUS "Using ValidatorKeys commit: ${validator_keys_commit}")

    FetchContent_Declare(
        validator_keys
        GIT_REPOSITORY https://github.com/ripple/validator-keys-tool.git
        GIT_TAG "${validator_keys_commit}"
    )
    FetchContent_MakeAvailable(validator_keys)
    # The tool's own CMakeLists excludes the target from 'all' when it is built
    # as a subproject. Undo that, so validator_keys=ON really does build it.
    set_target_properties(
        validator-keys
        PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
            EXCLUDE_FROM_ALL OFF
            EXCLUDE_FROM_DEFAULT_BUILD OFF
    )
    # We ship this binary, so like xrpld it must not keep the Nix store's ELF
    # loader, or it cannot run on the target distro at all.
    patch_nix_binary(validator-keys)
    install(TARGETS validator-keys RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

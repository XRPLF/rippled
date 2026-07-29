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
    # as a subproject. Undo that: the packages ship validator-keys, so a build
    # configured with validator_keys=ON must produce it as part of the default
    # target, next to xrpld.
    set_target_properties(
        validator-keys
        PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
            EXCLUDE_FROM_ALL OFF
            EXCLUDE_FROM_DEFAULT_BUILD OFF
    )
    # Like xrpld, this binary leaves the Nix-based build image (we ship it in
    # the deb/rpm), so it needs the system ELF loader instead of the one in the
    # Nix store. Without this it cannot run on the target distro at all.
    patch_nix_binary(validator-keys)
    install(TARGETS validator-keys RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

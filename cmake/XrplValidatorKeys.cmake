option(
    validator_keys
    "Enables building of the validator-keys tool as a separate target"
    OFF
)

if(validator_keys)
    # Own the install destination below rather than relying on another module
    # having pulled this in first.
    include(GNUInstallDirs)

    add_subdirectory(src/tools/validator-keys)
    set_target_properties(
        validator-keys
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
    # We ship this binary, so like xrpld it must not keep the Nix store's ELF
    # loader, or it cannot run on the target distro at all.
    patch_nix_binary(validator-keys)

    configure_file(
        "${CMAKE_SOURCE_DIR}/src/tools/validator-keys/LICENSE"
        "${CMAKE_BINARY_DIR}/validator-keys-LICENSE"
        COPYONLY
    )
    install(TARGETS validator-keys RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

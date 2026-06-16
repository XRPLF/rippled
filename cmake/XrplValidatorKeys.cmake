option(
    validator_keys
    "Enables building of the vendored validator-keys tool as a separate target"
    OFF
)

if(validator_keys)
    include(GNUInstallDirs)

    add_subdirectory(
        "${CMAKE_SOURCE_DIR}/validator-keys-tool"
        "${CMAKE_BINARY_DIR}/validator-keys-tool"
    )
    set_target_properties(
        validator-keys
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
    install(
        TARGETS validator-keys
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT runtime
    )
endif()

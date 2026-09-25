option(
    validator_keys
    "Enables building of the validator-keys tool as a separate target"
    OFF
)

if(validator_keys)
    add_subdirectory(src/tools/validator-keys)
    if(tests)
        add_subdirectory(src/tests/tools/validator-keys)
    endif()
endif()

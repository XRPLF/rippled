option(
    validator_keys
    "Enables building of the validator-keys tool as a separate target"
    OFF
)

if(validator_keys)
    if(WIN32)
        message(
            FATAL_ERROR
            "validator_keys is not supported on Windows: key files are written with POSIX mkstemp and rename"
        )
    endif()
    add_subdirectory(src/tools/validator-keys)
endif()

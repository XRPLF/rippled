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
    # The tool's suites in xrpld's unit tests; plain link signature as XrplCore uses for xrpld.
    if(tests AND xrpld)
        target_sources(
            xrpld
            PRIVATE
                "${CMAKE_SOURCE_DIR}/src/test/tools/ValidatorKeysTool_test.cpp"
        )
        target_link_libraries(xrpld xrpl.validator-keys)
    endif()
endif()

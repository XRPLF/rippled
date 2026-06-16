#[===================================================================[
   coverage report target

   Copied from rippled https://github.com/ripple/rippled/blob/develop/Builds/CMake/RippledCov.cmake
#]===================================================================]

# cspell: words xcrun

if(coverage)
    if(is_clang)
        if(APPLE)
            execute_process(
                COMMAND xcrun -f llvm-profdata
                OUTPUT_VARIABLE LLVM_PROFDATA
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
        else()
            find_program(LLVM_PROFDATA llvm-profdata)
        endif()
        if(NOT LLVM_PROFDATA)
            message(
                WARNING
                "unable to find llvm-profdata - skipping coverage_report target"
            )
        endif()

        if(APPLE)
            execute_process(
                COMMAND xcrun -f llvm-cov
                OUTPUT_VARIABLE LLVM_COV
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
        else()
            find_program(LLVM_COV llvm-cov)
        endif()
        if(NOT LLVM_COV)
            message(
                WARNING
                "unable to find llvm-cov - skipping coverage_report target"
            )
        endif()

        set(extract_pattern "")
        if(coverage_core_only)
            set(extract_pattern "${CMAKE_CURRENT_SOURCE_DIR}/src/")
        endif()

        if(LLVM_COV AND LLVM_PROFDATA)
            add_custom_target(
                coverage_report
                USES_TERMINAL
                COMMAND
                    ${CMAKE_COMMAND} -E echo
                    "Generating coverage - results will be in ${CMAKE_BINARY_DIR}/coverage/index.html."
                COMMAND ${CMAKE_COMMAND} -E echo "Running validator-keys tests."
                COMMAND
                    validator-keys
                    --unittest$<$<BOOL:${coverage_test}>:=${coverage_test}>
                COMMAND
                    ${LLVM_PROFDATA} merge -sparse default.profraw -o
                    rip.profdata
                COMMAND ${CMAKE_COMMAND} -E echo "Summary of coverage:"
                COMMAND
                    ${LLVM_COV} report -instr-profile=rip.profdata
                    $<TARGET_FILE:validator-keys> ${extract_pattern}
                # generate html report
                COMMAND
                    ${LLVM_COV} show -format=html
                    -output-dir=${CMAKE_BINARY_DIR}/coverage
                    -instr-profile=rip.profdata $<TARGET_FILE:validator-keys>
                    ${extract_pattern}
                BYPRODUCTS coverage/index.html
            )
        endif()
    elseif(is_gcc)
        find_program(LCOV lcov)
        if(NOT LCOV)
            message(
                WARNING
                "unable to find lcov - skipping coverage_report target"
            )
        endif()

        find_program(GENHTML genhtml)
        if(NOT GENHTML)
            message(
                WARNING
                "unable to find genhtml - skipping coverage_report target"
            )
        endif()

        set(extract_pattern "*")
        if(coverage_core_only)
            set(extract_pattern "*/src/*")
        endif()

        if(LCOV AND GENHTML)
            add_custom_target(
                coverage_report
                USES_TERMINAL
                COMMAND
                    ${CMAKE_COMMAND} -E echo
                    "Generating coverage- results will be in ${CMAKE_BINARY_DIR}/coverage/index.html."
                # create baseline info file
                COMMAND
                    ${LCOV} --no-external -d "${CMAKE_CURRENT_SOURCE_DIR}" -c -d
                    . -i -o baseline.info | grep -v
                    "ignoring data for external file"
                # run tests
                COMMAND
                    ${CMAKE_COMMAND} -E echo
                    "Running validator-keys tests for coverage report."
                COMMAND
                    validator-keys
                    --unittest$<$<BOOL:${coverage_test}>:=${coverage_test}>
                # Create test coverage data file
                COMMAND
                    ${LCOV} --no-external -d "${CMAKE_CURRENT_SOURCE_DIR}" -c -d
                    . -o tests.info | grep -v "ignoring data for external file"
                # Combine baseline and test coverage data
                COMMAND ${LCOV} -a baseline.info -a tests.info -o lcov-all.info
                # extract our files
                COMMAND
                    ${LCOV} -e lcov-all.info "${extract_pattern}" -o lcov.info
                COMMAND ${CMAKE_COMMAND} -E echo "Summary of coverage:"
                COMMAND ${LCOV} --summary lcov.info
                # generate HTML report
                COMMAND ${GENHTML} -o ${CMAKE_BINARY_DIR}/coverage lcov.info
                BYPRODUCTS coverage/index.html
            )
        endif()
    else()
        message(STATUS "Coverage: neither clang nor gcc")
    endif()
else()
    message(STATUS "Coverage disabled")
endif()

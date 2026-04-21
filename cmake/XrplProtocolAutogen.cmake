#[===================================================================[
   Protocol Autogen - Code generation for protocol wrapper classes
#]===================================================================]

# Options for code generation
option(
    XRPL_NO_CODEGEN
    "Disable code generation (use pre-generated files from repository)"
    OFF
)
set(CODEGEN_VENV_DIR
    ""
    CACHE PATH
    "Path to Python virtual environment for code generation. If provided, automatic venv setup is skipped."
)

# Function to set up code generation for protocol_autogen module
# This runs at configure time to generate C++ wrapper classes from macro files
function(setup_protocol_autogen)
    # Directory paths
    set(MACRO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include/xrpl/protocol/detail")
    set(AUTOGEN_HEADER_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/include/xrpl/protocol_autogen"
    )
    set(AUTOGEN_TEST_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/src/tests/libxrpl/protocol_autogen"
    )
    set(SCRIPTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/scripts")

    # Input macro files
    set(TRANSACTIONS_MACRO "${MACRO_DIR}/transactions.macro")
    set(LEDGER_ENTRIES_MACRO "${MACRO_DIR}/ledger_entries.macro")
    set(SFIELDS_MACRO "${MACRO_DIR}/sfields.macro")

    # Python scripts and templates
    set(GENERATE_TX_SCRIPT "${SCRIPTS_DIR}/generate_tx_classes.py")
    set(GENERATE_LEDGER_SCRIPT "${SCRIPTS_DIR}/generate_ledger_classes.py")
    set(REQUIREMENTS_FILE "${SCRIPTS_DIR}/requirements.txt")
    set(MACRO_PARSER_COMMON "${SCRIPTS_DIR}/macro_parser_common.py")
    set(TX_TEMPLATE "${SCRIPTS_DIR}/templates/Transaction.h.mako")
    set(TX_TEST_TEMPLATE "${SCRIPTS_DIR}/templates/TransactionTests.cpp.mako")
    set(LEDGER_TEMPLATE "${SCRIPTS_DIR}/templates/LedgerEntry.h.mako")
    set(LEDGER_TEST_TEMPLATE
        "${SCRIPTS_DIR}/templates/LedgerEntryTests.cpp.mako"
    )

    # Check if code generation is disabled
    if(XRPL_NO_CODEGEN)
        message(
            WARNING
            "Protocol autogen: Code generation is disabled (XRPL_NO_CODEGEN=ON). "
            "Generated files may be out of date."
        )
        return()
    endif()

    # Create output directories
    file(MAKE_DIRECTORY "${AUTOGEN_HEADER_DIR}/transactions")
    file(MAKE_DIRECTORY "${AUTOGEN_HEADER_DIR}/ledger_entries")
    file(MAKE_DIRECTORY "${AUTOGEN_TEST_DIR}/ledger_entries")
    file(MAKE_DIRECTORY "${AUTOGEN_TEST_DIR}/transactions")

    # Find Python3 - check if already found by Conan or find it ourselves
    if(NOT Python3_EXECUTABLE)
        find_package(Python3 COMPONENTS Interpreter QUIET)
    endif()

    if(NOT Python3_EXECUTABLE)
        # Try finding python3 executable directly
        find_program(Python3_EXECUTABLE NAMES python3 python)
    endif()

    if(NOT Python3_EXECUTABLE)
        message(
            FATAL_ERROR
            "Python3 not found. Code generation cannot proceed.\n"
            "Please install Python 3, or set -DXRPL_NO_CODEGEN=ON to use existing generated files."
        )
        return()
    endif()

    message(STATUS "Using Python3 for code generation: ${Python3_EXECUTABLE}")

    # Set up Python virtual environment for code generation
    if(CODEGEN_VENV_DIR)
        # User-provided venv - skip automatic setup
        set(VENV_DIR "${CODEGEN_VENV_DIR}")
        message(STATUS "Using user-provided Python venv: ${VENV_DIR}")
    else()
        # Use default venv in build directory
        set(VENV_DIR "${CMAKE_CURRENT_BINARY_DIR}/codegen_venv")
    endif()

    # Determine the Python executable path in the venv
    if(WIN32)
        set(VENV_PYTHON "${VENV_DIR}/Scripts/python.exe")
        set(VENV_PIP "${VENV_DIR}/Scripts/pip.exe")
    else()
        set(VENV_PYTHON "${VENV_DIR}/bin/python")
        set(VENV_PIP "${VENV_DIR}/bin/pip")
    endif()

    # Only auto-setup venv if not user-provided
    if(NOT CODEGEN_VENV_DIR)
        # Check if venv needs to be created or updated
        set(VENV_NEEDS_UPDATE FALSE)
        if(NOT EXISTS "${VENV_PYTHON}")
            set(VENV_NEEDS_UPDATE TRUE)
            message(
                STATUS
                "Creating Python virtual environment for code generation..."
            )
        elseif(
            "${REQUIREMENTS_FILE}"
                IS_NEWER_THAN
                "${VENV_DIR}/.requirements_installed"
        )
            set(VENV_NEEDS_UPDATE TRUE)
            message(
                STATUS
                "Updating Python virtual environment (requirements changed)..."
            )
        endif()

        # Create/update virtual environment if needed
        if(VENV_NEEDS_UPDATE)
            message(
                STATUS
                "Setting up Python virtual environment at ${VENV_DIR}"
            )
            execute_process(
                COMMAND ${Python3_EXECUTABLE} -m venv "${VENV_DIR}"
                RESULT_VARIABLE VENV_RESULT
                ERROR_VARIABLE VENV_ERROR
            )
            if(NOT VENV_RESULT EQUAL 0)
                message(
                    FATAL_ERROR
                    "Failed to create virtual environment: ${VENV_ERROR}"
                )
            endif()

            # Check pip index URL configuration
            execute_process(
                COMMAND ${VENV_PIP} config get global.index-url
                OUTPUT_VARIABLE PIP_INDEX_URL
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )

            # Default PyPI URL
            set(DEFAULT_PIP_INDEX "https://pypi.org/simple")

            # Show warning if using non-default index
            if(PIP_INDEX_URL AND NOT PIP_INDEX_URL STREQUAL "")
                if(NOT PIP_INDEX_URL STREQUAL DEFAULT_PIP_INDEX)
                    message(
                        WARNING
                        "Private pip index URL detected: ${PIP_INDEX_URL}\n"
                        "You may need to connect to VPN to access this URL."
                    )
                endif()
            endif()

            message(STATUS "Installing Python dependencies...")
            execute_process(
                COMMAND ${VENV_PIP} install --upgrade pip
                RESULT_VARIABLE PIP_UPGRADE_RESULT
                OUTPUT_QUIET
                ERROR_VARIABLE PIP_UPGRADE_ERROR
            )
            if(NOT PIP_UPGRADE_RESULT EQUAL 0)
                message(WARNING "Failed to upgrade pip: ${PIP_UPGRADE_ERROR}")
            endif()

            execute_process(
                COMMAND ${VENV_PIP} install -r "${REQUIREMENTS_FILE}"
                RESULT_VARIABLE PIP_INSTALL_RESULT
                ERROR_VARIABLE PIP_INSTALL_ERROR
            )
            if(NOT PIP_INSTALL_RESULT EQUAL 0)
                message(
                    FATAL_ERROR
                    "Failed to install Python dependencies: ${PIP_INSTALL_ERROR}"
                )
            endif()

            # Mark requirements as installed
            file(TOUCH "${VENV_DIR}/.requirements_installed")
            message(STATUS "Python virtual environment ready")
        endif()
    endif()

    # At configure time - get list of output files for transactions
    execute_process(
        COMMAND
            ${VENV_PYTHON} "${GENERATE_TX_SCRIPT}" "${TRANSACTIONS_MACRO}"
            --header-dir "${AUTOGEN_HEADER_DIR}/transactions" --test-dir
            "${AUTOGEN_TEST_DIR}/transactions" --list-outputs
        OUTPUT_VARIABLE TX_OUTPUT_FILES
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE TX_LIST_RESULT
        ERROR_VARIABLE TX_LIST_ERROR
    )
    if(NOT TX_LIST_RESULT EQUAL 0)
        message(
            FATAL_ERROR
            "Failed to list transaction output files:\n${TX_LIST_ERROR}"
        )
    endif()
    # Convert newline-separated list to CMake list
    string(REPLACE "\\" "/" TX_OUTPUT_FILES "${TX_OUTPUT_FILES}")
    string(REPLACE "\n" ";" TX_OUTPUT_FILES "${TX_OUTPUT_FILES}")

    # At configure time - get list of output files for ledger entries
    execute_process(
        COMMAND
            ${VENV_PYTHON} "${GENERATE_LEDGER_SCRIPT}" "${LEDGER_ENTRIES_MACRO}"
            --header-dir "${AUTOGEN_HEADER_DIR}/ledger_entries" --test-dir
            "${AUTOGEN_TEST_DIR}/ledger_entries" --list-outputs
        OUTPUT_VARIABLE LEDGER_OUTPUT_FILES
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE LEDGER_LIST_RESULT
        ERROR_VARIABLE LEDGER_LIST_ERROR
    )
    if(NOT LEDGER_LIST_RESULT EQUAL 0)
        message(
            FATAL_ERROR
            "Failed to list ledger entry output files:\n${LEDGER_LIST_ERROR}"
        )
    endif()
    # Convert newline-separated list to CMake list
    string(REPLACE "\\" "/" LEDGER_OUTPUT_FILES "${LEDGER_OUTPUT_FILES}")
    string(REPLACE "\n" ";" LEDGER_OUTPUT_FILES "${LEDGER_OUTPUT_FILES}")

    # Custom command to generate transaction classes at build time
    add_custom_command(
        OUTPUT ${TX_OUTPUT_FILES}
        COMMAND
            ${VENV_PYTHON} "${GENERATE_TX_SCRIPT}" "${TRANSACTIONS_MACRO}"
            --header-dir "${AUTOGEN_HEADER_DIR}/transactions" --test-dir
            "${AUTOGEN_TEST_DIR}/transactions" --sfields-macro
            "${SFIELDS_MACRO}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        DEPENDS
            "${TRANSACTIONS_MACRO}"
            "${SFIELDS_MACRO}"
            "${GENERATE_TX_SCRIPT}"
            "${MACRO_PARSER_COMMON}"
            "${TX_TEMPLATE}"
            "${TX_TEST_TEMPLATE}"
            "${REQUIREMENTS_FILE}"
        COMMENT "Generating transaction classes from transactions.macro..."
        VERBATIM
    )

    # Custom command to generate ledger entry classes at build time
    add_custom_command(
        OUTPUT ${LEDGER_OUTPUT_FILES}
        COMMAND
            ${VENV_PYTHON} "${GENERATE_LEDGER_SCRIPT}" "${LEDGER_ENTRIES_MACRO}"
            --header-dir "${AUTOGEN_HEADER_DIR}/ledger_entries" --test-dir
            "${AUTOGEN_TEST_DIR}/ledger_entries" --sfields-macro
            "${SFIELDS_MACRO}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        DEPENDS
            "${LEDGER_ENTRIES_MACRO}"
            "${SFIELDS_MACRO}"
            "${GENERATE_LEDGER_SCRIPT}"
            "${MACRO_PARSER_COMMON}"
            "${LEDGER_TEMPLATE}"
            "${LEDGER_TEST_TEMPLATE}"
            "${REQUIREMENTS_FILE}"
        COMMENT "Generating ledger entry classes from ledger_entries.macro..."
        VERBATIM
    )

    # Create a custom target that depends on all generated files
    add_custom_target(
        protocol_autogen_generate
        DEPENDS ${TX_OUTPUT_FILES} ${LEDGER_OUTPUT_FILES}
        COMMENT "Protocol autogen code generation"
    )

    # Extract test files from output lists (files ending in Tests.cpp)
    set(PROTOCOL_AUTOGEN_TEST_SOURCES "")
    foreach(FILE ${TX_OUTPUT_FILES} ${LEDGER_OUTPUT_FILES})
        if(FILE MATCHES "Tests\\.cpp$")
            list(APPEND PROTOCOL_AUTOGEN_TEST_SOURCES "${FILE}")
        endif()
    endforeach()
    # Export test sources to parent scope for use in test CMakeLists.txt
    set(PROTOCOL_AUTOGEN_TEST_SOURCES
        "${PROTOCOL_AUTOGEN_TEST_SOURCES}"
        CACHE INTERNAL
        "Generated protocol_autogen test sources"
    )

    # Register dependencies so CMake reconfigures when macro files change
    # (to update the list of output files)
    set_property(
        DIRECTORY
        APPEND
        PROPERTY
            CMAKE_CONFIGURE_DEPENDS
                "${TRANSACTIONS_MACRO}"
                "${LEDGER_ENTRIES_MACRO}"
    )
endfunction()

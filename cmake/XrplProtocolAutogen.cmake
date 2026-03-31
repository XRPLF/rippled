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
function(setup_protocol_autogen)
    # Directory paths
    set(MACRO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include/xrpl/protocol/detail")
    set(AUTOGEN_HEADER_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/include/xrpl/protocol_autogen"
    )
    set(AUTOGEN_TEST_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/src/tests/libxrpl/protocol_autogen"
    )
    set(SCRIPTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/scripts/codegen")

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
    set(UPDATE_STAMP_SCRIPT "${SCRIPTS_DIR}/update_codegen_stamp.py")

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

    # === Stamp file check ===
    # All input files whose content affects code generation output.
    set(STAMP_FILE "${CMAKE_CURRENT_SOURCE_DIR}/scripts/codegen/.codegen_stamp")
    set(ALL_INPUT_FILES
        "${TRANSACTIONS_MACRO}"
        "${LEDGER_ENTRIES_MACRO}"
        "${SFIELDS_MACRO}"
        "${GENERATE_TX_SCRIPT}"
        "${GENERATE_LEDGER_SCRIPT}"
        "${REQUIREMENTS_FILE}"
        "${MACRO_PARSER_COMMON}"
        "${TX_TEMPLATE}"
        "${TX_TEST_TEMPLATE}"
        "${LEDGER_TEMPLATE}"
        "${LEDGER_TEST_TEMPLATE}"
    )

    # Tell CMake to reconfigure automatically when any input file changes.
    # The reconfigure itself is cheap — it runs the stamp check below
    # which only invokes stdlib Python (no venv needed).
    set_property(
        DIRECTORY
        APPEND
        PROPERTY CMAKE_CONFIGURE_DEPENDS ${ALL_INPUT_FILES}
    )

    # Find Python3 (needed for stamp check; no venv required).
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

    # Check whether the stamp is up-to-date (stdlib-only, no venv).
    execute_process(
        COMMAND
            ${Python3_EXECUTABLE} "${UPDATE_STAMP_SCRIPT}" --check
            "${STAMP_FILE}" ${ALL_INPUT_FILES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE STAMP_CHECK_RESULT
    )

    # ------------------------------------------------------------------
    # Fast path: stamp matches — generated files are up to date.
    # ------------------------------------------------------------------
    if(STAMP_CHECK_RESULT EQUAL 0)
        message(
            STATUS
            "Protocol autogen: inputs unchanged (stamp matches), skipping generation"
        )
        return()
    endif()

    # ------------------------------------------------------------------
    # Slow path: stamp mismatch — run generation at configure time.
    # ------------------------------------------------------------------
    message(
        STATUS
        "Protocol autogen: inputs changed, running code generation..."
    )

    # Set up Python virtual environment for code generation.
    if(CODEGEN_VENV_DIR)
        # User-provided venv - skip automatic setup.
        set(VENV_DIR "${CODEGEN_VENV_DIR}")
        message(STATUS "Using user-provided Python venv: ${VENV_DIR}")
    else()
        # Use default venv in build directory.
        set(VENV_DIR "${CMAKE_CURRENT_BINARY_DIR}/codegen_venv")
    endif()

    # Determine the Python/pip executables inside the venv.
    if(WIN32)
        set(VENV_PYTHON "${VENV_DIR}/Scripts/python.exe")
        set(VENV_PIP "${VENV_DIR}/Scripts/pip.exe")
    else()
        set(VENV_PYTHON "${VENV_DIR}/bin/python")
        set(VENV_PIP "${VENV_DIR}/bin/pip")
    endif()

    # Create or update the virtual environment if needed.
    if(NOT CODEGEN_VENV_DIR)
        # Check if venv needs to be created or updated.
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

        # Create/update virtual environment if needed.
        if(VENV_NEEDS_UPDATE)
            # Create the venv.
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

            # Warn if pip is configured with a non-default index (may need VPN).
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

            # Install dependencies.
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

            # Mark requirements as installed.
            file(TOUCH "${VENV_DIR}/.requirements_installed")
            message(STATUS "Python virtual environment ready")
        endif()
    endif()

    # Generate transaction classes.
    execute_process(
        COMMAND
            ${VENV_PYTHON} "${GENERATE_TX_SCRIPT}" "${TRANSACTIONS_MACRO}"
            --header-dir "${AUTOGEN_HEADER_DIR}/transactions" --test-dir
            "${AUTOGEN_TEST_DIR}/transactions" --sfields-macro
            "${SFIELDS_MACRO}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE TX_RESULT
        ERROR_VARIABLE TX_ERROR
    )
    if(NOT TX_RESULT EQUAL 0)
        message(FATAL_ERROR "Transaction code generation failed:\n${TX_ERROR}")
    endif()

    # Generate ledger entry classes.
    execute_process(
        COMMAND
            ${VENV_PYTHON} "${GENERATE_LEDGER_SCRIPT}" "${LEDGER_ENTRIES_MACRO}"
            --header-dir "${AUTOGEN_HEADER_DIR}/ledger_entries" --test-dir
            "${AUTOGEN_TEST_DIR}/ledger_entries" --sfields-macro
            "${SFIELDS_MACRO}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE LEDGER_RESULT
        ERROR_VARIABLE LEDGER_ERROR
    )
    if(NOT LEDGER_RESULT EQUAL 0)
        message(
            FATAL_ERROR
            "Ledger entry code generation failed:\n${LEDGER_ERROR}"
        )
    endif()

    # Update the stamp file so subsequent configures skip generation.
    execute_process(
        COMMAND
            ${Python3_EXECUTABLE} "${UPDATE_STAMP_SCRIPT}" --update
            "${STAMP_FILE}" ${ALL_INPUT_FILES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE STAMP_RESULT
    )
    if(NOT STAMP_RESULT EQUAL 0)
        message(WARNING "Failed to update codegen stamp file")
    endif()

    message(STATUS "Protocol autogen: code generation complete")
endfunction()

#[===================================================================[
   Protocol Autogen - Code generation for protocol wrapper classes
#]===================================================================]

# Options for code generation
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

    # Python scripts
    set(GENERATE_TX_SCRIPT "${SCRIPTS_DIR}/generate_tx_classes.py")
    set(GENERATE_LEDGER_SCRIPT "${SCRIPTS_DIR}/generate_ledger_classes.py")
    set(REQUIREMENTS_FILE "${SCRIPTS_DIR}/requirements.txt")

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
            "Python3 not found. Code generation cannot proceed."
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

    # Generate transaction classes and tests at configure time
    message(STATUS "Generating transaction classes from transactions.macro...")
    execute_process(
        COMMAND
            ${VENV_PYTHON} "${GENERATE_TX_SCRIPT}" "${TRANSACTIONS_MACRO}"
            --header-dir "${AUTOGEN_HEADER_DIR}/transactions" --test-dir
            "${AUTOGEN_TEST_DIR}/transactions" --sfields-macro
            "${SFIELDS_MACRO}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE TX_GEN_RESULT
        OUTPUT_VARIABLE TX_GEN_OUTPUT
        ERROR_VARIABLE TX_GEN_ERROR
    )
    if(NOT TX_GEN_RESULT EQUAL 0)
        message(
            FATAL_ERROR
            "Failed to generate transaction classes:\n${TX_GEN_ERROR}"
        )
    else()
        message(STATUS "Transaction classes generated successfully")
    endif()

    # Generate ledger entry classes and tests at configure time
    message(
        STATUS
        "Generating ledger entry classes from ledger_entries.macro..."
    )
    execute_process(
        COMMAND
            ${VENV_PYTHON} "${GENERATE_LEDGER_SCRIPT}" "${LEDGER_ENTRIES_MACRO}"
            --header-dir "${AUTOGEN_HEADER_DIR}/ledger_entries" --test-dir
            "${AUTOGEN_TEST_DIR}/ledger_entries" --sfields-macro
            "${SFIELDS_MACRO}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE LEDGER_GEN_RESULT
        OUTPUT_VARIABLE LEDGER_GEN_OUTPUT
        ERROR_VARIABLE LEDGER_GEN_ERROR
    )
    if(NOT LEDGER_GEN_RESULT EQUAL 0)
        message(
            FATAL_ERROR
            "Failed to generate ledger entry classes:\n${LEDGER_GEN_ERROR}"
        )
    else()
        message(STATUS "Ledger entry classes generated successfully")
    endif()

    # Format generated files using pre-commit (clang-format)
    if(WIN32)
        set(VENV_PRECOMMIT "${VENV_DIR}/Scripts/pre-commit.exe")
    else()
        set(VENV_PRECOMMIT "${VENV_DIR}/bin/pre-commit")
    endif()

    file(
        GLOB GENERATED_FILES
        "${AUTOGEN_HEADER_DIR}/transactions/*.h"
        "${AUTOGEN_HEADER_DIR}/ledger_entries/*.h"
        "${AUTOGEN_TEST_DIR}/transactions/*.cpp"
        "${AUTOGEN_TEST_DIR}/ledger_entries/*.cpp"
    )

    if(GENERATED_FILES)
        message(STATUS "Formatting generated files with clang-format...")
        execute_process(
            COMMAND
                ${VENV_PRECOMMIT} run clang-format --files ${GENERATED_FILES}
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            RESULT_VARIABLE FORMAT_RESULT
            OUTPUT_VARIABLE FORMAT_OUTPUT
            ERROR_VARIABLE FORMAT_ERROR
        )
        if(NOT FORMAT_RESULT EQUAL 0)
            message(
                WARNING
                "Failed to format generated files:\n${FORMAT_ERROR}"
            )
        else()
            message(STATUS "Generated files formatted successfully")
        endif()
    endif()

    # Register dependencies so CMake reconfigures when these files change
    set_property(
        DIRECTORY
        APPEND
        PROPERTY
            CMAKE_CONFIGURE_DEPENDS
                "${TRANSACTIONS_MACRO}"
                "${LEDGER_ENTRIES_MACRO}"
                "${SFIELDS_MACRO}"
                "${GENERATE_TX_SCRIPT}"
                "${GENERATE_LEDGER_SCRIPT}"
                "${SCRIPTS_DIR}/macro_parser_common.py"
                "${SCRIPTS_DIR}/templates/Transaction.h.mako"
                "${SCRIPTS_DIR}/templates/TransactionTests.cpp.mako"
                "${SCRIPTS_DIR}/templates/LedgerEntry.h.mako"
                "${SCRIPTS_DIR}/templates/LedgerEntryTests.cpp.mako"
                "${REQUIREMENTS_FILE}"
    )
endfunction()

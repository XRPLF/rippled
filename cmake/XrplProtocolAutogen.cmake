#[===================================================================[
   Protocol Autogen - Code generation for protocol wrapper classes
#]===================================================================]

set(CODEGEN_VENV_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/.venv"
    CACHE PATH
    "Path to a Python virtual environment for code generation. A venv will be created here by setup_code_gen and used to run generation scripts."
)

# Directory paths
set(MACRO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include/xrpl/protocol/detail")
set(AUTOGEN_HEADER_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/include/xrpl/protocol_autogen"
)
set(AUTOGEN_TEST_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/src/tests/libxrpl/protocol_autogen"
)
set(SCRIPTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake/scripts/codegen")

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
set(LEDGER_TEST_TEMPLATE "${SCRIPTS_DIR}/templates/LedgerEntryTests.cpp.mako")
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

# Create output directories
file(MAKE_DIRECTORY "${AUTOGEN_HEADER_DIR}/transactions")
file(MAKE_DIRECTORY "${AUTOGEN_HEADER_DIR}/ledger_entries")
file(MAKE_DIRECTORY "${AUTOGEN_TEST_DIR}/ledger_entries")
file(MAKE_DIRECTORY "${AUTOGEN_TEST_DIR}/transactions")

# Find Python3
if(NOT Python3_EXECUTABLE)
    find_package(Python3 COMPONENTS Interpreter QUIET)
endif()

if(NOT Python3_EXECUTABLE)
    find_program(Python3_EXECUTABLE NAMES python3 python)
endif()

if(NOT Python3_EXECUTABLE)
    message(
        WARNING
        "Python3 not found. The 'code_gen' and 'setup_code_gen' targets will not be available."
    )
    return()
endif()

# Warn if pip is configured with a non-default index (may need VPN).
execute_process(
    COMMAND ${Python3_EXECUTABLE} -m pip config get global.index-url
    OUTPUT_VARIABLE PIP_INDEX_URL
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE PIP_CONFIG_RESULT
)
if(PIP_CONFIG_RESULT EQUAL 0 AND PIP_INDEX_URL)
    if(
        NOT PIP_INDEX_URL STREQUAL "https://pypi.org/simple"
        AND NOT PIP_INDEX_URL STREQUAL "https://pypi.python.org/simple"
    )
        message(
            WARNING
            "Private pip index URL detected: ${PIP_INDEX_URL}\n"
            "You may need to connect to VPN to access this URL."
        )
    endif()
endif()

# Determine which Python interpreter to use for code generation.
if(CODEGEN_VENV_DIR)
    if(WIN32)
        set(CODEGEN_PYTHON "${CODEGEN_VENV_DIR}/Scripts/python.exe")
    else()
        set(CODEGEN_PYTHON "${CODEGEN_VENV_DIR}/bin/python")
    endif()
else()
    set(CODEGEN_PYTHON "${Python3_EXECUTABLE}")
    message(
        WARNING
        "CODEGEN_VENV_DIR is not set. Dependencies will be installed globally.\n"
        "If this is not intended, reconfigure with:\n"
        "  cmake . -UCODEGEN_VENV_DIR"
    )
endif()

# Custom target to create a venv and install Python dependencies.
# Run manually with: cmake --build . --target setup_code_gen
if(CODEGEN_VENV_DIR)
    add_custom_target(
        setup_code_gen
        COMMAND ${Python3_EXECUTABLE} -m venv "${CODEGEN_VENV_DIR}"
        COMMAND ${CODEGEN_PYTHON} -m pip install -r "${REQUIREMENTS_FILE}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Creating venv and installing code generation dependencies..."
    )
else()
    add_custom_target(
        setup_code_gen
        COMMAND ${Python3_EXECUTABLE} -m pip install -r "${REQUIREMENTS_FILE}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Installing code generation dependencies..."
    )
endif()

# Custom target for code generation, excluded from ALL.
# Run manually with: cmake --build . --target code_gen
add_custom_target(
    code_gen
    COMMAND
        ${CMAKE_COMMAND} -DCODEGEN_PYTHON=${CODEGEN_PYTHON}
        -DGENERATE_TX_SCRIPT=${GENERATE_TX_SCRIPT}
        -DGENERATE_LEDGER_SCRIPT=${GENERATE_LEDGER_SCRIPT}
        -DTRANSACTIONS_MACRO=${TRANSACTIONS_MACRO}
        -DLEDGER_ENTRIES_MACRO=${LEDGER_ENTRIES_MACRO}
        -DSFIELDS_MACRO=${SFIELDS_MACRO}
        -DAUTOGEN_HEADER_DIR=${AUTOGEN_HEADER_DIR}
        -DAUTOGEN_TEST_DIR=${AUTOGEN_TEST_DIR} -P
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/XrplProtocolAutogenRun.cmake"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Running protocol code generation..."
    SOURCES ${ALL_INPUT_FILES}
)

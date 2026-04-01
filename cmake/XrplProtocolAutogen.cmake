#[===================================================================[
   Protocol Autogen - Code generation for protocol wrapper classes
#]===================================================================]

set(CODEGEN_VENV_DIR
    ""
    CACHE PATH
    "Path to a Python virtual environment for code generation. If provided, dependencies will be installed into this venv. Otherwise, dependencies will be installed directly."
)

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
        "Python3 not found. The 'codegen' target will not be available."
    )
    return()
endif()

# Custom target for code generation, excluded from ALL.
# Run manually with: cmake --build . --target codegen
add_custom_target(
    codegen
    COMMAND
        ${CMAKE_COMMAND} -DPYTHON3_EXECUTABLE=${Python3_EXECUTABLE}
        -DCODEGEN_VENV_DIR=${CODEGEN_VENV_DIR}
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

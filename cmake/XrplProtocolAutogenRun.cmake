#[===================================================================[
   Protocol Autogen - Run script invoked by the 'codegen' target
#]===================================================================]

if(CODEGEN_VENV_DIR)
    set(VENV_DIR_ARGS --venv-dir "${CODEGEN_VENV_DIR}")
else()
    set(VENV_DIR_ARGS)
endif()

# Generate transaction classes.
execute_process(
    COMMAND
        ${PYTHON3_EXECUTABLE} "${GENERATE_TX_SCRIPT}" "${TRANSACTIONS_MACRO}"
        --header-dir "${AUTOGEN_HEADER_DIR}/transactions" --test-dir
        "${AUTOGEN_TEST_DIR}/transactions" --sfields-macro "${SFIELDS_MACRO}"
        ${VENV_DIR_ARGS}
    RESULT_VARIABLE TX_RESULT
    ERROR_VARIABLE TX_ERROR
)
if(NOT TX_RESULT EQUAL 0)
    message(FATAL_ERROR "Transaction code generation failed:\n${TX_ERROR}")
endif()

# Generate ledger entry classes.
execute_process(
    COMMAND
        ${PYTHON3_EXECUTABLE} "${GENERATE_LEDGER_SCRIPT}"
        "${LEDGER_ENTRIES_MACRO}" --header-dir
        "${AUTOGEN_HEADER_DIR}/ledger_entries" --test-dir
        "${AUTOGEN_TEST_DIR}/ledger_entries" --sfields-macro "${SFIELDS_MACRO}"
        ${VENV_DIR_ARGS}
    RESULT_VARIABLE LEDGER_RESULT
    ERROR_VARIABLE LEDGER_ERROR
)
if(NOT LEDGER_RESULT EQUAL 0)
    message(FATAL_ERROR "Ledger entry code generation failed:\n${LEDGER_ERROR}")
endif()

message(STATUS "Protocol autogen: code generation complete")

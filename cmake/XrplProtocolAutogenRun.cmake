#[===================================================================[
   Protocol Autogen - Run script invoked by the 'code_gen' target
#]===================================================================]

# Generate transaction classes.
execute_process(
    COMMAND
        ${CODEGEN_PYTHON} "${GENERATE_TX_SCRIPT}" "${TRANSACTIONS_MACRO}"
        --header-dir "${AUTOGEN_HEADER_DIR}/transactions" --test-dir
        "${AUTOGEN_TEST_DIR}/transactions" --sfields-macro "${SFIELDS_MACRO}"
    RESULT_VARIABLE TX_RESULT
    OUTPUT_VARIABLE TX_OUTPUT
    ERROR_VARIABLE TX_ERROR
)
if(NOT TX_RESULT EQUAL 0)
    message(
        FATAL_ERROR
        "Transaction code generation failed:\n${TX_OUTPUT}\n${TX_ERROR}\n${TX_RESULT}"
    )
endif()

# Generate ledger entry classes.
execute_process(
    COMMAND
        ${CODEGEN_PYTHON} "${GENERATE_LEDGER_SCRIPT}" "${LEDGER_ENTRIES_MACRO}"
        --header-dir "${AUTOGEN_HEADER_DIR}/ledger_entries" --test-dir
        "${AUTOGEN_TEST_DIR}/ledger_entries" --sfields-macro "${SFIELDS_MACRO}"
    RESULT_VARIABLE LEDGER_RESULT
    OUTPUT_VARIABLE LEDGER_OUTPUT
    ERROR_VARIABLE LEDGER_ERROR
)
if(NOT LEDGER_RESULT EQUAL 0)
    message(
        FATAL_ERROR
        "Ledger entry code generation failed:\n${LEDGER_OUTPUT}\n${LEDGER_ERROR}\n${TX_RESULT}"
    )
endif()

message(STATUS "Protocol autogen: code generation complete")

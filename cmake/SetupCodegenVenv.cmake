#[===================================================================[
   SetupCodegenVenv.cmake
   Run as a cmake -P script at build time to create/update the Python
   virtual environment used for protocol autogen code generation.

   Required variables (pass via -D on the command line):
     PYTHON_EXECUTABLE   - Path to the system Python3 interpreter
     VENV_DIR            - Directory where the venv should be created
     REQUIREMENTS_FILE   - Path to requirements.txt
     STAMP_FILE          - Path to the stamp file to touch on success
#]===================================================================]

if(NOT PYTHON_EXECUTABLE)
    message(FATAL_ERROR "SetupCodegenVenv: PYTHON_EXECUTABLE not set")
endif()
if(NOT VENV_DIR)
    message(FATAL_ERROR "SetupCodegenVenv: VENV_DIR not set")
endif()
if(NOT REQUIREMENTS_FILE)
    message(FATAL_ERROR "SetupCodegenVenv: REQUIREMENTS_FILE not set")
endif()
if(NOT STAMP_FILE)
    message(FATAL_ERROR "SetupCodegenVenv: STAMP_FILE not set")
endif()

if(WIN32)
    set(VENV_PIP "${VENV_DIR}/Scripts/pip.exe")
else()
    set(VENV_PIP "${VENV_DIR}/bin/pip")
endif()

message(STATUS "Setting up Python virtual environment at ${VENV_DIR}...")

execute_process(
    COMMAND "${PYTHON_EXECUTABLE}" -m venv "${VENV_DIR}"
    RESULT_VARIABLE VENV_RESULT
    ERROR_VARIABLE VENV_ERROR
)
if(NOT VENV_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to create virtual environment: ${VENV_ERROR}")
endif()

execute_process(
    COMMAND "${VENV_PIP}" install --upgrade pip
    OUTPUT_QUIET
    ERROR_VARIABLE PIP_UPGRADE_ERROR
)

message(STATUS "Installing Python dependencies from ${REQUIREMENTS_FILE}...")

execute_process(
    COMMAND "${VENV_PIP}" install -r "${REQUIREMENTS_FILE}"
    RESULT_VARIABLE PIP_RESULT
    ERROR_VARIABLE PIP_ERROR
)
if(NOT PIP_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to install Python dependencies: ${PIP_ERROR}")
endif()

file(TOUCH "${STAMP_FILE}")
message(STATUS "Python virtual environment ready")

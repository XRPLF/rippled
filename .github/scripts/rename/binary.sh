#!/bin/bash

# Exit the script as soon as an error occurs.
set -e

# On MacOS, ensure that GNU sed is installed and available as `gsed`.
SED_COMMAND=sed
if [[ "$OSTYPE" == "darwin"* ]]; then
  if ! command -v gsed &> /dev/null; then
      echo "Error: gsed is not installed. Please install it using 'brew install gnu-sed'."
      exit 1
  fi
  SED_COMMAND=gsed
fi

# This script changes the binary name from `rippled` to `xrpld`, and creates a
# symlink named `rippled` that points to the `xrpld` binary.
# Usage: .github/scripts/rename/binary.sh <repository directory>

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <repository directory>"
    exit 1
fi

DIRECTORY=$1
echo "Processing directory: ${DIRECTORY}"
if [ ! -d "${DIRECTORY}" ]; then
    echo "Error: Directory '${DIRECTORY}' does not exist."
    exit 1
fi

FILE="${DIRECTORY}/cmake/XrplCore.cmake"
echo "Processing file: ${FILE}"
${SED_COMMAND} -i -E 's/For the time being.+/Create a symlink named "rippled" for backward compatibility./g' "${FILE}"
${SED_COMMAND} -i -E 's/set_target_properties\(xrpld.+/add_custom_command(TARGET xrpld POST_BUILD COMMAND ${CMAKE_COMMAND} -E create_symlink "xrpld" "rippled")/g' "${FILE}"

echo "Processing complete."

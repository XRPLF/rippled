#!/bin/bash

# Exit the script as soon as an error occurs.
set -e

# This script checks whether there are no new include guards introduced by a new
# PR, as header files should use "#pragma once" instead. The script assumes any
# include guards will use "XRPL_" as prefix.
# Usage: .github/scripts/rename/include.sh <repository directory>

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

find "${DIRECTORY}" -type f \( -name "*.h" -o -name "*.hpp" -o -name "*.ipp" \) | while read -r FILE; do
    echo "Processing file: ${FILE}"
    if grep -q "#ifndef XRPL_" "${FILE}"; then
      echo "Please replace all include guards by #pragma once."
      exit 1
    fi
done
echo "Checking complete."

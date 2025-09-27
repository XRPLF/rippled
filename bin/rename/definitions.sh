#!/bin/bash

# This script renames definitions, such as include guards, in this project.
# Specifically, it renames "RIPPLED_XXX" and "RIPPLE_XXX" to "XRPL_XXX" by
# scanning all cmake, header, and source files in the specified directory and
# its subdirectories.
# Usage: bin/rename/definitions.sh from the repository root.

if [ "$#" -ne 0 ]; then
    echo "Usage: $0"
    exit 1
fi

DIRECTORIES=("cmake" "include" "src" "tests")
for DIRECTORY in "${DIRECTORIES[@]}"; do
  echo "Processing directory: ${DIRECTORY}"
  if [ ! -d "${DIRECTORY}" ]; then
      echo "Error: Directory '${DIRECTORY}' does not exist."
      exit 1
  fi

  find "${DIRECTORY}" -type f \( -name "*.h" -o -name "*.hpp" -o -name "*.ipp" -o -name "*.cpp" \) | while read -r FILE; do
      echo "Processing file: ${FILE}"
      sed -i'.bak' -E 's@#(define|endif|if|ifdef|ifndef)(.*)(RIPPLED_|RIPPLE_)([A-Z0-9_]+)@#\1\2XRPL_\4@g' "${FILE}"
      rm "${FILE}.bak"
  done
  find "${DIRECTORY}" -type f \( -name "*.cmake" \) | while read -r FILE; do
      echo "Processing file: ${FILE}"
      sed -i'.bak' -E 's@(RIPPLED_|RIPPLE_)([A-Z0-9_]+)@XRPL_\2@g' "${FILE}"
      rm "${FILE}.bak"
  done
done
echo "Renaming complete."

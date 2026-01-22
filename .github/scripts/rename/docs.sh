#!/bin/bash

# Exit the script as soon as an error occurs.
set -e

# On MacOS, ensure that GNU sed is installed and available as `gsed`.
SED_COMMAND=sed
if [[ "${OSTYPE}" == 'darwin'* ]]; then
  if ! command -v gsed &> /dev/null; then
      echo "Error: gsed is not installed. Please install it using 'brew install gnu-sed'."
      exit 1
  fi
  SED_COMMAND=gsed
fi

# This script renames all remaining references to `ripple` and `rippled` to
# `xrpl` and `xrpld`, respectively, in comments and documentation.
# Usage: .github/scripts/rename/docs.sh <repository directory>

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
pushd ${DIRECTORY}

DIRECTORIES=("cfg" "cmake" "conan" "docs" "include" "src" "tests")
for DIRECTORY in "${DIRECTORIES[@]}"; do
  echo "Processing directory: ${DIRECTORY}"

  find "${DIRECTORY}" -type f | while read -r FILE; do
      echo "Processing file: ${FILE}"
      ${SED_COMMAND} -i -E 's@rippled@xrpld@g' "${FILE}"
  done
done

#DIRECTORY=$1
#find "${DIRECTORY}" -type f -name "*.md" | while read -r FILE; do
#    echo "Processing file: ${FILE}"
#    ${SED_COMMAND} -i -E 's@([\s\`])rippled([\s\`])@\1xrpld\2@g' "${FILE}"
#done

popd
echo "Renaming complete."

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

# This script renames the `ripple` namespace to `xrpl` in this project.
# Specifically, it renames all occurrences of `namespace ripple` and `ripple::`
# to `namespace xrpl` and `xrpl::`, respectively, by scanning all header and
# source files in the specified directory and its subdirectories, as well as any
# occurrences in the documentation. It also renames them in the test suites.
# Usage: .github/scripts/rename/namespace.sh <repository directory>

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

DIRECTORIES=("include" "src" "tests")
for DIRECTORY in "${DIRECTORIES[@]}"; do
  echo "Processing directory: ${DIRECTORY}"

  find "${DIRECTORY}" -type f \( -name "*.h" -o -name "*.hpp" -o -name "*.ipp" -o -name "*.cpp" \) | while read -r FILE; do
      echo "Processing file: ${FILE}"
      ${SED_COMMAND} -i 's/namespace ripple/namespace xrpl/g' "${FILE}"
      ${SED_COMMAND} -i 's/ripple::/xrpl::/g' "${FILE}"
      ${SED_COMMAND} -i -E 's/(BEAST_DEFINE_TESTSUITE.+)ripple(.+)/\1xrpl\2/g' "${FILE}"
  done
done

# Special case for NuDBFactory that has ripple twice in the test suite name.
${SED_COMMAND} -i -E 's/(BEAST_DEFINE_TESTSUITE.+)ripple(.+)/\1xrpl\2/g' src/test/nodestore/NuDBFactory_test.cpp

DIRECTORY=$1
find "${DIRECTORY}" -type f -name "*.md" | while read -r FILE; do
    echo "Processing file: ${FILE}"
    ${SED_COMMAND} -i 's/ripple::/xrpl::/g' "${FILE}"
done

popd
echo "Renaming complete."

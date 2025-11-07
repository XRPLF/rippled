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

# This script changes the binary name from `rippled` to `xrpld`, and reverse the
# symlink that currently points from `xrpld` to `rippled` so that it points from
# `rippled` to `xrpld` instead.
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
pushd ${DIRECTORY}

# Remove the binary name override added by the cmake.sh script.
${SED_COMMAND} -z -i -E 's@\s+# For the time being.+"rippled"\)@@' cmake/XrplCore.cmake

# Reverse the symlink.
${SED_COMMAND} -i -E 's@create_symbolic_link\(rippled@create_symbolic_link(xrpld@' cmake/XrplInstall.cmake
${SED_COMMAND} -i -E 's@/xrpld\$\{suffix\}@/rippled${suffix}@' cmake/XrplInstall.cmake

popd
echo "Processing complete."

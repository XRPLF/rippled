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

# This script renames the config from `rippled.cfg` to `xrpld.cfg`, and updates
# the code accordingly. The old filename will still be accepted.
# Usage: .github/scripts/rename/config.sh <repository directory>

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

# Add the xrpld.cfg to the .gitignore.
if ! grep -q 'xrpld.cfg' ${DIRECTORY}/.gitignore; then
  ${SED_COMMAND} -i '/rippled.cfg/a\
xrpld.cfg' ${DIRECTORY}/.gitignore
fi

# Rename the files.
if [ -e "${DIRECTORY}/rippled.cfg" ]; then
  mv "${DIRECTORY}/rippled.cfg" "${DIRECTORY}/xrpld.cfg"
fi
if [ -e "${DIRECTORY}/cfg/rippled-example.cfg" ]; then
  mv "${DIRECTORY}/cfg/rippled-example.cfg" "${DIRECTORY}/cfg/xrpld-example.cfg"
fi

# Rename inside the files.
DIRECTORIES=("cfg" "cmake" "include" "src")
for DIRECTORY in "${DIRECTORIES[@]}"; do
  DIRECTORY=$1/${DIRECTORY}
  echo "Processing directory: ${DIRECTORY}"
  if [ ! -d "${DIRECTORY}" ]; then
      echo "Error: Directory '${DIRECTORY}' does not exist."
      exit 1
  fi

  find "${DIRECTORY}" -type f \( -name "*.h" -o -name "*.hpp" -o -name "*.ipp" -o -name "*.cpp" -o -name "*.cmake" -o -name "*.txt" -o -name "*.cfg" -o -name "*.md" \) | while read -r FILE; do
      echo "Processing file: ${FILE}"
      ${SED_COMMAND} -i -E 's/rippled(-example)?[ .]cfg/xrpld\1.cfg/g' "${FILE}"
  done
done

echo "Renaming complete."

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
pushd ${DIRECTORY}

# Add the xrpld.cfg to the .gitignore.
if ! grep -q 'xrpld.cfg' .gitignore; then
  ${SED_COMMAND} -i '/rippled.cfg/a\
/xrpld.cfg' .gitignore
fi

# Rename the files.
if [ -e rippled.cfg ]; then
  mv rippled.cfg xrpld.cfg
fi
if [ -e cfg/rippled-example.cfg ]; then
  mv cfg/rippled-example.cfg cfg/xrpld-example.cfg
fi

# Rename inside the files.
DIRECTORIES=("cfg" "cmake" "include" "src")
for DIRECTORY in "${DIRECTORIES[@]}"; do
  echo "Processing directory: ${DIRECTORY}"

  find "${DIRECTORY}" -type f \( -name "*.h" -o -name "*.hpp" -o -name "*.ipp" -o -name "*.cpp" -o -name "*.cmake" -o -name "*.txt" -o -name "*.cfg" -o -name "*.md" \) | while read -r FILE; do
      echo "Processing file: ${FILE}"
      ${SED_COMMAND} -i -E 's/rippled(-example)?[ .]cfg/xrpld\1.cfg/g' "${FILE}"
  done
done
${SED_COMMAND} -i 's/rippled/xrpld/g' cfg/xrpld-example.cfg
${SED_COMMAND} -i 's/rippled/xrpld/g' src/test/core/Config_test.cpp
${SED_COMMAND} -i 's/ripplevalidators/xrplvalidators/g' src/test/core/Config_test.cpp # cspell: disable-line
${SED_COMMAND} -i 's/rippleConfig/xrpldConfig/g' src/test/core/Config_test.cpp
${SED_COMMAND} -i 's@ripple/@xrpld/@g' src/test/core/Config_test.cpp
${SED_COMMAND} -i 's/Rippled/File/g' src/test/core/Config_test.cpp


# Restore the old config file name in the code that maintains support for now.
${SED_COMMAND} -i 's/configLegacyName = "xrpld.cfg"/configLegacyName = "rippled.cfg"/g' src/xrpld/core/detail/Config.cpp

# Restore an URL.
${SED_COMMAND} -i 's/connect-your-xrpld-to-the-xrp-test-net.html/connect-your-rippled-to-the-xrp-test-net.html/g' cfg/xrpld-example.cfg

popd
echo "Renaming complete."

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

# This script removes superfluous copyright notices in source and header files
# in this project. Specifically, it removes all notices referencing Ripple,
# XRPLF, and certain individual contributors upon mutual agreement, so the one
# in the LICENSE.md file applies throughout. Copyright notices referencing
# external contributions, e.g. from Bitcoin, remain as-is.
# Usage: .github/scripts/rename/copyright.sh <repository directory>

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

# Prevent sed and echo from removing newlines and tabs in string literals by
# temporarily replacing them with placeholders. This only affects one file.
PLACEHOLDER_NEWLINE="__NEWLINE__"
PLACEHOLDER_TAB="__TAB__"
${SED_COMMAND} -i -E "s@\\\n@${PLACEHOLDER_NEWLINE}@g" src/test/rpc/ValidatorInfo_test.cpp
${SED_COMMAND} -i -E "s@\\\t@${PLACEHOLDER_TAB}@g" src/test/rpc/ValidatorInfo_test.cpp

# Process the include/ and src/ directories.
DIRECTORIES=("include" "src")
for DIRECTORY in "${DIRECTORIES[@]}"; do
  echo "Processing directory: ${DIRECTORY}"

  find "${DIRECTORY}" -type f \( -name "*.h" -o -name "*.hpp" -o -name "*.ipp" -o -name "*.cpp" -o -name "*.macro" \) | while read -r FILE; do
      echo "Processing file: ${FILE}"
      # Handle the cases where the copyright notice is enclosed in /* ... */
      # and usually surrounded by //---- and //======.
      ${SED_COMMAND} -z -i -E 's@^//-------+\n+@@' "${FILE}"
      ${SED_COMMAND} -z -i -E 's@^.*Copyright.+(Ripple|Bougalis|Falco|Hinnant|Null|Ritchford|XRPLF).+PERFORMANCE OF THIS SOFTWARE\.\n\*/\n+@@' "${FILE}"
      ${SED_COMMAND} -z -i -E 's@^//=======+\n+@@' "${FILE}"

      # Handle the cases where the copyright notice is commented out with //.
      ${SED_COMMAND} -z -i -E 's@^//\n// Copyright.+Falco \(vinnie dot falco at gmail dot com\)\n//\n+@@' "${FILE}"
  done
done

# Restore copyright notices that were removed from specific files, without
# restoring the verbiage that is already present in LICENSE.md. Ensure that if
# the script is run multiple times, duplicate notices are not added.
if ! grep -q 'Raw Material Software' include/xrpl/beast/core/CurrentThreadName.h; then
  echo -e "// Portions of this file are from JUCE (http://www.juce.com).\n// Copyright (c) 2013 - Raw Material Software Ltd.\n// Please visit http://www.juce.com\n\n$(cat include/xrpl/beast/core/CurrentThreadName.h)" > include/xrpl/beast/core/CurrentThreadName.h
fi
if ! grep -q 'Dev Null' src/test/app/NetworkID_test.cpp; then
  echo -e "// Copyright (c) 2020 Dev Null Productions\n\n$(cat src/test/app/NetworkID_test.cpp)" > src/test/app/NetworkID_test.cpp
fi
if ! grep -q 'Dev Null' src/test/app/tx/apply_test.cpp; then
  echo -e "// Copyright (c) 2020 Dev Null Productions\n\n$(cat src/test/app/tx/apply_test.cpp)" > src/test/app/tx/apply_test.cpp
fi
if ! grep -q 'Dev Null' src/test/rpc/ManifestRPC_test.cpp; then
  echo -e "// Copyright (c) 2020 Dev Null Productions\n\n$(cat src/test/rpc/ManifestRPC_test.cpp)" > src/test/rpc/ManifestRPC_test.cpp
fi
if ! grep -q 'Dev Null' src/test/rpc/ValidatorInfo_test.cpp; then
  echo -e "// Copyright (c) 2020 Dev Null Productions\n\n$(cat src/test/rpc/ValidatorInfo_test.cpp)" > src/test/rpc/ValidatorInfo_test.cpp
fi
if ! grep -q 'Dev Null' src/xrpld/rpc/handlers/DoManifest.cpp; then
  echo -e "// Copyright (c) 2019 Dev Null Productions\n\n$(cat src/xrpld/rpc/handlers/DoManifest.cpp)" > src/xrpld/rpc/handlers/DoManifest.cpp
fi
if ! grep -q 'Dev Null' src/xrpld/rpc/handlers/ValidatorInfo.cpp; then
  echo -e "// Copyright (c) 2019 Dev Null Productions\n\n$(cat src/xrpld/rpc/handlers/ValidatorInfo.cpp)" > src/xrpld/rpc/handlers/ValidatorInfo.cpp
fi
if ! grep -q 'Bougalis' include/xrpl/basics/SlabAllocator.h; then
  echo -e "// Copyright (c) 2022, Nikolaos D. Bougalis <nikb@bougalis.net>\n\n$(cat include/xrpl/basics/SlabAllocator.h)" > include/xrpl/basics/SlabAllocator.h
fi
if ! grep -q 'Bougalis' include/xrpl/basics/spinlock.h; then
  echo -e "// Copyright (c) 2022, Nikolaos D. Bougalis <nikb@bougalis.net>\n\n$(cat include/xrpl/basics/spinlock.h)" > include/xrpl/basics/spinlock.h
fi
if ! grep -q 'Bougalis' include/xrpl/basics/tagged_integer.h; then
  echo -e "// Copyright (c) 2014, Nikolaos D. Bougalis <nikb@bougalis.net>\n\n$(cat include/xrpl/basics/tagged_integer.h)" > include/xrpl/basics/tagged_integer.h
fi
if ! grep -q 'Ritchford' include/xrpl/beast/utility/Zero.h; then
  echo -e "// Copyright (c) 2014, Tom Ritchford <tom@swirly.com>\n\n$(cat include/xrpl/beast/utility/Zero.h)" > include/xrpl/beast/utility/Zero.h
fi

# Restore newlines and tabs in string literals in the affected file.
${SED_COMMAND} -i -E "s@${PLACEHOLDER_NEWLINE}@\\\n@g" src/test/rpc/ValidatorInfo_test.cpp
${SED_COMMAND} -i -E "s@${PLACEHOLDER_TAB}@\\\t@g" src/test/rpc/ValidatorInfo_test.cpp

popd
echo "Removal complete."

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
# `xrpl` and `xrpld`, respectively, in code, comments, and documentation.
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

find . -type f \( -name "*.h" -o -name "*.hpp" -o -name "*.ipp" -o -name "*.cpp" -o -name "*.txt" -o -name "*.cfg" -o -name "*.md" -o -name "*.proto" \) -not -path "./.github/scripts/*" | while read -r FILE; do
    echo "Processing file: ${FILE}"
    ${SED_COMMAND} -i -E 's@([^+-/])rippled@\1xrpld@g' "${FILE}"
    ${SED_COMMAND} -i -E 's@([^+-/])Rippled@\1Xrpld@g' "${FILE}"
    ${SED_COMMAND} -i -E 's/^rippled/xrpld/g' "${FILE}"
    ${SED_COMMAND} -i -E 's/^Rippled/Xrpld/g' "${FILE}"
    ${SED_COMMAND} -i -E 's@(/etc)?/opt/rippled?@/etc/opt/xrpld@g' "${FILE}"
    # cspell: disable
    ${SED_COMMAND} -i 's/(r|R)ipple (e|E)poch/XRPL epoch/g' "${FILE}"
    ${SED_COMMAND} -i 's/Ripple (n|N)etwork/XRPL network/g' "${FILE}"
    ${SED_COMMAND} -i 's/Ripple (p|P)ayment/XRPL payment/g' "${FILE}"
    ${SED_COMMAND} -i 's/Ripple (p|P)rotocol/XRPL protocol/g' "${FILE}"
    ${SED_COMMAND} -i 's/Ripple (r|R)epository/XRPL repository/g' "${FILE}"
    ${SED_COMMAND} -i 's/Ripple (s|S)erialization/XRPL serialization/g' "${FILE}"
    ${SED_COMMAND} -i 's/Ripple (s|S)specific/XRPL specific/g' "${FILE}"
    # cspell: enable
    ${SED_COMMAND} -i 's/a xrpl/an xrpl/g' "${FILE}"
    ${SED_COMMAND} -i 's/RippleLib/XrplLib/g' "${FILE}"
    ${SED_COMMAND} -i 's/ripple-lib/XrplLib/g' "${FILE}"
    ${SED_COMMAND} -i 's@src/ripple/@src/xrpld/@g' "${FILE}"
    ${SED_COMMAND} -i 's@ripple/app/@xrpld/app/@g' "${FILE}"
    ${SED_COMMAND} -i 's@https://github.com/ripple/rippled@https://github.com/XRPLF/rippled@g' "${FILE}"
    ${SED_COMMAND} -i 's/rippleLockEscrowMPT/lockEscrowMPT/g' "${FILE}"
    ${SED_COMMAND} -i 's/rippleUnlockEscrowMPT/unlockEscrowMPT/g' "${FILE}"
    ${SED_COMMAND} -i 's/rippleCredit/directSend/g' "${FILE}"
done
${SED_COMMAND} -i 's/ripple_libs/xrpl_libs/' BUILD.md
${SED_COMMAND} -i 's/Ripple integrators/XRPL developers/' README.md
${SED_COMMAND} -i 's/sanitizer-configuration-for-rippled/sanitizer-configuration-for-xrpld/' docs/build/sanitizers.md
${SED_COMMAND} -i 's/rippled/xrpld/' .github/scripts/levelization/README.md
${SED_COMMAND} -i 's/rippled/xrpld/' .github/scripts/strategy-matrix/generate.py
${SED_COMMAND} -i 's@/rippled@/xrpld@' docs/build/install.md
${SED_COMMAND} -i 's/rippled/xrpld/' docs/Doxyfile
${SED_COMMAND} -i 's/ripple_basics/basics/' include/xrpl/basics/CountedObject.h
${SED_COMMAND} -i 's/<ripple/<xrpl/' include/xrpl/protocol/AccountID.h
${SED_COMMAND} -i 's/Ripple:/the XRPL:/g' include/xrpl/protocol/SecretKey.h
${SED_COMMAND} -i 's/Ripple:/the XRPL:/g' include/xrpl/protocol/Seed.h
${SED_COMMAND} -i 's/ripple/xrpl/' src/test/README.md
${SED_COMMAND} -i 's/www.ripple.com/www.xrpl.org/g' src/test/protocol/Seed_test.cpp

# Restore specific changes.
${SED_COMMAND} -i 's@b5efcc/src/xrpld@b5efcc/src/ripple@' include/xrpl/protocol/README.md
${SED_COMMAND} -i 's/dbPrefix_ = "xrpldb"/dbPrefix_ = "rippledb"/' src/xrpld/app/misc/SHAMapStoreImp.h # cspell: disable-line
${SED_COMMAND} -i 's/configLegacyName = "xrpld.cfg"/configLegacyName = "rippled.cfg"/' src/xrpld/core/detail/Config.cpp

popd
echo "Renaming complete."

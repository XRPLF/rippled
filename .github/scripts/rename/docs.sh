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
pushd "${DIRECTORY}"

find . -type f \( -name "*.h" -o -name "*.hpp" -o -name "*.ipp" -o -name "*.cpp" -o -name "*.txt" -o -name "*.cfg" -o -name "*.md" -o -name "*.proto" \) -not -path "./.github/scripts/*" | while read -r FILE; do
    echo "Processing file: ${FILE}"
    ${SED_COMMAND} -i 's/rippleLockEscrowMPT/lockEscrowMPT/g' "${FILE}"
    ${SED_COMMAND} -i 's/rippleUnlockEscrowMPT/unlockEscrowMPT/g' "${FILE}"
    ${SED_COMMAND} -i 's/rippleCredit/directSendNoFee/g' "${FILE}"
    ${SED_COMMAND} -i 's/rippleSend/directSendNoLimit/g' "${FILE}"
    ${SED_COMMAND} -i -E 's@([^/+-])rippled@\1xrpld@g' "${FILE}"
    ${SED_COMMAND} -i -E 's@([^/+-])Rippled@\1Xrpld@g' "${FILE}"
    ${SED_COMMAND} -i -E 's/^rippled/xrpld/g' "${FILE}"
    ${SED_COMMAND} -i -E 's/^Rippled/Xrpld/g' "${FILE}"
    # cspell: disable
    ${SED_COMMAND} -i -E 's/(r|R)ipple (a|A)ddress/XRPL address/g' "${FILE}"
    ${SED_COMMAND} -i -E 's/(r|R)ipple (a|A)ccount/XRPL account/g' "${FILE}"
    ${SED_COMMAND} -i -E 's/(r|R)ipple (a|A)lgorithm/XRPL algorithm/g' "${FILE}"
    ${SED_COMMAND} -i -E 's/(r|R)ipple (c|C)lient/XRPL client/g' "${FILE}"
    ${SED_COMMAND} -i -E 's/(r|R)ipple (c|C)luster/XRPL cluster/g' "${FILE}"
    ${SED_COMMAND} -i -E 's/(r|R)ipple (c|C)onsensus/XRPL consensus/g' "${FILE}"
    ${SED_COMMAND} -i -E 's/(r|R)ipple (d|D)efault/XRPL default/g' "${FILE}"
    ${SED_COMMAND} -i -E 's/(r|R)ipple (e|E)poch/XRPL epoch/g' "${FILE}"
    ${SED_COMMAND} -i -E 's/(r|R)ipple (f|F)eature/XRPL feature/g' "${FILE}"
    ${SED_COMMAND} -i -E 's/(r|R)ipple (n|N)etwork/XRPL network/g' "${FILE}"
    ${SED_COMMAND} -i -E 's/(r|R)ipple (p|P)ayment/XRPL payment/g' "${FILE}"
    ${SED_COMMAND} -i -E 's/(r|R)ipple (p|P)rotocol/XRPL protocol/g' "${FILE}"
    ${SED_COMMAND} -i -E 's/(r|R)ipple (r|R)epository/XRPL repository/g' "${FILE}"
    ${SED_COMMAND} -i -E 's/(r|R)ipple RPC/XRPL RPC/g' "${FILE}"
    ${SED_COMMAND} -i -E 's/(r|R)ipple (s|S)erialization/XRPL serialization/g' "${FILE}"
    ${SED_COMMAND} -i -E 's/(r|R)ipple (s|S)erver/XRPL server/g' "${FILE}"
    ${SED_COMMAND} -i -E 's/(r|R)ipple (s|S)pecific/XRPL specific/g' "${FILE}"
    ${SED_COMMAND} -i -E 's/(r|R)ipple Source/XRPL Source/g' "${FILE}"
    ${SED_COMMAND} -i -E 's/(r|R)ipple (t|T)imestamp/XRPL timestamp/g' "${FILE}"
    ${SED_COMMAND} -i -E 's/(r|R)ipple uses the consensus/XRPL uses the consensus/g' "${FILE}"
    ${SED_COMMAND} -i -E 's/(r|R)ipple (v|V)alidator/XRPL validator/g' "${FILE}"
    # cspell: enable
    ${SED_COMMAND} -i 's/RippleLib/XrplLib/g' "${FILE}"
    ${SED_COMMAND} -i 's/ripple-lib/XrplLib/g' "${FILE}"
    ${SED_COMMAND} -i 's@opt/ripple/@opt/xrpld/@g' "${FILE}"
    ${SED_COMMAND} -i 's@src/ripple/@src/xrpld/@g' "${FILE}"
    ${SED_COMMAND} -i 's@ripple/app/@xrpld/app/@g' "${FILE}"
    ${SED_COMMAND} -i 's@github.com/ripple/rippled@github.com/XRPLF/rippled@g' "${FILE}"
    ${SED_COMMAND} -i 's/\ba xrpl/an xrpl/g' "${FILE}"
    ${SED_COMMAND} -i 's/\ba XRPL/an XRPL/g' "${FILE}"
done
${SED_COMMAND} -i 's/ripple_libs/xrpl_libs/' BUILD.md
${SED_COMMAND} -i 's/Ripple integrators/XRPL developers/' README.md
${SED_COMMAND} -i 's/sanitizer-configuration-for-rippled/sanitizer-configuration-for-xrpld/' docs/build/sanitizers.md
${SED_COMMAND} -i 's/rippled/xrpld/g' .github/scripts/levelization/README.md
${SED_COMMAND} -i 's/rippled/xrpld/g' .github/scripts/strategy-matrix/generate.py
${SED_COMMAND} -i 's@/rippled@/xrpld@g' docs/build/install.md
${SED_COMMAND} -i 's@github.com/XRPLF/xrpld@github.com/XRPLF/rippled@g' docs/build/install.md
${SED_COMMAND} -i 's/rippled/xrpld/g' docs/Doxyfile
${SED_COMMAND} -i 's/ripple_basics/basics/' include/xrpl/basics/CountedObject.h
${SED_COMMAND} -i 's/<ripple/<xrpl/' include/xrpl/protocol/AccountID.h
${SED_COMMAND} -i 's/Ripple:/the XRPL:/g' include/xrpl/protocol/SecretKey.h
${SED_COMMAND} -i 's/Ripple:/the XRPL:/g' include/xrpl/protocol/Seed.h
${SED_COMMAND} -i 's/ripple/xrpl/g' src/test/README.md
${SED_COMMAND} -i 's/www.ripple.com/www.xrpl.org/g' src/test/protocol/Seed_test.cpp

# Restore specific changes.
${SED_COMMAND} -i 's@b5efcc/src/xrpld@b5efcc/src/ripple@' include/xrpl/protocol/README.md
${SED_COMMAND} -i 's/dbPrefix_ = "xrpldb"/dbPrefix_ = "rippledb"/' src/xrpld/app/misc/SHAMapStoreImp.h # cspell: disable-line
${SED_COMMAND} -i 's/kConfigLegacyName = "xrpld.cfg"/kConfigLegacyName = "rippled.cfg"/' src/xrpld/core/detail/Config.cpp

popd
echo "Renaming complete."

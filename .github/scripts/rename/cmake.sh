#!/bin/bash

# Exit the script as soon as an error occurs.
set -e

# On MacOS, ensure that GNU sed and head are installed and available as `gsed`
# and `ghead`, respectively.
SED_COMMAND=sed
HEAD_COMMAND=head
if [[ "${OSTYPE}" == 'darwin'* ]]; then
  if ! command -v gsed &> /dev/null; then
      echo "Error: gsed is not installed. Please install it using 'brew install gnu-sed'."
      exit 1
  fi
  SED_COMMAND=gsed
  if ! command -v ghead &> /dev/null; then
      echo "Error: ghead is not installed. Please install it using 'brew install coreutils'."
      exit 1
  fi
  HEAD_COMMAND=ghead
fi

# This script renames CMake files from `RippleXXX.cmake` or `RippledXXX.cmake`
# to `XrplXXX.cmake`, and any references to `ripple` and `rippled` (with or
# without capital letters) to `xrpl` and `xrpld`, respectively. The name of the
# binary will remain as-is, and will only be renamed to `xrpld` in a different
# script, but the proto file will be renamed.
# Usage: .github/scripts/rename/cmake.sh <repository directory>

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

# Rename the files.
find cmake -type f -name 'Rippled*.cmake' -exec bash -c 'mv "${1}" "${1/Rippled/Xrpl}"' - {} \;
find cmake -type f -name 'Ripple*.cmake' -exec bash -c 'mv "${1}" "${1/Ripple/Xrpl}"' - {} \;
if [ -e cmake/xrpl_add_test.cmake ]; then
  mv cmake/xrpl_add_test.cmake cmake/XrplAddTest.cmake
fi
if [ -e include/xrpl/proto/ripple.proto ]; then
  mv include/xrpl/proto/ripple.proto include/xrpl/proto/xrpl.proto
fi

# Rename inside the files.
find cmake -type f -name '*.cmake' | while read -r FILE; do
    echo "Processing file: ${FILE}"
    ${SED_COMMAND} -i 's/Rippled/Xrpld/g' "${FILE}"
    ${SED_COMMAND} -i 's/Ripple/Xrpl/g' "${FILE}"
    ${SED_COMMAND} -i 's/rippled/xrpld/g' "${FILE}"
    ${SED_COMMAND} -i 's/ripple/xrpl/g' "${FILE}"
done
${SED_COMMAND} -i -E 's/Rippled?/Xrpl/g' CMakeLists.txt
${SED_COMMAND} -i 's/ripple/xrpl/g' CMakeLists.txt
${SED_COMMAND} -i 's/include(xrpl_add_test)/include(XrplAddTest)/' src/tests/libxrpl/CMakeLists.txt
${SED_COMMAND} -i 's/ripple.pb.h/xrpl.pb.h/' include/xrpl/protocol/messages.h
${SED_COMMAND} -i 's/ripple.pb.h/xrpl.pb.h/' BUILD.md
${SED_COMMAND} -i 's/ripple.pb.h/xrpl.pb.h/' BUILD.md

# Restore the name of the validator keys repository.
${SED_COMMAND} -i 's@xrpl/validator-keys-tool@ripple/validator-keys-tool@' cmake/XrplValidatorKeys.cmake

# Ensure the name of the binary and config remain 'rippled' for now.
${SED_COMMAND} -i -E 's/xrpld(-example)?\.cfg/rippled\1.cfg/g' cmake/XrplInstall.cmake
if grep -q '"xrpld"' cmake/XrplCore.cmake; then
  # The script has been rerun, so just restore the name of the binary.
  ${SED_COMMAND} -i 's/"xrpld"/"rippled"/' cmake/XrplCore.cmake
elif ! grep -q '"rippled"' cmake/XrplCore.cmake; then
  ghead -n -1 cmake/XrplCore.cmake > cmake.tmp
  echo '  # For the time being, we will keep the name of the binary as it was.' >> cmake.tmp
  echo '  set_target_properties(xrpld PROPERTIES OUTPUT_NAME "rippled")' >> cmake.tmp
  tail -1 cmake/XrplCore.cmake >> cmake.tmp
  mv cmake.tmp cmake/XrplCore.cmake
fi

popd
echo "Renaming complete."

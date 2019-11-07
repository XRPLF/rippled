#!/usr/bin/env bash
set -exu

if [[ -z ${COMSPEC:-} ]]; then
    EXE="vcpkg"
else
    EXE="vcpkg.exe"
fi

if [[ -d "${VCPKG_DIR}" && -x "${VCPKG_DIR}/${EXE}" ]] ; then
    echo "Using cached vcpkg at ${VCPKG_DIR}"
    ${VCPKG_DIR}/${EXE} list
    exit
fi

if [[ -d "${VCPKG_DIR}" ]] ; then
    rm -rf "${VCPKG_DIR}"
fi

git clone --branch 2019.10 https://github.com/Microsoft/vcpkg.git ${VCPKG_DIR}
pushd ${VCPKG_DIR}
if [[ -z ${COMSPEC:-} ]]; then
    chmod +x ./bootstrap-vcpkg.sh
    ./bootstrap-vcpkg.sh
else
    ./bootstrap-vcpkg.bat
fi
popd
# TODO  -- can pin specific ports to a commit/version like this:
#git checkout <SOME COMMIT HASH> ports/boost

${VCPKG_DIR}/${EXE} install openssl


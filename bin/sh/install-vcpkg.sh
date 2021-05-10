#!/usr/bin/env bash
set -exu

: ${TRAVIS_BUILD_DIR:=""}
: ${VCPKG_DIR:=".vcpkg"}
export VCPKG_ROOT=${VCPKG_DIR}
: ${VCPKG_DEFAULT_TRIPLET:="x64-windows-static"}

export VCPKG_DEFAULT_TRIPLET

EXE="vcpkg"
if [[ -z ${COMSPEC:-} ]]; then
    EXE="${EXE}.exe"
fi

if [[ -d "${VCPKG_DIR}" && -x "${VCPKG_DIR}/${EXE}" && -d "${VCPKG_DIR}/installed" ]] ; then
    echo "Using cached vcpkg at ${VCPKG_DIR}"
    ${VCPKG_DIR}/${EXE} list
else
    if [[ -d "${VCPKG_DIR}" ]] ; then
        rm -rf "${VCPKG_DIR}"
    fi
    gittag=2021.05.12
    git clone --branch ${gittag} https://github.com/Microsoft/vcpkg.git ${VCPKG_DIR}
    pushd ${VCPKG_DIR}
    if git tag -l --contains ${gittag} | grep -v ${gittag}
    then
      echo ****************************************************** >&2
      echo WARNING: Found git tags of vcpkg later than ${gittag}. >&2
      echo Consider updating to get the latest software versions. >&2
      echo ****************************************************** >&2
    fi
    BSARGS=()
    if [[ "$(uname)" == "Darwin" ]] ; then
        BSARGS+=(--allowAppleClang)
    fi
    if [[ -z ${COMSPEC:-} ]]; then
        chmod +x ./bootstrap-vcpkg.sh
        time ./bootstrap-vcpkg.sh "${BSARGS[@]}"
    else
        time ./bootstrap-vcpkg.bat
    fi
    popd
fi

# TODO: bring boost in this way as well ?
# NOTE: can pin specific ports to a commit/version like this:
#    git checkout <SOME COMMIT HASH> ports/boost
if [ $# -eq 0 ]; then
    echo "No extra packages specified..."
    PKGS=()
else
    PKGS=( "$@" )
fi
for LIB in "${PKGS[@]}"; do
    time ${VCPKG_DIR}/${EXE} --clean-after-build install ${LIB}
done



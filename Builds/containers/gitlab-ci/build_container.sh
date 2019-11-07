#!/usr/bin/env sh
set -ex
pkgtype=$1
if [ "${pkgtype}" = "rpm" ] ; then
    container_name="${RPM_CONTAINER_NAME}"
elif [ "${pkgtype}" = "dpkg" ] ; then
    container_name="${DPKG_CONTAINER_NAME}"
else
    echo "invalid package type"
    exit 1
fi

if docker pull "${ARTIFACTORY_HUB}/${container_name}:latest_${CI_COMMIT_REF_SLUG}"; then
    echo "found container for latest - using as cache."
    docker tag \
       "${ARTIFACTORY_HUB}/${container_name}:latest_${CI_COMMIT_REF_SLUG}" \
       "${container_name}:latest_${CI_COMMIT_REF_SLUG}"
    CMAKE_EXTRA="-D${pkgtype}_cache_from=${container_name}:latest_${CI_COMMIT_REF_SLUG}"
fi

cmake --version
test -d build && rm -rf build
mkdir -p build/container && cd build/container
eval time \
    cmake -Dpackages_only=ON -DCMAKE_VERBOSE_MAKEFILE=ON ${CMAKE_EXTRA} \
    -G Ninja ../..
time cmake --build . --target "${pkgtype}_container" -- -v


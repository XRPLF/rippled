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
time docker pull "${ARTIFACTORY_HUB}/${container_name}:${CI_COMMIT_SHA}"
docker tag \
  "${ARTIFACTORY_HUB}/${container_name}:${CI_COMMIT_SHA}" \
  "${container_name}:${CI_COMMIT_SHA}"
docker images
test -d build && rm -rf build
mkdir -p build/${pkgtype} && cd build/${pkgtype}
time cmake \
  -Dpackages_only=ON -Dhave_package_container=ON -DCMAKE_VERBOSE_MAKEFILE=ON \
  -G Ninja ../..
time cmake --build . --target ${pkgtype} -- -v


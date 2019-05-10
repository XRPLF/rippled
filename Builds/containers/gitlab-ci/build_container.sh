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
if docker pull "${ARTIFACTORY_HUB}/${container_name}:${CI_COMMIT_SHA}"; then
    echo "${pkgtype} container for ${CI_COMMIT_SHA} already exists" \
        "- skipping container build!"
    exit 0
else
    echo "no existing ${pkgtype} container for this branch - searching history."
    for CID_PREV in $(git log --pretty=%H -n30) ; do
        if docker pull "${ARTIFACTORY_HUB}/${container_name}:${CID_PREV}"; then
            echo "found container for previous commit ${CID_PREV}" \
                "- using as cache."
            docker tag \
               "${ARTIFACTORY_HUB}/${container_name}:${CID_PREV}" \
               "${container_name}:${CID_PREV}"
            CMAKE_EXTRA="-D${pkgtype}_cache_from=${container_name}:${CID_PREV}"
            break
        fi
    done
fi
cmake --version
test -d build && rm -rf build
mkdir -p build/container && cd build/container
eval time \
    cmake -Dpackages_only=ON -DCMAKE_VERBOSE_MAKEFILE=ON ${CMAKE_EXTRA} \
    -G Ninja ../..
time cmake --build . --target "${pkgtype}_container" -- -v
docker tag \
    "${container_name}:${CI_COMMIT_SHA}" \
    "${ARTIFACTORY_HUB}/${container_name}:${CI_COMMIT_SHA}"
time docker push "${ARTIFACTORY_HUB}/${container_name}:${CI_COMMIT_SHA}"


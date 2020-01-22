#!/usr/bin/env sh
set -ex
docker login -u rippled \
    -p ${ARTIFACTORY_DEPLOY_KEY_RIPPLED} "${ARTIFACTORY_HUB}"
# this gives us rippled_version :
source build/rpm/packages/build_vars
docker pull "${ARTIFACTORY_HUB}/${RPM_CONTAINER_FULLNAME}"
docker pull "${ARTIFACTORY_HUB}/${DPKG_CONTAINER_FULLNAME}"
# tag/push two labels...one using the current rippled version and one just using "latest"
for label in ${rippled_version} latest ; do
    docker tag \
        "${ARTIFACTORY_HUB}/${RPM_CONTAINER_FULLNAME}" \
        "${ARTIFACTORY_HUB}/${RPM_CONTAINER_NAME}:${label}_${CI_COMMIT_REF_SLUG}"
    docker push \
        "${ARTIFACTORY_HUB}/${RPM_CONTAINER_NAME}:${label}_${CI_COMMIT_REF_SLUG}"
    docker tag \
        "${ARTIFACTORY_HUB}/${DPKG_CONTAINER_FULLNAME}" \
        "${ARTIFACTORY_HUB}/${DPKG_CONTAINER_NAME}:${label}_${CI_COMMIT_REF_SLUG}"
    docker push \
        "${ARTIFACTORY_HUB}/${DPKG_CONTAINER_NAME}:${label}_${CI_COMMIT_REF_SLUG}"
done


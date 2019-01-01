#!/usr/bin/env sh
set -ex
action=$1
filter=$2

. ./Builds/containers/gitlab-ci/get_component.sh

apk add curl jq coreutils util-linux
TOPDIR=$(pwd)

# DPKG

cd $TOPDIR
cd build/dpkg/packages
CURLARGS="-sk -X${action} -urippled:${ARTIFACTORY_DEPLOY_KEY_RIPPLED}"
RIPPLED_PKG=$(ls rippled_*.deb)
RIPPLED_DEV_PKG=$(ls rippled-dev_*.deb)
RIPPLED_DBG_PKG=$(ls rippled-dbgsym_*.deb)
# TODO - where to upload src tgz?
RIPPLED_SRC=$(ls rippled_*.orig.tar.gz)
DEB_MATRIX=";deb.component=${COMPONENT};deb.architecture=amd64"
for dist in stretch buster xenial bionic ; do
    DEB_MATRIX="${DEB_MATRIX};deb.distribution=${dist}"
done
for deb in ${RIPPLED_PKG} ${RIPPLED_DEV_PKG} ${RIPPLED_DBG_PKG} ; do
    echo "FILE --> ${deb}" | tee -a "${TOPDIR}/files.info"
    ca="${CURLARGS}"
    if [ "${action}" = "PUT" ] ; then
        url="https://${ARTIFACTORY_HOST}/artifactory/${DEB_REPO}/pool/${deb}${DEB_MATRIX}"
        ca="${ca} -T${deb}"
    elif [ "${action}" = "GET" ] ; then
        url="https://${ARTIFACTORY_HOST}/artifactory/api/storage/${DEB_REPO}/pool/${deb}"
    fi
    echo "url --> ${url}"
    eval "curl ${ca} \"${url}\"" | jq -M "${filter}" | tee -a "${TOPDIR}/files.info"
done

# RPM

cd $TOPDIR
cd build/rpm/packages
RIPPLED_PKG=$(ls rippled-[0-9]*.x86_64.rpm)
RIPPLED_DEV_PKG=$(ls rippled-devel*.rpm)
RIPPLED_DBG_PKG=$(ls rippled-debuginfo*.rpm)
# TODO - where to upload src rpm ?
RIPPLED_SRC=$(ls rippled-[0-9]*.src.rpm)
for rpm in ${RIPPLED_PKG} ${RIPPLED_DEV_PKG} ${RIPPLED_DBG_PKG} ; do
    echo "FILE --> ${rpm}" | tee -a "${TOPDIR}/files.info"
    ca="${CURLARGS}"
    if [ "${action}" = "PUT" ] ; then
        url="https://${ARTIFACTORY_HOST}/artifactory/${RPM_REPO}/${COMPONENT}/"
        ca="${ca} -T${rpm}"
    elif [ "${action}" = "GET" ] ; then
        url="https://${ARTIFACTORY_HOST}/artifactory/api/storage/${RPM_REPO}/${COMPONENT}/${rpm}"
    fi
    echo "url --> ${url}"
    eval "curl ${ca} \"${url}\"" | jq -M "${filter}" | tee -a "${TOPDIR}/files.info"
done


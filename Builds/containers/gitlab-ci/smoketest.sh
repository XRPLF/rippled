#!/usr/bin/env sh
set -ex
install_from=$1
use_private=${2:-0} # this option not currently needed by any CI scripts,
                    # reserved for possible future use
if [ "$use_private" -gt 0 ] ; then
    REPO_ROOT="https://rippled:${ARTIFACTORY_DEPLOY_KEY_RIPPLED}@${ARTIFACTORY_HOST}/artifactory"
else
    REPO_ROOT="${PUBLIC_REPO_ROOT}"
fi

. ./Builds/containers/gitlab-ci/get_component.sh

. /etc/os-release
case ${ID} in
    ubuntu|debian)
        pkgtype="dpkg"
        ;;
    fedora|centos|rhel|scientific)
        pkgtype="rpm"
        ;;
    *)
        echo "unrecognized distro!"
        exit 1
        ;;
esac

if [ "${pkgtype}" = "dpkg" ] ; then
    if [ "${install_from}" = "repo" ] ; then
        apt -y upgrade
        apt -y update
        apt -y install apt apt-transport-https ca-certificates coreutils util-linux wget gnupg
        wget -q -O - "${REPO_ROOT}/api/gpg/key/public" | apt-key add -
        echo "deb ${REPO_ROOT}/${DEB_REPO} ${DISTRO} ${COMPONENT}" >> /etc/apt/sources.list
        # sometimes update fails and requires a cleanup
        if ! apt -y update ; then
          rm -rvf /var/lib/apt/lists/*
          apt-get clean
          apt -y update
        fi
        apt-get -y install rippled
    elif [ "${install_from}" = "local" ] ; then
        # cached pkg install
        apt -y update
        apt -y install libprotobuf-dev libssl-dev
        rm -f build/dpkg/packages/rippled-dbgsym*.*
        dpkg --no-debsig -i build/dpkg/packages/*.deb
    else
        echo "unrecognized pkg source!"
        exit 1
    fi
else
    yum -y update
    if [ "${install_from}" = "repo" ] ; then
        yum -y install yum-utils coreutils util-linux
        REPOFILE="/etc/yum.repos.d/artifactory.repo"
        echo "[Artifactory]" > ${REPOFILE}
        echo "name=Artifactory" >> ${REPOFILE}
        echo "baseurl=${REPO_ROOT}/${RPM_REPO}/${COMPONENT}/" >> ${REPOFILE}
        echo "enabled=1" >> ${REPOFILE}
        echo "gpgcheck=0" >> ${REPOFILE}
        echo "gpgkey=${REPO_ROOT}/${RPM_REPO}/${COMPONENT}/repodata/repomd.xml.key" >> ${REPOFILE}
        echo "repo_gpgcheck=1" >> ${REPOFILE}
        yum -y update
        yum -y install rippled
    elif [ "${install_from}" = "local" ] ; then
        # cached pkg install
        yum install -y yum-utils openssl-static zlib-static
        rm -f build/rpm/packages/rippled-debug*.rpm
        rm -f build/rpm/packages/*.src.rpm
        rpm -i build/rpm/packages/*.rpm
    else
        echo "unrecognized pkg source!"
        exit 1
    fi
fi

# verify installed version
INSTALLED=$(/opt/ripple/bin/rippled --version | awk '{print $NF}')
. build/${pkgtype}/packages/build_vars
if [ "${rippled_version}" != "${INSTALLED}" ] ; then
    echo "INSTALLED version ${INSTALLED} does not match ${rippled_version}"
    exit 1
fi
# run unit tests
/opt/ripple/bin/rippled --unittest --unittest-jobs $(nproc)
/opt/ripple/bin/validator-keys --unittest



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
    fedora|centos|rhel|scientific|rocky)
        pkgtype="rpm"
        ;;
    *)
        echo "unrecognized distro!"
        exit 1
        ;;
esac

# this script provides info variables about pkg version
. build/${pkgtype}/packages/build_vars

if [ "${pkgtype}" = "dpkg" ] ; then
    # sometimes update fails and requires a cleanup
    updateWithRetry()
    {
        if ! apt-get -y update ; then
            rm -rvf /var/lib/apt/lists/*
            apt-get -y clean
            apt-get -y update
        fi
    }
    if [ "${install_from}" = "repo" ] ; then
        apt-get -y upgrade
        updateWithRetry
        apt-get -y install apt apt-transport-https ca-certificates coreutils util-linux wget gnupg
        wget -q -O - "${REPO_ROOT}/api/gpg/key/public" | apt-key add -
        echo "deb ${REPO_ROOT}/${DEB_REPO} ${DISTRO} ${COMPONENT}" >> /etc/apt/sources.list
        updateWithRetry
        # uncomment this next line if you want to see the available package versions
        # apt-cache policy rippled
        apt-get -y install rippled=${dpkg_full_version}
    elif [ "${install_from}" = "local" ] ; then
        # cached pkg install
        updateWithRetry
        apt-get -y install libprotobuf-dev libssl-dev
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
        # uncomment this next line if you want to see the available package versions
        # yum --showduplicates list rippled
        yum -y install ${rpm_version_release}
    elif [ "${install_from}" = "local" ] ; then
        # cached pkg install
        pkgs=("yum-utils openssl-static zlib-static")
        if [ "$ID" = "rocky" ]; then
            yum config-manager --set-enabled powertools
            pkgs="${pkgs[@]/openssl-static}"
        fi
        yum install -y $pkgs
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
if [ "${rippled_version}" != "${INSTALLED}" ] ; then
    echo "INSTALLED version ${INSTALLED} does not match ${rippled_version}"
    exit 1
fi
# run unit tests
/opt/ripple/bin/rippled --unittest --unittest-jobs $(nproc)
/opt/ripple/bin/validator-keys --unittest

#!/usr/bin/env bash
# Part of Vagrant virtual development environments for SOCI

# Installs DB2 CLI Driver 10.5 (64-bit and 32-bit) from
# IBM Data Server Driver Package (DS Driver)

# Prerequisities (manual download if wget fails):
# 1. Go to http://www-01.ibm.com/support/docview.wss?uid=swg21385217
# 2. Go to "IBM Data Server Driver Package (DS Driver)"
# 3. Download "IBM Data Server Driver Package (Linux AMD64 and Intel EM64T)"
# 4. Copy the package to '{SOCI SOURCE TREE ROOT}/tmp' directory.
DSPKG="ibm_data_server_driver_package_linuxx64_v10.5.tar.gz"
DSPREFIX=/opt/ibm
DSDRIVER=${DSPREFIX}/dsdriver
SOCITMP=/vagrant/tmp

# Try to download from known location
echo "DB2CLI: downloading ${DSPKG} from github.com/rorymckinley/gold_importer"
wget -q https://raw.githubusercontent.com/rorymckinley/gold_importer/master/3rdparty/ibm_data_server_driver_package_linuxx64_v10.5.tar.gz

# Check if driver package is available
if [[ ! -f ${DSPKG} && ! -f ${SOCITMP}/${DSPKG} ]]; then
  echo "DB2CLI: missing ${SOCITMP}/${DSPKG}"
  echo "DB2CLI: try manual download, then provision VM to install"
  echo "DB2CLI: meanwhile, skipping driver installation"
  exit 0
fi

if [ -f ${SOCITMP}/${DSPKG} ]; then
    DSPKG=${SOCITMP}/${DSPKG}
fi

# If the drivers is already installed, skip re-installation
# because installDSDriver script does not support it
if [[ -d "${DSDRIVER}" && -f ${DSDRIVER}/db2profile ]]; then
  echo "DB2CLI: ${DSDRIVER} already installed, skipping"
  echo "DB2CLI: if necessary, upgrade manually running: ${DSDRIVER}/installDSDriver -upgrade ${DSDRIVER}"
  exit 0
fi

# Korn shell required by installDSDriver script
sudo apt-get -o Dpkg::Options::='--force-confnew' -y -q install \
  ksh

echo "DB2CLI: unpacking ${DSPKG} to ${DSPREFIX}"
sudo mkdir -p ${DSPREFIX}
sudo tar -zxf ${DSPKG} -C ${DSPREFIX}
echo "DB2CLI: running ${DSDRIVER}/installDSDriver script"
sudo ${DSDRIVER}/installDSDriver
echo "DB2CLI: cat ${DSDRIVER}/installDSDriver.log"
cat ${DSDRIVER}/installDSDriver.log
echo
echo "DB2CLI: installing ${DSDRIVER}/db2profile in /etc/profile.d/db2profile.sh for 64-bit env"
sudo cp ${DSDRIVER}/db2profile /etc/profile.d/db2profile.sh
echo "DB2CLI: For 32-bit, manually source ${DSDRIVER}/db2profile32 to overwrite 64-bit env"

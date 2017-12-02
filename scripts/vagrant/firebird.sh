#!/usr/bin/env bash
# Part of Vagrant virtual development environments for SOCI

# Installs PostgreSQL with 'soci' user and database
# Pre-installation
source /vagrant/scripts/vagrant/common.env
export DEBIAN_FRONTEND="noninteractive"
# FIXME: these debconf lines should automate the Firebird config but do/may not :(
# However, keep them enabled to allow smooth(er) apt-get/dpkg operations.
# Othwerise, installation of firebird packages may fail, randomly.
# We work around this bug with Expect script below used to update SYSDBA password.
sudo debconf-set-selections <<< "firebird2.5-super shared/firebird/enabled boolean true"
sudo debconf-set-selections <<< "firebird2.5-super shared/firebird/sysdba_password/first_install password masterkey"
# Installation
sudo apt-get -o Dpkg::Options::='--force-confnew' -y -q install \
  expect \
  firebird2.5-super
# Post-installation
FB_ORIGINAL_PASS=`sudo cat /etc/firebird/2.5/SYSDBA.password \
  | grep ISC_PASSWORD | sed -e 's/ISC_PASSWORD="\([^"]*\)".*/\1/'`
echo "Firebird: dpkg-reconfigure resetting sysdba password from ${FB_ORIGINAL_PASS} to ${SOCI_PASS}"
export DEBIAN_FRONTEND="readline"
# Expect script feeding dpkg-reconfigure prompts
sudo /usr/bin/expect - << ENDMARK > /dev/null
spawn dpkg-reconfigure firebird2.5-super -freadline
expect "Enable Firebird server?"
send "Y\r"

expect "Password for SYSDBA:"
send "${SOCI_PASS}\r"

# done
expect eof
ENDMARK
# End of Expect script
export DEBIAN_FRONTEND="noninteractive"
echo "Firebird: cat /etc/firebird/2.5/SYSDBA.password"
sudo cat /etc/firebird/2.5/SYSDBA.password | grep ISC_
echo
echo "Firebird: creating user ${SOCI_USER}"
sudo gsec -user sysdba -pass ${SOCI_PASS} -delete ${SOCI_USER}
sudo gsec -user sysdba -pass ${SOCI_PASS} -add ${SOCI_USER} -pw ${SOCI_PASS} -admin yes
echo "Firebird: creating database /tmp/${SOCI_USER}.fdb"
sudo rm -f /tmp/${SOCI_USER}.fdb
echo "CREATE DATABASE \"LOCALHOST:/tmp/${SOCI_USER}.fdb\";" \
  | isql-fb -q -u ${SOCI_USER} -p ${SOCI_PASS}
echo "Firebird: restarting"
sudo service firebird2.5-super restart
echo "Firebird: DONE"

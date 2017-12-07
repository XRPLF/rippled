#!/usr/bin/env bash
# Part of Vagrant virtual development environments for SOCI

# Installs MySQL with 'soci' user and database
# Pre-installation
source /vagrant/scripts/vagrant/common.env
export DEBIAN_FRONTEND="noninteractive"
sudo debconf-set-selections <<< "mysql-server mysql-server/root_password password ${SOCI_PASS}"
sudo debconf-set-selections <<< "mysql-server mysql-server/root_password_again password ${SOCI_PASS}"
# Installation
sudo apt-get -o Dpkg::Options::='--force-confnew' -y -q install \
  mysql-server
# Post-installation
echo "MySQL: updating /etc/mysql/my.cnf"
sudo sed -i "s/bind-address.*/bind-address = 0.0.0.0/" /etc/mysql/my.cnf
echo "MySQL: setting root password to ${SOCI_PASS}"
mysql -uroot -p${SOCI_PASS} -e \
  "GRANT ALL PRIVILEGES ON *.* TO 'root'@'%' IDENTIFIED BY '${SOCI_PASS}' WITH GRANT OPTION; FLUSH PRIVILEGES;"
echo "MySQL: creating user ${SOCI_USER}"
mysql -uroot -p${SOCI_PASS} -e \
  "GRANT ALL PRIVILEGES ON ${SOCI_USER}.* TO '${SOCI_USER}'@'%' IDENTIFIED BY '${SOCI_PASS}' WITH GRANT OPTION"
mysql -uroot -p${SOCI_PASS} -e "DROP DATABASE IF EXISTS ${SOCI_USER}"
echo "MySQL: creating database ${SOCI_USER}"
mysql -uroot -p${SOCI_PASS} -e "CREATE DATABASE ${SOCI_USER}"
echo "MySQL: restarting"
sudo service mysql restart
echo "MySQL: DONE"

#!/usr/bin/env bash
# Part of Vagrant virtual development environments for SOCI

# Installs PostgreSQL with 'soci' user and database
# Pre-installation
source /vagrant/scripts/vagrant/common.env
export DEBIAN_FRONTEND="noninteractive"
# Installation
sudo apt-get -o Dpkg::Options::='--force-confnew' -y -q install \
  postgresql \
  postgresql-contrib
# Post-installation
echo "PostgreSQL: updating /etc/postgresql/9.3/main/postgresql.conf"
sudo sed -i "s/#listen_address.*/listen_addresses '*'/" /etc/postgresql/9.3/main/postgresql.conf
echo "PostgreSQL: updating /etc/postgresql/9.3/main/pg_hba.conf"
sudo cat >> /etc/postgresql/9.3/main/pg_hba.conf <<EOF
# Accept all IPv4 connections - DEVELOPMENT ONLY
host    all         all         0.0.0.0/0             md5
EOF
echo "PostgreSQL: creating user ${SOCI_USER}"
sudo -u postgres psql -c "CREATE ROLE ${SOCI_USER} WITH LOGIN SUPERUSER CREATEDB ENCRYPTED PASSWORD '${SOCI_PASS}'"
echo "PostgreSQL: creating database ${SOCI_USER}"
sudo -u postgres dropdb --if-exists ${SOCI_USER}
sudo -u postgres createdb ${SOCI_USER} --owner=${SOCI_USER}
echo "PostgreSQL: restarting"
sudo service postgresql restart
echo "PostgreSQL: DONE"

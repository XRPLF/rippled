#!/usr/bin/env bash
# Part of Vagrant virtual development environments for SOCI

# Installs essentials and core dependencies
# Pre-installation
source /vagrant/scripts/vagrant/common.env
export DEBIAN_FRONTEND="noninteractive"
# Trusty has CMake 2.8, we need CMake 3
sudo apt-get install software-properties-common
sudo add-apt-repository ppa:george-edison55/cmake-3.x
sudo apt-get update -y -q
# Installation
sudo apt-get -o Dpkg::Options::='--force-confnew' -y -q install \
  build-essential \
  cmake \
  firebird2.5-dev \
  git \
  libboost-dev \
  libboost-date-time-dev \
  libmyodbc \
  libmysqlclient-dev \
  libpq-dev \
  libsqlite3-dev \
  odbc-postgresql \
  unixodbc-dev \
  valgrind
# Post-installation

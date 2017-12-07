#!/usr/bin/env bash
# Part of Vagrant virtual development environments for SOCI

# Pre-installation
echo "Bootstrap: setting common environment in /etc/profile.d/vagrant-soci.sh"
sudo sh -c "cat /vagrant/scripts/vagrant/common.env > /etc/profile.d/vagrant-soci.sh"
export DEBIAN_FRONTEND="noninteractive"
# Installation
# TODO: Switch to apt-fast when it is avaiable for Trusty
sudo apt-get update -y -q
sudo apt-get -o Dpkg::Options::='--force-confnew' -y -q install \
  build-essential \
  avahi-daemon \
  zip
# Post-installation
## Configure Avahi to enable .local hostnames used to connect between VMs.
echo "Bootstrap: updating /etc/nsswitch.conf to configure Avahi/MDNS for .local lookup"
sudo sed -i 's/hosts:.*/hosts:          files mdns4_minimal [NOTFOUND=return] dns myhostname/g' /etc/nsswitch.conf

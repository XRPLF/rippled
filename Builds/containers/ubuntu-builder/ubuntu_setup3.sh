#!/usr/bin/env bash

set -o errexit
set -o nounset
set -o xtrace

apt-get install --yes build-essential fakeroot devscripts cmake debhelper dh-systemd

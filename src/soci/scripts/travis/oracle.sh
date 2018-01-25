# Definitions used by SOCI when building Oracle backend at travis-ci.org
#
# Copyright (c) 2015 Vadim Zeitlin <vz-soci@zeitlins.org>
#
# Notice that this file is not executable, it is supposed to be sourced from
# the other files.

# Oracle environment required by https://github.com/cbandy/travis-oracle
export ORACLE_COOKIE=sqldev
export ORACLE_FILE=oracle11g/xe/oracle-xe-11.2.0-1.0.x86_64.rpm.zip
export ORACLE_HOME=/u01/app/oracle/product/11.2.0/xe
export ORACLE_SID=XE

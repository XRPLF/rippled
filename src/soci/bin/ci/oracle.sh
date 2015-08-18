# Definitions used by SOCI when building Oracle backend at travis-ci.org
#
# Copyright (c) 2015 Vadim Zeitlin <vz-soci@zeitlins.org>
#
# Notice that this file is not executable, it is supposed to be sourced from
# the other files.

# Load Oracle environment variables
. /usr/lib/oracle/xe/app/oracle/product/10.2.0/server/bin/oracle_env.sh
echo "ORACLE_HOME=${ORACLE_HOME}"
echo "ORACLE_SID=${ORACLE_SID}"

LD_LIBRARY_PATH=${ORACLE_HOME}:${LD_LIBRARY_PATH}
export LD_LIBRARY_PATH

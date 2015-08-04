#!/bin/bash
# Sets up environment for p6psy backend Oracle at travis-ci.org
#
# Copyright (c) 2013 Peter Butkovic <butkovic@gmail.com>
#
# Modified by Mateusz Loskot <mateusz@loskot.net>
# Changes:
# - Check connection as user for testing
#

# for some reason the file is not found any more here => creating user in install script
# Load Oracle environment variables so that we could run `sqlplus`.
. /usr/lib/oracle/xe/app/oracle/product/10.2.0/server/bin/oracle_env.sh
echo "ORACLE_HOME=${ORACLE_HOME}"
echo "ORACLE_SID=${ORACLE_SID}"

# create user for testing
echo "CREATE USER travis IDENTIFIED BY travis;" | \
sqlplus -S -L sys/admin AS SYSDBA

echo "grant connect, resource to travis;" | \
sqlplus -S -L sys/admin AS SYSDBA

echo "grant create session, alter any procedure to travis;" | \
sqlplus -S -L sys/admin AS SYSDBA

# to enable xa recovery, see: https://community.oracle.com/thread/378954
echo "grant select on sys.dba_pending_transactions to travis;" | \
sqlplus -S -L sys/admin AS SYSDBA
echo "grant select on sys.pending_trans$ to travis;" | \
sqlplus -S -L sys/admin AS SYSDBA
echo "grant select on sys.dba_2pc_pending to travis;" | \
sqlplus -S -L sys/admin AS SYSDBA
echo "grant execute on sys.dbms_system to travis;" | \
sqlplus -S -L sys/admin AS SYSDBA

# increase default=40 value of processes to prevent ORA-12520 failures while testing
echo "alter system set processes=100 scope=spfile;" | \
sqlplus -S -L sys/admin AS SYSDBA

# check connection as user for testing
echo "Connecting using travis/travis@XE"
echo "SELECT * FROM product_component_version;" | \
sqlplus -S -L travis/travis@XE


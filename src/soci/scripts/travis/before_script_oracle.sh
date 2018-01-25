#!/bin/bash
# Sets up environment for p6psy backend Oracle at travis-ci.org
#
# Copyright (c) 2013 Peter Butkovic <butkovic@gmail.com>
#
# Modified by Mateusz Loskot <mateusz@loskot.net>
# Changes:
# - Check connection as user for testing
#
source ${TRAVIS_BUILD_DIR}/scripts/travis/oracle.sh
echo "ORACLE_HOME=${ORACLE_HOME}"
echo "ORACLE_SID=${ORACLE_SID}"

# travis-oracle installer created travis user w/o password
echo "ALTER USER travis IDENTIFIED BY travis;" | \
$ORACLE_HOME/bin/sqlplus -S -L sys/admin AS SYSDBA

echo "grant connect, resource to travis;" | \
$ORACLE_HOME/bin/sqlplus -S -L sys/admin AS SYSDBA

echo "grant create session, alter any procedure to travis;" | \
$ORACLE_HOME/bin/sqlplus -S -L sys/admin AS SYSDBA

# to enable xa recovery, see: https://community.oracle.com/thread/378954
echo "grant select on sys.dba_pending_transactions to travis;" | \
$ORACLE_HOME/bin/sqlplus -S -L sys/admin AS SYSDBA
echo "grant select on sys.pending_trans$ to travis;" | \
$ORACLE_HOME/bin/sqlplus -S -L sys/admin AS SYSDBA
echo "grant select on sys.dba_2pc_pending to travis;" | \
$ORACLE_HOME/bin/sqlplus -S -L sys/admin AS SYSDBA
echo "grant execute on sys.dbms_system to travis;" | \
$ORACLE_HOME/bin/sqlplus -S -L sys/admin AS SYSDBA

# increase default=40 value of processes to prevent ORA-12520 failures while testing
echo "alter system set processes=100 scope=spfile;" | \
$ORACLE_HOME/bin/sqlplus -S -L sys/admin AS SYSDBA

# check connection as user for testing
echo "Connecting using travis/travis@XE"
echo "SELECT * FROM product_component_version;" | \
$ORACLE_HOME/bin/sqlplus -S -L travis/travis@XE

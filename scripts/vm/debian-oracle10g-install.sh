#/bin/sh
# Script performs non-interactive instllation of Oracle XE 10g on Debian
#
# Based on oracle10g-update.sh from HTSQL project:
# https://bitbucket.org/prometheus/htsql
#
# Modified by Mateusz Loskot <mateusz@loskot.net>
# Changes:
# - Add fake swap support (backup /usr/bin/free manually anyway!)
#
set -ex

#
# Utilities
#
function free_backup()
{
    # Multiple copies to be on safe side
    cp /usr/bin/free /root
    mv /usr/bin/free /usr/bin/free.original
}

function free_restore()
{
    cp /usr/bin/free.original /usr/bin/free
}

# Install fake free
# http://www.axelog.de/2010/02/7-oracle-ee-refused-to-install-into-openvz/
free_backup
cat <<EOF >> /usr/bin/free
#!/bin/sh
cat <<__eof
             total       used       free     shared    buffers     cached
Mem:       1048576     327264     721312          0          0          0
-/+ buffers/cache:     327264     721312
Swap:      2000000          0    2000000
__eof
exit
EOF
chmod 755 /usr/bin/free

# Enable HTTPS for APT repositories.
apt-get -q update
apt-get -qy install apt-transport-https

# Register the Oracle repository.
echo "deb https://oss.oracle.com/debian/ unstable main non-free" >/etc/apt/sources.list.d/oracle.list
wget -q https://oss.oracle.com/el4/RPM-GPG-KEY-oracle -O- | apt-key add -
apt-get -q update

# Install the Oracle 10g Express Edition.
apt-get -qy install oracle-xe

# Clean APT cache.
apt-get clean

# Fix the problem when the configuration script eats the last
# character of the password if it is 'n': replace IFS="\n" with IFS=$'\n'.
sed -i -e s/IFS=\"\\\\n\"/IFS=\$\'\\\\n\'/ /etc/init.d/oracle-xe

# Configure the server; provide the answers for the following questions:
# The HTTP port for Oracle Application Express: 8080
# A port for the database listener: 1521
# The password for the SYS and SYSTEM database accounts: admin
# Start the server on boot: yes
/etc/init.d/oracle-xe configure <<END
8080
1521
admin
admin
y
END

# Load Oracle environment variables so that we could run `sqlplus`.
. /usr/lib/oracle/xe/app/oracle/product/10.2.0/server/bin/oracle_env.sh

# Increase the number of connections.
echo "ALTER SYSTEM SET PROCESSES=40 SCOPE=SPFILE;" | \
sqlplus -S -L sys/admin AS SYSDBA

# Set Oracle environment variables on login.
cat <<END >>/root/.bashrc

. /usr/lib/oracle/xe/app/oracle/product/10.2.0/server/bin/oracle_env.sh
END

free_restore


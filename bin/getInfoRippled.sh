#!/usr/bin/env bash

TMP_LOC=$(mktemp -d /tmp/ripple_info.XXXX)
echo ${TMP_LOC}

cd ${TMP_LOC}

# Send output from this script to a log file
## this captures any messages
## or errors from the script itself

LOG_FILE=${TMP_LOC}/get_info.log
exec 3>&1 1>>${LOG_FILE} 2>&1

## Send all stdout files to /tmp

/opt/ripple/bin/rippled server_info > ${TMP_LOC}/server_info.txt
df -h                               > ${TMP_LOC}/free_disk_space.txt
cat /proc/meminfo                   > ${TMP_LOC}/amount_mem.txt
cat /proc/swaps                     > ${TMP_LOC}/swap_space.txt
ulimit -a                           > ${TMP_LOC}/reported_current_limits.txt

TMP_CONF=${TMP_LOC}/rippled.cfg

cp /etc/opt/ripple/rippled.cfg ${TMP_CONF}

SEED_LINE_NUM=$(grep -n "\[validation_seed\]" ${TMP_CONF} | tail -n 1 | awk -F ':' '{print $1}')
SEED_LINE_NUM=$((SEED_LINE_NUM + 1))

awk -v SEED_LINE_NUM=${SEED_LINE_NUM} 'NR != SEED_LINE_NUM' "${TMP_CONF}" > ${TMP_LOC}/cleaned_rippled_cfg.txt

if [[ "$(cat /sys/block/xvda/queue/rotational)" = 0 ]]
then
        echo "SSD exists"  > ${TMP_LOC}/is_ssd.txt
else
        echo "No SSD"      > ${TMP_LOC}/is_ssd.txt
fi

tar -czvf info-package.tar.gz *.txt *.log

echo "Use the following command on your local machine to download from your rippled instance: scp <remote_rippled_username>@<remote_host>:${TMP_LOC}/info-package.tar.gz <path/to/local_machine/directory>"| tee /dev/fd/3

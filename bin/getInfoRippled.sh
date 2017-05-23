#!/usr/bin/env bash

rippled_exe=/opt/ripple/bin/rippled
conf_file=/etc/opt/ripple/rippled.cfg

while getopts ":e:c:" opt; do
    case $opt in
        e)
            rippled_exe=${OPTARG}
            ;;
        c)
            conf_file=${OPTARG}
            ;;
        \?)
            echo "Invalid option: -$OPTARG"
    esac
done

tmp_loc=$(mktemp -d --tmpdir ripple_info.XXXX)
cd /tmp
chmod 751 ripple_info.*
cd ~
echo ${tmp_loc}

cleaned_conf=${tmp_loc}/cleaned_rippled_cfg.txt

if [[ -f ${conf_file} ]]
then
    db=$(sed -r -e 's/\<s[a-zA-Z0-9]{28}\>/secretsecretsecretsecretmaybe/g' ${conf_file} |\
            awk -v OUT_FILE=${cleaned_conf} '
    BEGIN {skip=0; db_path="";print > OUT_FILE}
    /^\[validation_seed\]/ {skip=1; next}
    /^\[node_seed\]/ {skip=1; next}
    /^\[validation_manifest\]/ {skip=1; next}
    /^\[validator_token\]/ {skip=1; next}
    /^\[.*\]/ {skip=0}
    skip==1 {next}
    save==1 {save=0;db_path=$0}
    /^\[database_path\]/ {save=1}
    {print >> OUT_FILE}
    END {print db_path}
    ')
fi

echo "database_path: ${db}"
df ${db} > ${tmp_loc}/db_path_df.txt
echo

# Send output from this script to a log file
## this captures any messages
## or errors from the script itself

log_file=${tmp_loc}/get_info.log
exec 3>&1 1>>${log_file} 2>&1

## Send all stdout files to /tmp

if [[ -x ${rippled_exe} ]]
then
    pgrep rippled && \
    ${rippled_exe} --conf ${conf_file} \
    -- server_info                  > ${tmp_loc}/server_info.txt
fi

df -h                               > ${tmp_loc}/free_disk_space.txt
cat /proc/meminfo                   > ${tmp_loc}/amount_mem.txt
cat /proc/swaps                     > ${tmp_loc}/swap_space.txt
ulimit -a                           > ${tmp_loc}/reported_current_limits.txt

for dev_path in $(df | awk '$1 ~ /^\/dev\// {print $1}'); do
    # strip numbers from end and remove '/dev/'
    dev=$(basename ${dev_path%%[0-9]})
    if [[ "$(cat /sys/block/${dev}/queue/rotational)" = 0 ]]
    then
        echo "${dev} : SSD" >> ${tmp_loc}/is_ssd.txt
    else
        echo "${dev} : NO SSD" >> ${tmp_loc}/is_ssd.txt
    fi
done

pushd ${tmp_loc}
tar -czvf info-package.tar.gz *.txt *.log
popd

echo "Use the following command on your local machine to download from your rippled instance: scp <remote_rippled_username>@<remote_host>:${tmp_loc}/info-package.tar.gz <path/to/local_machine/directory>"| tee /dev/fd/3


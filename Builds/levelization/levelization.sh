#!/bin/bash

# Usage: levelization.sh
# This script takes no parameters, reads no environment variables,
# and can be run from any directory, as long as it is in the expected
# location in the repo.

pushd $( dirname $0 )

if [ -v PS1 ]
then
  # if the shell is interactive, clean up any flotsam before analyzing
  git clean -ix
fi

rm -rfv results
mkdir results
includes="$( pwd )/results/rawincludes.txt"
pushd ../..
echo Raw includes:
grep -r '#include.*/.*\.h' include src | \
    grep -v boost | tee ${includes}
popd
pushd results

oldifs=${IFS}
IFS=:
mkdir includes
mkdir includedby
echo Build levelization paths
exec 3< ${includes} # open rawincludes.txt for input
while read -r -u 3 file include
do
    level=$( echo ${file} | cut -d/ -f 2,3 )
    # If the "level" indicates a file, cut off the filename
    if [[ "${level##*.}" != "${level}" ]]
    then
        # Use the "toplevel" label as a workaround for `sort`
        # inconsistencies between different utility versions
        level="$( dirname ${level} )/toplevel"
    fi
    level=$( echo ${level} | tr '/' '.' )

    includelevel=$( echo ${include} | sed 's/.*["<]//; s/[">].*//' | \
        cut -d/ -f 1,2 )
    if [[ "${includelevel##*.}" != "${includelevel}" ]]
    then
        # Use the "toplevel" label as a workaround for `sort`
        # inconsistencies between different utility versions
        includelevel="$( dirname ${includelevel} )/toplevel"
    fi
    includelevel=$( echo ${includelevel} | tr '/' '.' )

    if [[ "$level" != "$includelevel" ]]
    then
        echo $level $includelevel | tee -a paths.txt
    fi
done
echo Sort and dedup paths
sort -ds paths.txt | uniq -c | tee sortedpaths.txt
mv sortedpaths.txt paths.txt
exec 3>&- #close fd 3
IFS=${oldifs}
unset oldifs

echo Split into flat-file database
exec 4<paths.txt # open paths.txt for input
while read -r -u 4 count level include
do
    echo ${include} ${count} | tee -a includes/${level}
    echo ${level} ${count} | tee -a includedby/${include}
done
exec 4>&- #close fd 4

loops="$( pwd )/loops.txt"
ordering="$( pwd )/ordering.txt"
pushd includes
echo Search for loops
# Redirect stdout to a file
exec 4>&1
exec 1>"${loops}"
for source in *
do
  if [[ -f "$source" ]]
  then
    exec 5<"${source}" # open for input
    while read -r -u 5 include includefreq
    do
      if [[ -f $include ]]
      then
        if grep -q -w $source $include
        then
          if grep -q -w "Loop: $include $source" "${loops}"
          then
            continue
          fi
          sourcefreq=$( grep -w $source $include | cut -d\  -f2 )
          echo "Loop: $source $include"
          # If the counts are close, indicate that the two modules are
          # on the same level, though they shouldn't be
          if [[ $(( $includefreq - $sourcefreq )) -gt 3 ]]
          then
              echo -e "  $source > $include\n"
          elif [[ $(( $sourcefreq - $includefreq )) -gt 3 ]]
          then
              echo -e "  $include > $source\n"
          elif [[ $sourcefreq -eq $includefreq ]]
          then
              echo -e "  $include == $source\n"
          else
              echo -e "  $include ~= $source\n"
          fi
        else
          echo "$source > $include" >> "${ordering}"
        fi
      fi
    done
    exec 5>&- #close fd 5
  fi
done
exec 1>&4 #close fd 1
exec 4>&- #close fd 4
cat "${ordering}"
cat "${loops}"
popd
popd
popd

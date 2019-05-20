#!/usr/bin/env bash
# run our build script in a docker container
# using travis-ci hosts
set -eux

function join_by { local IFS="$1"; shift; echo "$*"; }

set +x
echo "VERBOSE_BUILD=true" > /tmp/co.env
matchers=(
   'TRAVIS.*' 'CI' 'CC' 'CXX'
   'BUILD_TYPE' 'TARGET' 'MAX_TIME'
   'CODECOV.+' 'CMAKE.*' '.+_TESTS'
   '.+_OPTIONS' 'NINJA.*' 'NUM_.+'
   'NIH_.+' 'BOOST.*' '.*CCACHE.*')

matchstring=$(join_by '|' "${matchers[@]}")
echo "MATCHSTRING IS:: $matchstring"
env | grep -E "^(${matchstring})=" >> /tmp/co.env
set -x
# need to eliminate TRAVIS_CMD...don't want to pass it to the container
cat /tmp/co.env | grep -v TRAVIS_CMD > /tmp/co.env.2
mv /tmp/co.env.2 /tmp/co.env
cat /tmp/co.env
mkdir -p -m 0777 ${TRAVIS_BUILD_DIR}/cores
echo "${TRAVIS_BUILD_DIR}/cores/%e.%p" | sudo tee /proc/sys/kernel/core_pattern
docker run \
    -t --env-file /tmp/co.env \
    -v ${TRAVIS_HOME}:${TRAVIS_HOME} \
    -w ${TRAVIS_BUILD_DIR} \
    --cap-add SYS_PTRACE \
    --ulimit "core=-1" \
    $DOCKER_IMAGE \
    /bin/bash -c 'if [[ $CC =~ ([[:alpha:]]+)-([[:digit:].]+) ]] ; then sudo update-alternatives --set ${BASH_REMATCH[1]} /usr/bin/$CC; fi; bin/ci/ubuntu/build-and-test.sh'



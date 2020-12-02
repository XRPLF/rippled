#!/usr/bin/env bash
# some cached files create churn, so save them here for
# later restoration before packing the cache
set -eux
clean_cache="travis_clean_cache"
if [[ ! ( "${TRAVIS_JOB_NAME}" =~ "windows" || \
    "${TRAVIS_JOB_NAME}" =~ "prereq-keep" ) ]] && \
    ( [[ "${TRAVIS_COMMIT_MESSAGE}" =~ "${clean_cache}" ]] || \
        ( [[ -v TRAVIS_PULL_REQUEST_SHA && \
            "${TRAVIS_PULL_REQUEST_SHA}" != "" ]] && \
          git log -1 "${TRAVIS_PULL_REQUEST_SHA}" | grep -cq "${clean_cache}" -
        )
    )
then
    find ${TRAVIS_HOME}/_cache -maxdepth 2 -type d
    rm -rf ${TRAVIS_HOME}/_cache
    mkdir -p ${TRAVIS_HOME}/_cache
fi

pushd ${TRAVIS_HOME}
if [ -f cache_ignore.tar ] ; then
    rm -f cache_ignore.tar
fi

if [ -d _cache/nih_c ] ; then
    find _cache/nih_c -name "build.ninja" | tar rf cache_ignore.tar --files-from -
    find _cache/nih_c -name ".ninja_deps" | tar rf cache_ignore.tar --files-from -
    find _cache/nih_c -name ".ninja_log" | tar rf cache_ignore.tar --files-from -
    find _cache/nih_c -name "*.log" | tar rf cache_ignore.tar --files-from -
    find _cache/nih_c -name "*.tlog" | tar rf cache_ignore.tar --files-from -
    # show .a files in the cache, for sanity checking
    find _cache/nih_c -name "*.a" -ls
fi

if [ -d _cache/ccache ] ; then
    find _cache/ccache -name "stats" | tar rf cache_ignore.tar --files-from -
fi

if [ -f cache_ignore.tar ] ; then
    tar -tf cache_ignore.tar
fi
popd



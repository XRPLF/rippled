#!/usr/bin/env groovy

import groovy.json.JsonOutput
import java.text.*

def all_status = [:]
def commit_id = ''
// this is not the actual token, but an ID/key into the jenkins
// credential store which httpRequest can access.
def github_cred = '6bd3f3b9-9a35-493e-8aef-975403c82d3e'
//
// root API url for our repo (default, overriden below)
//
String github_repo = 'https://api.github.com/repos/ripple/rippled'

try {
    stage ('Startup Checks') {
        // here we check the commit author against collaborators
        // we need a node to do this because readJSON requires
        // a filesystem (although we just pass it text)
        node {
            checkout scm
            commit_id = sh(
                script: 'git rev-parse HEAD',
                returnStdout: true)
            commit_id = commit_id.trim()
            echo "commit ID is ${commit_id}"
            commit_log = sh (
                 script: "git show --name-status ${commit_id}",
                 returnStdout: true)
            printGitInfo (commit_id, commit_log)
            //
            // NOTE this getUserRemoteConfigs call requires a one-time
            // In-process Script Approval (configure jenkins). We need this
            // to detect the remote repo to interact with via the github API.
            //
            def remote_url = scm.getUserRemoteConfigs()[0].getUrl()
            if (remote_url) {
                echo "GIT URL scm: $remote_url"
                def fork = remote_url.tokenize('/')[2]
                def repo = remote_url.tokenize('/')[3].split('\\.')[0]
                echo "GIT FORK: $fork"
                echo "GIT REPO: $repo"
                github_repo = "https://api.github.com/repos/${fork}/${repo}"
                echo "API URL REPO: $github_repo"
            }

            if (env.CHANGE_AUTHOR) {
                //
                // this means we have some sort of PR , so verify the author
                //
                echo "CHANGE AUTHOR ---> $CHANGE_AUTHOR"
                echo "CHANGE TARGET ---> $CHANGE_TARGET"
                echo "CHANGE ID ---> $CHANGE_ID"
                //
                // check the commit author against collaborators
                // we need a node to do this because readJSON requires
                // a filesystem (although we just pass it text)
                //
                def response = httpRequest(
                    timeout: 10,
                    authentication: github_cred,
                    url: "${github_repo}/collaborators")
                def collab_data = readJSON(
                    text: response.content)
                collab_found = false;
                for (collaborator in collab_data) {
                    if (collaborator['login'] == "$CHANGE_AUTHOR") {
                        echo "$CHANGE_AUTHOR is a collaborator!"
                        collab_found = true;
                        break;
                    }
                }

                if (! collab_found) {
                    manager.addShortText(
                        "Author of this change is not a collaborator!",
                        "Crimson",
                        "white",
                        "0px",
                        "white")
                    all_status['startup'] =
                        [false, 'Author Check', "$CHANGE_AUTHOR is not a collaborator!"]
                    error "$CHANGE_AUTHOR does not appear to be a collaborator...bailing on this build"
                }
            }
        }
    }

    stage ('Parallel Build') {
        String[][] variants = [
            ['coverage'],
            ['docs'],
            ['clang.debug.unity'],
            ['clang.debug.nounity'],
            ['gcc.debug.unity'],
            ['gcc.debug.nounity'],
            ['clang.release.unity'],
            ['gcc.release.unity'],
            // add a static build just to make sure it works
            ['gcc.debug.unity', "-Dstatic=true"],
            // TODO - san run currently fails
            //['gcc.debug.nounity'  ,  '-Dsan=address'],
        ]

        // create a map of all builds
        // that we want to run. The map
        // is string keys and node{} object values
        def builds = [:]
        for (int index = 0; index < variants.size(); index++) {
            def bldtype = variants[index][0]
            def cmake_extra = variants[index].size() > 1 ? variants[index][1] : ''
            def bldlabel = bldtype + cmake_extra
            bldlabel = bldlabel.replace('-', '_')
            bldlabel = bldlabel.replace(' ', '')
            bldlabel = bldlabel.replace('=', '_')
            builds[bldlabel] = {
                node('rippled-dev') {
                    checkout scm
                    dir ('build') {
                        deleteDir()
                    }
                    def cdir = upDir(pwd())
                    echo "BASEDIR: ${cdir}"
                    def compiler = getCompiler(bldtype)
                    def target = getTarget(bldtype)
                    if (compiler == "coverage" || compiler == "docs") {
                        compiler = 'gcc'
                    }
                    echo "COMPILER: ${compiler}"
                    echo "TARGET: ${target}"
                    def clang_cc  =
                        (compiler == "clang") ? "${LLVM_ROOT}/bin/clang" : ''
                    def clang_cxx =
                        (compiler == "clang") ? "${LLVM_ROOT}/bin/clang++" : ''
                    def ucc = isNoUnity(target) ? 'true' : 'false'
                    echo "USE_CC: ${ucc}"
                    withEnv(["CCACHE_BASEDIR=${cdir}",
                             "CCACHE_NOHASHDIR=true",
                             'LCOV_ROOT=""',
                             "TARGET=${target}",
                             "CC=${compiler}",
                             'BUILD=cmake',
                             "CMAKE_EXTRA_ARGS=${cmake_extra}",
                             'VERBOSE_BUILD=true',
                             "CLANG_CC=${clang_cc}",
                             "CLANG_CXX=${clang_cxx}",
                             "USE_CCACHE=${ucc}"])
                    {
                        myStage(bldlabel)
                        try {
                            sh "rm -fv ${bldlabel}.txt"
                            if (bldtype == "docs") {
                                sh '''#!/bin/bash
set -ex
log_file=''' + "${bldlabel}.txt" + '''
exec 3>&1 1>>${log_file} 2>&1
cd docs
rm -rf html_doc
/usr/bin/time -p doxygen source.dox
echo "0 tests total, 0 failures"
'''
                            }
                            else {
                                sh "ccache -s > ${bldlabel}.txt"
                                // the devtoolset from SCL gives us a recent gcc. It's
                                // not strictly needed when we are building with clang,
                                // but it doesn't seem to interfere either
                                sh "source /opt/rh/devtoolset-6/enable && " +
                                   "(/usr/bin/time -p ./bin/ci/ubuntu/build-and-test.sh 2>&1) 2>&1 " +
                                   ">> ${bldlabel}.txt"
                                sh "ccache -s >> ${bldlabel}.txt"
                            }
                        }
                        finally {
                            def outstr = readFile("${bldlabel}.txt")
                            def st = getResults(outstr)
                            def time = getTime(outstr)
                            def fail_count = getFailures(outstr)
                            outstr = null
                            def txtcolor =
                                fail_count == 0 ? "DarkGreen" : "Crimson"
                            def shortbld = bldlabel
                            shortbld = shortbld.replace('debug', 'dbg')
                            shortbld = shortbld.replace('release', 'rel')
                            shortbld = shortbld.replace('unity', 'un')
                            manager.addShortText(
                                "${shortbld}: ${st}, t: ${time}",
                                txtcolor,
                                "white",
                                "0px",
                                "white")
                            archive("${bldlabel}.txt")
                            if (bldtype == "docs") {
                                publishHTML(
                                    reportName: 'Doxygen',
                                    reportDir: 'docs/html_doc',
                                    reportFiles: 'index.html')
                            }
                            lock('rippled_dev_status') {
                                all_status[bldlabel] =
                                    [fail_count == 0, bldtype + " " + cmake_extra, "${st}, t: ${time}"]
                            }
                        }
                    }
                }
            }
        }

        parallel builds
    }
}
finally {
    // anything here should run always...
    stage ('Final Status') {
        node {
            def start_time = new Date()
            def sdf = new SimpleDateFormat("yyyyMMdd - HH:mm:ss")
            def datestamp = sdf.format(start_time)

            def results = """
## Jenkins Build Summary

Built from [this commit](https://github.com/ripple/rippled/commit/${commit_id})

Built at __${datestamp}__

### Test Results

Build Type | Result | Status
---------- | ------ | ------
"""
            for ( e in all_status) {
                results += e.value[1] + " | " + e.value[2] + " | " +
                    (e.value[0] ? "PASS :white_check_mark: " : "FAIL :red_circle: ") + "\n"
            }
            results += "\n"
            echo "FINAL BUILD RESULTS"
            echo results

            try {
                def url_comment = ""
                if (env.CHANGE_ID && env.CHANGE_ID ==~ /\d+/) {
                    //
                    // CHANGE_ID indicates we are building a PR
                    // find PR comments
                    //
                    def resp = httpRequest(
                        timeout: 10,
                        authentication: github_cred,
                        url: "${github_repo}/pulls/$CHANGE_ID")
                    def result = readJSON(text: resp.content)
                    //
                    // follow issue comments link
                    //
                    url_comment = result['_links']['issue']['href'] + '/comments'
                }
                else {
                    //
                    // if not a PR, just search comments for our commit ID
                    //
                    url_comment =
                        "${github_repo}/commits/${commit_id}/comments"
                }

                def response = httpRequest(
                    timeout: 10,
                    authentication: github_cred,
                    url: url_comment)
                def data = readJSON(text: response.content)
                def comment_id = 0
                def mode = 'POST'
                // see if we can find and existing comment here with
                // a heading that matches ours...
                for (comment in data) {
                    if (comment['body'] =~ /(?m)^##\s+Jenkins Build/) {
                        comment_id = comment['id']
                        echo "existing status comment ${comment_id} found"
                        url_comment = comment['url']
                        mode = 'PATCH'
                        break;
                    }
                }

                if (comment_id == 0) {
                    echo "no existing status comment found"
                }

                def body = JsonOutput.toJson([
                    body: results
                ])

                response = httpRequest(
                    timeout: 10,
                    authentication: github_cred,
                    url: url_comment,
                    contentType: 'APPLICATION_JSON',
                    httpMode: mode,
                    requestBody: body)
            }
            catch (e) {
                echo "had a problem interacting with github...status is probably not updated"
            }
        }
    }
}

// ---------------
// util functions
// ---------------
def myStage(name) {
    echo """
+++++++++++++++++++++++++++++++++++++++++
  >> building ${name}
+++++++++++++++++++++++++++++++++++++++++
"""
}

def printGitInfo(id, log) {
    echo """
+++++++++++++++++++++++++++++++++++++++++
  >> Building commit ID ${id}
  >>
${log}
+++++++++++++++++++++++++++++++++++++++++
"""
}

@NonCPS
def getResults(text) {
    // example:
    /// 194.5s, 154 suites, 948 cases, 360485 tests total, 0 failures
    def matcher = text =~ /(\d+) cases, (\d+) tests total, (\d+) (failure(s?))/
    matcher ? matcher[0][1] + " cases, " + matcher[0][3] + " failed" : "no test results"
}

def getFailures(text) {
    // example:
    /// 194.5s, 154 suites, 948 cases, 360485 tests total, 0 failures
    def matcher = text =~ /(\d+) tests total, (\d+) (failure(s?))/
    // if we didn't match, then return 1 since something is
    // probably wrong, e.g. maybe the build failed...
    matcher ? matcher[0][2] as Integer : 1i
}

@NonCPS
def getCompiler(bld) {
    def matcher = bld =~ /^(.+?)\.(.+)$/
    matcher ? matcher[0][1] : bld
}

@NonCPS
def isNoUnity(bld) {
    def matcher = bld =~ /\.nounity\s*$/
    matcher ? true : false
}

@NonCPS
def getTarget(bld) {
    def matcher = bld =~ /^(.+?)\.(.+)$/
    matcher ? matcher[0][2] : bld
}

// because I can't seem to find path manipulation
// functions in groovy....
@NonCPS
def upDir(path) {
    def matcher = path =~ /^(.+)\/(.+?)/
    matcher ? matcher[0][1] : path
}

@NonCPS
def getTime(text) {
    // look for text following a label 'real' for
    // wallclock time. Some `time`s report fractional
    // seconds and we can omit those in what we report
    def matcher = text =~ /(?m)^real\s+(.+)\.(\d+?)[s]?/
    matcher ? matcher[0][1] + "s" : "n/a"
}


#!/usr/bin/env groovy

import groovy.json.JsonOutput
import java.text.*

all_status = [:]
commit_id = ''
git_fork = 'ripple'
git_repo = 'rippled'
//
// this is not the actual token, but an ID/key into the jenkins
// credential store which httpRequest can access.
//
github_cred = '6bd3f3b9-9a35-493e-8aef-975403c82d3e'
//
// root API url for our repo (default, overriden below)
//
github_api = 'https://api.github.com/repos/ripple/rippled'

try {
    stage ('Startup Checks') {
        // here we check the commit author against collaborators
        // we need a node to do this because readJSON requires
        // a filesystem (although we just pass it text)
        node {
            checkout scm
            commit_id = getCommitID()
            //
            // NOTE this getUserRemoteConfigs call requires a one-time
            // In-process Script Approval (configure jenkins). We need this
            // to detect the remote repo to interact with via the github API.
            //
            def remote_url = scm.getUserRemoteConfigs()[0].getUrl()
            if (remote_url) {
                echo "GIT URL scm: $remote_url"
                git_fork = remote_url.tokenize('/')[2]
                git_repo = remote_url.tokenize('/')[3].split('\\.')[0]
                echo "GIT FORK: $git_fork"
                echo "GIT REPO: $git_repo"
                github_api = "https://api.github.com/repos/${git_fork}/${git_repo}"
                echo "API URL REPO: $github_api"
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
                    url: "${github_api}/collaborators")
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
                        'Author of this change is not a collaborator!',
                        'Crimson',
                        'white',
                        '0px',
                        'white')
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
            ['msvc.debug'],
            // This one does not currently build (TBD):
            //['msvc.debug.nounity'],
            ['msvc.debug', '', 'PROJECT_NAME=rippled_classic'],
            ['msvc.release'],
            ['clang.debug.unity'],
            ['clang.debug.unity', '', 'PARALLEL_TESTS=false'],
            ['clang.debug.nounity'],
            ['gcc.debug.unity'],
            ['gcc.debug.nounity'],
            ['clang.release.unity'],
            ['gcc.release.unity'],
            // add a static build just to make sure it works
            ['gcc.debug.unity', '-Dstatic=true'],
            // TODO - sanitizer runs currently fail
            //['gcc.debug.nounity' , '-Dsan=address', 'PARALLEL_TESTS=false'],
            //['gcc.debug.nounity' , '-Dsan=thread',  'PARALLEL_TESTS=false'],
        ]

        // create a map of all builds
        // that we want to run. The map
        // is string keys and node{} object values
        def builds = [:]
        for (int index = 0; index < variants.size(); index++) {
            def bldtype = variants[index][0]
            def cmake_extra = variants[index].size() > 1 ? variants[index][1] : ''
            def bldlabel = bldtype + cmake_extra
            def extra_env = variants[index].size() > 2 ? variants[index][2..-1] : []
            for (int j = 0; j < extra_env.size(); j++) {
                bldlabel += "_" + extra_env[j]
            }
            bldlabel = bldlabel.replace('-', '_')
            bldlabel = bldlabel.replace(' ', '')
            bldlabel = bldlabel.replace('=', '_')

            def compiler = getFirstPart(bldtype)
            def target = getSecondPart(bldtype)
            def config = getFirstPart(target)
            if (compiler == 'coverage' || compiler == 'docs') {
                compiler = 'gcc'
            }
            def cc =
                (compiler == 'clang') ? '/opt/llvm-5.0.1/bin/clang' : 'gcc'
            def cxx =
                (compiler == 'clang') ? '/opt/llvm-5.0.1/bin/clang++' : 'g++'
            def ucc = isNoUnity(target) ? 'true' : 'false'
            def node_type =
                (compiler == 'msvc') ? 'rippled-win' : 'rippled-dev'
            // the default disposition for parallel test..disabled
            // for coverage, enabled otherwise. Can still be overridden
            // by explicitly setting with extra env settings above.
            def pt = (compiler == 'coverage') ? 'false' : 'true'

            def env_vars = [
                "TARGET=${target}",
                "CONFIG_TYPE=${config}",
                "COMPILER=${compiler}",
                "PARALLEL_TESTS=${pt}",
                'BUILD=cmake',
                "BUILD_DIR=${bldlabel}",
                "CMAKE_EXTRA_ARGS=${cmake_extra}",
                'VERBOSE_BUILD=true']

            builds[bldlabel] = {
                node(node_type) {
                    checkout scm
                    dir ('build') {
                        deleteDir()
                    }
                    def cdir = upDir(pwd())
                    echo "BASEDIR: ${cdir}"
                    echo "COMPILER: ${compiler}"
                    echo "TARGET: ${target}"
                    echo "CONFIG: ${config}"
                    echo "USE_CC: ${ucc}"
                    if (compiler == 'msvc') {
                        env_vars.addAll([
                            'BOOST_ROOT=c:\\lib\\boost_1_66',
                            'PROJECT_NAME=rippled',
                            'MSBUILDDISABLENODEREUSE=1',  // this ENV setting is probably redundant since we also pass /nr:false to msbuild
                            'OPENSSL_ROOT=c:\\OpenSSL-Win64'])
                    }
                    else {
                        env_vars.addAll([
                            'NINJA_BUILD=false',
                            "CCACHE_BASEDIR=${cdir}",
                            'PLANTUML_JAR=/opt/plantuml/plantuml.jar',
                            'CCACHE_NOHASHDIR=true',
                            "CC=${cc}",
                            "CXX=${cxx}",
                            'LCOV_ROOT=""',
                            'PATH+CMAKE_BIN=/opt/local/cmake',
                            'GDB_ROOT=/opt/local/gdb',
                            'BOOST_ROOT=/opt/local/boost_1_66_0',
                            "USE_CCACHE=${ucc}"])
                    }

                    if (extra_env.size() > 0) {
                        env_vars.addAll(extra_env)
                    }

                    withCredentials(
                        [string(
                            credentialsId: 'RIPPLED_CODECOV_TOKEN',
                            variable: 'CODECOV_TOKEN')])
                    {
                        withEnv(env_vars) {
                            myStage(bldlabel)
                            try {
                                if (compiler == 'msvc') {
                                    powershell "Remove-Item -Path \"${bldlabel}.txt\" -Force -ErrorAction Ignore"
                                    // we capture stdout to variable because I could
                                    // not figure out how to make powershell redirect internally
                                    output = powershell (
                                            returnStdout: true,
                                            script: windowsBuildCmd())
                                    // if the powershell command fails (has nonzero exit)
                                    // then the command above throws, we don't get our output,
                                    // and we never create this output file.
                                    //  SEE https://issues.jenkins-ci.org/browse/JENKINS-44930
                                    // Alternatively, figure out how to reliably redirect
                                    // all output above to a file (Start/Stop transcript does not work)
                                    writeFile(
                                        file: "${bldlabel}.txt",
                                        text: output)
                                }
                                else {
                                    sh "rm -fv ${bldlabel}.txt"
                                    // execute the bld command in a redirecting shell
                                    // to capture output
                                    sh redhatBuildCmd(bldlabel)
                                }
                            }
                            finally {
                                if (bldtype == 'docs') {
                                    publishHTML(
                                        allowMissing: true,
                                        alwaysLinkToLastBuild: false,
                                        keepAll: true,
                                        reportName:  'Doxygen',
                                        reportDir:   'build/docs/html_doc',
                                        reportFiles: 'index.html')
                                }
                                def envs = ''
                                for (int j = 0; j < extra_env.size(); j++) {
                                    envs += ", <br/>" + extra_env[j]
                                }
                                def cmake_txt = cmake_extra
                                if (cmake_txt != '') {
                                    cmake_txt = " <br/>" + cmake_txt
                                }
                                def st = reportStatus(bldlabel, bldtype + cmake_txt + envs, env.BUILD_URL)
                                lock('rippled_dev_status') {
                                    all_status[bldlabel] = st
                                }
                            } //try-catch-finally
                        } //withEnv
                    } //withCredentials
                } //node
            } //builds item
        } //for variants

        // Also add a single build job for doing the RPM build
        // on a docker node
        builds['rpm'] = {
            node('docker') {
                def bldlabel = 'rpm'
                configFileProvider (
                    [configFile(
                        fileId: 'rippled-commit-signer-public-keys.txt',
                        variable: 'SIGNER_PUBLIC_KEYS')])
                {
                    def remote =
                        (git_fork == 'ripple') ? 'origin' : git_fork

                    withCredentials(
                        [string(
                            credentialsId: 'RIPPLED_RPM_ROLE_ID',
                            variable: 'ROLE_ID')])
                    {
                        withEnv([
                            'docker_image=artifactory.ops.ripple.com:6555/rippled-rpm-builder:latest',
                            "git_commit=${commit_id}",
                            "git_remote=${remote}",
                            "rpm_release=${env.BUILD_ID}"])
                        {
                            try {
                                sh "rm -fv ${bldlabel}.txt"
                                sh "if [ -d rpm-out ]; then rm -rf rpm-out; fi"
                                sh rpmBuildCmd(bldlabel)
                            }
                            finally {
                                def st = reportStatus(bldlabel, bldlabel, env.BUILD_URL)
                                lock('rippled_dev_status') {
                                    all_status[bldlabel] = st
                                }
                                archiveArtifacts(
                                    artifacts: 'rpm-out/*.rpm',
                                    allowEmptyArchive: true)
                            }
                        } //withEnv
                    } //withCredentials
                } //configFile
            } //node
        }

        // this actually executes all the builds we just defined
        // above, in parallel as slaves are available
        parallel builds
    }
}
finally {
    // anything here should run always...
    stage ('Final Status') {
        node {
            def results = makeResultText()
            try {
                def res = getCommentID() //get array return b/c jenkins does not allow multiple direct return/assign
                def comment_id = res[0]
                def url_comment = res[1]
                def mode = 'PATCH'
                if (comment_id == 0) {
                    echo 'no existing status comment found'
                    mode =  'POST'
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
                echo 'had a problem interacting with github...status is probably not updated'
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

def makeResultText () {
    def start_time = new Date()
    def sdf = new SimpleDateFormat('yyyyMMdd - HH:mm:ss')
    def datestamp = sdf.format(start_time)

    def results = """
## Jenkins Build Summary

Built from [this commit](https://github.com/${git_fork}/${git_repo}/commit/${commit_id})

Built at __${datestamp}__

### Test Results

Build Type | Log | Result | Status
---------- | --- | ------ | ------
"""
    for ( e in all_status) {
        results += e.value[1] + ' | ' + e.value[3] + ' | ' + e.value[2] + ' | ' +
            (e.value[0] ? 'PASS :white_check_mark: ' : 'FAIL :red_circle: ') + '\n'
    }
    results += '\n'
    echo 'FINAL BUILD RESULTS'
    echo results
    results
}

def getCommentID () {
    def url_c = ''
    if (env.CHANGE_ID && env.CHANGE_ID ==~ /\d+/) {
        //
        // CHANGE_ID indicates we are building a PR
        // find PR comments
        //
        def resp = httpRequest(
            timeout: 10,
            authentication: github_cred,
            url: "${github_api}/pulls/$CHANGE_ID")
        def result = readJSON(text: resp.content)
        //
        // follow issue comments link
        //
        url_c = result['_links']['issue']['href'] + '/comments'
    }
    else {
        //
        // if not a PR, just search comments for our commit ID
        //
        url_c =
            "${github_api}/commits/${commit_id}/comments"
    }
    def response = httpRequest(
        timeout: 10,
        authentication: github_cred,
        url: url_c)
    def data = readJSON(text: response.content)
    def comment_id = 0
    // see if we can find and existing comment here with
    // a heading that matches ours...
    for (comment in data) {
        if (comment['body'] =~ /(?m)^##\s+Jenkins Build/) {
            comment_id = comment['id']
            echo "existing status comment ${comment_id} found"
            url_c = comment['url']
            break;
        }
    }
    [comment_id, url_c]
}

def getCommitID () {
    def cid = sh (
        script: 'git rev-parse HEAD',
        returnStdout: true)
    cid = cid.trim()
    echo "commit ID is ${cid}"
    commit_log = sh (
         script: "git show --name-status ${cid}",
         returnStdout: true)
    printGitInfo (cid, commit_log)
    cid
}

@NonCPS
def getResults(text, label) {
    // example:
    ///   194.5s, 154 suites, 948 cases, 360485 tests total, 0 failures
    // or build log format:
    //    [msvc.release] 71.3s, 162 suites, 995 cases, 318901 tests total, 1 failure
    def matcher =
        text == '' ?
        manager.getLogMatcher(/\[${label}\].+?(\d+) case[s]?, (\d+) test[s]? total, (\d+) (failure(s?))/) :
        text =~ /(\d+) case[s]?, (\d+) test[s]? total, (\d+) (failure(s?))/
    matcher ? matcher[0][1] + ' cases, ' + matcher[0][3] + ' failed' : 'no test results'
}

@NonCPS
def getFailures(text, label) {
    // [see above for format]
    def matcher =
        text == '' ?
        manager.getLogMatcher(/\[${label}\].+?(\d+) test[s]? total, (\d+) (failure(s?))/) :
        text =~ /(\d+) test[s]? total, (\d+) (failure(s?))/
    // if we didn't match, then return 1 since something is
    // probably wrong, e.g. maybe the build failed...
    matcher ? matcher[0][2] as Integer : 1i
}

@NonCPS
def getTime(text, label) {
    // look for text following a label 'real' for
    // wallclock time. Some `time`s report fractional
    // seconds and we can omit those in what we report
    def matcher =
        text == '' ?
        manager.getLogMatcher(/(?m)^\[${label}\]\s+real\s+(.+)\.(\d+?)[s]?/) :
        text =~ /(?m)^real\s+(.+)\.(\d+?)[s]?/
    if (matcher) {
        return matcher[0][1] + 's'
    }

    // alternatively, look for powershell elapsed time
    // format, e.g. :
    //    TotalSeconds      : 523.2140529
    def matcher2 =
        text == '' ?
        manager.getLogMatcher(/(?m)^\[${label}\]\s+TotalSeconds\s+:\s+(\d+)\.(\d+?)?/) :
        text =~ /(?m)^TotalSeconds\s+:\s+(\d+)\.(\d+?)?/
    matcher2 ? matcher2[0][1] + 's' : 'n/a'
}

@NonCPS
def getFirstPart(bld) {
    def matcher = bld =~ /^(.+?)\.(.+)$/
    matcher ? matcher[0][1] : bld
}

@NonCPS
def isNoUnity(bld) {
    def matcher = bld =~ /\.nounity\s*$/
    matcher ? true : false
}

@NonCPS
def getSecondPart(bld) {
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

// the shell command used for building on redhat
def redhatBuildCmd(bldlabel) {
'''\
#!/bin/bash
set -ex
log_file=''' + "${bldlabel}.txt" + '''
exec 3>&1 1>>${log_file} 2>&1
ccache -s
source /opt/rh/devtoolset-6/enable
/usr/bin/time -p ./bin/ci/ubuntu/build-and-test.sh 2>&1
ccache -s
'''
}

// the powershell command used for building an RPM
def windowsBuildCmd() {
'''
# Enable streams 3-6
$WarningPreference = 'Continue'
$VerbosePreference = 'Continue'
$DebugPreference = 'Continue'
$InformationPreference = 'Continue'

Invoke-BatchFile "${env:ProgramFiles(x86)}\\Microsoft Visual Studio\\2017\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat" x86_amd64
Get-ChildItem env:* | Sort-Object name
cl
cmake --version
New-Item -ItemType Directory -Force -Path "build/$env:BUILD_DIR" -ErrorAction Stop
$sw = [Diagnostics.Stopwatch]::StartNew()
try {
    Push-Location "build/$env:BUILD_DIR"
    if ($env:NINJA_BUILD -eq "true") {
        cmake -G"Ninja" -Dtarget="$env:COMPILER.$env:TARGET" -DCMAKE_VERBOSE_MAKEFILE=ON ../..
    }
    else {
        cmake -G"Visual Studio 15 2017 Win64" -Dtarget="$env:COMPILER.$env:TARGET" -DCMAKE_VERBOSE_MAKEFILE=ON ../..
    }
    if ($LastExitCode -ne 0) { throw "CMake failed" }

    ## as of 01/2018, DO NOT USE cmake to run the actual build step. for some
    ## reason, cmake spawning the build under jenkins causes MSBUILD/ninja to
    ## get stuck at the end of the build. Perhaps cmake is spawning
    ## incorrectly or failing to pass certain params

    if ($env:NINJA_BUILD -eq "true") {
        ninja -j $env:NUMBER_OF_PROCESSORS -v
    }
    else {
        msbuild /fl /m /nr:false /p:Configuration="$env:CONFIG_TYPE" /p:Platform=x64 /p:GenerateFullPaths=True /v:normal /nologo /clp:"ShowCommandLine;DisableConsoleColor" "$env:PROJECT_NAME.vcxproj"
    }
    if ($LastExitCode -ne 0) { throw "CMake build failed" }

    $exe = "./$env:CONFIG_TYPE/$env:PROJECT_NAME"
    if ($env:NINJA_BUILD -eq "true") {
        $exe = "./$env:PROJECT_NAME"
    }
    "Exe is at $exe"
    $params = '--unittest', '--quiet', '--unittest-log'
    if ($env:PARALLEL_TESTS -eq "true") {
        $params = $params += "--unittest-jobs=$env:NUMBER_OF_PROCESSORS"
    }
    & $exe $params
    if ($LastExitCode -ne 0) { throw "Unit tests failed" }
}
catch {
    throw
}
finally {
    $sw.Stop()
    $sw.Elapsed
    Pop-Location
}
'''
}

// the shell command used for building an RPM
def rpmBuildCmd(bldlabel) {
'''\
#!/bin/bash
set -ex
log_file=''' + "${bldlabel}.txt" + '''
exec 3>&1 1>>${log_file} 2>&1

# Vault Steps
SECRET_ID=$(cat /.vault/rippled-build-role/secret-id)
export VAULT_TOKEN=$(/usr/local/ripple/ops-toolbox/vault/vault_approle_auth -r ${ROLE_ID} -s ${SECRET_ID} -t)
/usr/local/ripple/ops-toolbox/vault/vault_get_sts_token.py -r rippled-build-role

head $SIGNER_PUBLIC_KEYS

mkdir -p rpm-out

docker pull "${docker_image}"

echo "Running build container"

docker run --rm \
-v $PWD/rpm-out:/opt/rippled-rpm/out \
-v $SIGNER_PUBLIC_KEYS:/opt/rippled-rpm/public-keys.txt \
-e "GIT_COMMIT=$git_commit" \
-e "GIT_REMOTE=$git_remote" \
-e "RPM_RELEASE=$rpm_release" \
"${docker_image}"

. rpm-out/build_vars

cd rpm-out
tar xvf rippled-*.tar.gz
ls -la *.rpm
#################################
## for now we don't want the src
## and debugsource rpms for testing
## or archiving...
#################################
rm rippled-debugsource*.rpm
rm *.src.rpm
mkdir rpm-main
cp *.rpm rpm-main
cd rpm-main
cd ../..

cat > test_rpm.sh << "EOL"
#!/bin/bash

function error {
  echo $1
  exit 1
}

yum install -y yum-utils
rpm -i /opt/rippled-rpm/*.rpm
rc=$?; if [[ $rc != 0 ]]; then
  error "error installing rpms"
fi

/opt/ripple/bin/rippled --unittest
rc=$?; if [[ $rc != 0 ]]; then
  error "rippled --unittest failed"
fi

/opt/ripple/bin/validator-keys --unittest
rc=$?; if [[ $rc != 0 ]]; then
  error "validator-keys --unittest failed"
fi

EOL

chmod +x test_rpm.sh

echo "Running test container"

docker run --rm \
-v $PWD/rpm-out/rpm-main:/opt/rippled-rpm \
-v $PWD:/opt/rippled --entrypoint /opt/rippled/test_rpm.sh \
centos:latest
'''
}

// post processing step after each build:
//   * archives the log file
//   * adds short description/status to build status
//   * returns an array of result info to add to the all_build summary
def reportStatus(label, type, bldurl) {
    def outstr = ''
    def loglink = "[console](${bldurl}/console)"
    def logfile = "${label}.txt"
    if ( fileExists(logfile) ) {
        archiveArtifacts( artifacts: logfile )
        outstr = readFile(logfile)
        loglink = "[logfile](${bldurl}/artifact/${logfile})"
    }
    def st = getResults(outstr, label)
    def time = getTime(outstr, label)
    def fail_count = getFailures(outstr, label)
    outstr = null
    def txtcolor =
        fail_count == 0 ? 'DarkGreen' : 'Crimson'
    def shortbld = label
    shortbld = shortbld.replace('debug', 'dbg')
    shortbld = shortbld.replace('release', 'rel')
    shortbld = shortbld.replace('unity', 'un')
    manager.addShortText(
        "${shortbld}: ${st}, t: ${time}",
        txtcolor,
        'white',
        '0px',
        'white')
    [fail_count == 0, type, "${st}, t: ${time}", loglink]
}


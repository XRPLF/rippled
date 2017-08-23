#!/usr/bin/env groovy

stage ('Parallel Build') {

    def variants = [
        'clang.debug.unity',
        'clang.debug.nounity',
        'gcc.debug.unity',
        'gcc.debug.nounity',
        'clang.release.unity',
        'gcc.release.unity',
        'coverage'] as String[]

    // create a map of all builds
    // that we want to run. The map
    // is string keys and node{} object values
    def builds = [:]
    for (int index = 0; index < variants.size(); index++) {
        def bldtype = variants[index]
        builds[bldtype] = {
            node('rippled-dev') {
                checkout scm
                dir ('build') {
                    deleteDir()
                }
                def cdir = upDir(pwd())
                echo "BASEDIR: ${cdir}"
                def compiler = getCompiler(bldtype)
                def target = getTarget(bldtype)
                if (compiler == "coverage") {
                    compiler = 'gcc'
                }
                echo "COMPILER: ${compiler}"
                echo "TARGET: ${target}"
                def clang_cc  = (compiler == "clang") ? '/opt/llvm-4.0.0/bin/clang' : ''
                def clang_cxx = (compiler == "clang") ? '/opt/llvm-4.0.0/bin/clang++' : ''
                def ucc = isNoUnity(target) ? 'true' : 'false'
                echo "USE_CC: ${ucc}"
                withEnv(["CCACHE_BASEDIR=${cdir}",
                         'LCOV_ROOT=""',
                         "TARGET=${target}",
                         "CC=${compiler}",
                         'BUILD=cmake',
                         'VERBOSE_BUILD=true',
                         "CLANG_CC=${clang_cc}",
                         "CLANG_CXX=${clang_cxx}",
                         "CCACHE_LOGFILE=${bldtype}.ccache.txt",
                         "USE_CCACHE=${ucc}"])
                {
                    myStage(bldtype)
                    try {
                        if (fileExists("${bldtype}.ccache.txt")) {
                            sh "rm -f ${bldtype}.ccache.txt"
                        }
                        sh "ccache -s > ${bldtype}.txt"
                        // the devtoolset from SCL gives us a recent gcc. It's not strictly needed
                        // when we are building with clang, but it doesn't seem to interfere either
                        sh "source /opt/rh/devtoolset-6/enable && (/usr/bin/time -p ./bin/ci/ubuntu/build-and-test.sh 2>&1) 2>&1 >> ${bldtype}.txt"
                        sh "ccache -s >> ${bldtype}.txt"
                    }
                    catch (any) {
                        throw any
                    }
                    finally {
                        def outstr = readFile("${bldtype}.txt")
                        def st = getResults(outstr)
                        def time = getTime(outstr)
                        def txtcolor = getFailures(outstr) == 0 ? "DarkGreen" : "Crimson"
                        outstr = null
                        def shortbld = bldtype
                        shortbld = shortbld.replace('debug', 'dbg')
                        shortbld = shortbld.replace('release', 'rel')
                        manager.addShortText("${shortbld}: ${st}, t: ${time}", txtcolor, "white", "0px", "white")
                        archive "${bldtype}.txt"
                        if (ucc == "true" && fileExists("${bldtype}.ccache.txt")) {
                            archive "${bldtype}.ccache.txt"
                        }
                    }
                }
            }
        }
    }

    parallel builds
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

@NonCPS
def getResults(text) {
    // example:
    /// 194.5s, 154 suites, 948 cases, 360485 tests total, 0 failures
    def matcher = text =~ /(\d+) tests total, (\d+) (failure(s?))/
    matcher ? matcher[0][1] + " tests, " + matcher[0][2] + " " + matcher[0][3] : "no test results"
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


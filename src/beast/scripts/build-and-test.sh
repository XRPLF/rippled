#!/usr/bin/env bash
# We use set -e to bail on first non zero exit code of any processes launched
# and -x to exit upon any unbound variable. -x will output command lines used
# (with variable expansion)
set -eux

# brew install bash (4) to get this working on OSX!
shopt -s globstar

################################## ENVIRONMENT #################################

# If not CI, then set some defaults
if [[ "${CI:-}" == "" ]]; then
  TRAVIS_BRANCH=${TRAVIS_BRANCH:-feature}
  CC=${CC:-gcc}
  ADDRESS_MODEL=${ADDRESS_MODEL:-64}
  VARIANT=${VARIANT:-debug}
  # If running locally we assume we have lcov/valgrind on PATH
else
  export PATH=$VALGRIND_ROOT/bin:$LCOV_ROOT/usr/bin:$PATH
fi

MAIN_BRANCH="0"
# For builds not triggered by a pull request TRAVIS_BRANCH is the name of the
# branch currently being built; whereas for builds triggered by a pull request
# it is the name of the branch targeted by the pull request (in many cases this
# will be master).
if [[ $TRAVIS_BRANCH == "master" || $TRAVIS_BRANCH == "develop" ]]; then
    MAIN_BRANCH="1"
fi

num_jobs="1"
if [[ $(uname) == "Darwin" ]]; then
  num_jobs=$(sysctl -n hw.physicalcpu)
elif [[ $(uname -s) == "Linux" ]]; then
  # CircleCI returns 32 phys procs, but 2 virt proc
  num_proc_units=$(nproc)
  # Physical cores
  num_jobs=$(lscpu -p | grep -v '^#' | sort -u -t, -k 2,4 | wc -l)
  if ((${num_proc_units} < ${num_jobs})); then
      num_jobs=$num_proc_units
  fi
fi

echo "using toolset: $CC"
echo "using variant: $VARIANT"
echo "using address-model: $ADDRESS_MODEL"
echo "using PATH: $PATH"
echo "using MAIN_BRANCH: $MAIN_BRANCH"
echo "using BOOST_ROOT: $BOOST_ROOT"

#################################### HELPERS ###################################

function run_tests_with_debugger {
  for x in bin/**/$VARIANT/**/*-tests; do
    scripts/run-with-debugger.sh "$x"
  done
}

function run_tests {
  for x in bin/**/$VARIANT/**/*-tests; do
    $x
  done
}

function run_tests_with_valgrind {
  for x in bin/**/$VARIANT/**/*-tests; do
    if [[ $(basename $x) == "bench-tests" ]]; then
      $x
    else
      # TODO --max-stackframe=8388608
      # see: https://travis-ci.org/vinniefalco/Beast/jobs/132486245
      valgrind --suppressions=./scripts/valgrind.supp --error-exitcode=1 "$x"
    fi
  done
}

function build_beast {
  $BOOST_ROOT/bjam toolset=$CC \
               variant=$VARIANT \
               address-model=$ADDRESS_MODEL \
               -j${num_jobs}
}

function build_beast_cmake {
    mkdir -p build
    pushd build > /dev/null
    cmake -DVARIANT=${VARIANT} ..
    make -j${num_jobs}
    mkdir -p ../bin/$VARIANT
    find . -executable -type f -exec cp {} ../bin/$VARIANT/. \;
    popd > /dev/null
}

function run_autobahn_test_suite {
  # Run autobahn tests
  wsecho=$(find bin -name "websocket-echo" | grep /$VARIANT/)
  nohup $wsecho&

  # We need to wait a while so wstest can connect!
  sleep 5
  cd scripts && wstest -m fuzzingclient
  cd ..
  # Show the output
  cat nohup.out
  rm nohup.out
  # Show what jobs are running
  jobs
  # Wait a while for things to wind down before issuing a kill
  sleep 5
  # Kill it gracefully
  kill -INT %1
  # Wait for all the jobs to finish
  wait
  # Parse the test results, with python>=2.5<3 script
  python scripts/parseautobahn.py scripts/autoresults/index.json
}

##################################### BUILD ####################################

if [[ ${BUILD_SYSTEM:-} == cmake ]]; then
    build_beast_cmake
else
    build_beast
fi

##################################### TESTS ####################################

if [[ $VARIANT == "coverage" ]]; then
  find . -name "*.gcda" | xargs rm -f
  rm *.info -f
  # Create baseline coverage data file
  lcov --no-external -c -i -d . -o baseline.info > /dev/null

  # Perform test
  if [[ $MAIN_BRANCH == "1" ]]; then
    run_tests_with_valgrind
    run_autobahn_test_suite
  else
    echo "skipping autobahn tests for feature branch build"
    run_tests
  fi

  # Create test coverage data file
  lcov --no-external -c -d . -o testrun.info > /dev/null

  # Combine baseline and test coverage data
  lcov -a baseline.info -a testrun.info -o lcov-all.info > /dev/null

  # Extract only include/beast, and don\'t report on examples/test
  lcov -e "lcov-all.info" "$PWD/include/beast/*" -o lcov.info > /dev/null

  ~/.local/bin/codecov -X gcov
  cat lcov.info | node_modules/.bin/coveralls

  # Clean up these stragglers so BOOST_ROOT cache is clean
  find $BOOST_ROOT/bin.v2 -name "*.gcda" | xargs rm -f
else
  run_tests_with_debugger
fi

#!/bin/bash -u
# We use set -e and bash with -u to bail on first non zero exit code of any
# processes launched or upon any unbound variable
shopt -s globstar
set -ex

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

echo "using toolset: $CC"
echo "using variant: $VARIANT"
echo "using address-model: $ADDRESS_MODEL"
echo "using PATH: $PATH"

#################################### HELPERS ###################################

function run_tests_with_gdb {
  for x in bin/**/*-tests; do scripts/run-with-gdb.sh "$x"; done
}

function build_beast {
  $BOOST_ROOT/bjam toolset=$CC \
               variant=$VARIANT \
               address-model=$ADDRESS_MODEL
}

##################################### BUILD ####################################

build_beast

##################################### TESTS ####################################

if [[ $VARIANT == "coverage" ]]; then
  find . -name "*.gcda" | xargs rm -f
  rm *.info -f
  # Create baseline coverage data file
  lcov --no-external -c -i -d . -o baseline.info > /dev/null

  # Perform test
  run_tests_with_gdb

  # Run autobahn tests
  export SERVER=`find . -name "websocket-echo"`
  nohup scripts/run-with-gdb.sh $SERVER&

  # We need to wait a while so wstest can connect!
  sleep 5
  cd scripts && wstest -m fuzzingclient
  cd ..
  # Show the output
  cat nohup.out
  rm nohup.out
  jobs
  # Kill it gracefully
  kill -INT %1
  sleep 1
  kill -INT %1 || echo "Dead already"

  # Create test coverage data file
  lcov --no-external -c -d . -o testrun.info > /dev/null

  # Combine baseline and test coverage data
  lcov -a baseline.info -a testrun.info -o lcov-all.info > /dev/null

  # Extract only include/beast, and don\'t report on examples/test
  lcov -e "lcov-all.info" "*/include/beast/*" -o lcov.info > /dev/null

  ~/.local/bin/codecov -X gcov
else
  # TODO: make a function
  run_tests_with_gdb

  if [[ $VARIANT == "debug" ]]; then
    for x in bin/**/*-tests; do
      # if [[ $x != "bench-tests" ]]; then
      valgrind --error-exitcode=1 "$x"
        ## declare -i RESULT=$RESULT + $?
      # fi
    done
    echo
  fi
fi

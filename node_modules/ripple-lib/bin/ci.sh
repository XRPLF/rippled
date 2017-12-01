#!/bin/bash -ex

NODE_INDEX="$1"
TOTAL_NODES="$2"

typecheck() {
  npm install -g flow-bin
  flow --version
  npm run typecheck
}

lint() {
  echo "eslint $(node_modules/.bin/eslint --version)"
  npm list babel-eslint | grep babel-eslint
  REPO_URL="https://raw.githubusercontent.com/ripple/javascript-style-guide"
  curl "$REPO_URL/es6/eslintrc" > ./eslintrc
  echo "parser: babel-eslint" >> ./eslintrc
  node_modules/.bin/eslint -c ./eslintrc $(git --no-pager diff --name-only -M100% --diff-filter=AM --relative $(git merge-base FETCH_HEAD origin/HEAD) FETCH_HEAD | grep "\.js$")
}

unittest() {
  # test "src"
  npm test --coverage
  npm run coveralls

  # test compiled version in "dist/npm"
  ln -nfs ../../dist/npm/core test/node_modules/ripple-lib
  ln -nfs ../../dist/npm test/node_modules/ripple-api
  npm test
}

oneNode() {
  lint
  typecheck
  unittest
}

twoNodes() {
  case "$NODE_INDEX" in
    0) lint && unittest;;
    1) typecheck;;
    *) echo "ERROR: invalid usage"; exit 2;;
  esac
}

threeNodes() {
  case "$NODE_INDEX" in
    0) lint;;
    1) typecheck;;
    2) unittest;;
    *) echo "ERROR: invalid usage"; exit 2;;
  esac
}

case "$TOTAL_NODES" in
  "") oneNode;;
  1) oneNode;;
  2) twoNodes;;
  3) threeNodes;;
  *) echo "ERROR: invalid usage"; exit 2;;
esac

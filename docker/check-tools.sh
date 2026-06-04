#!/bin/bash
# Verify that every tool expected in the Nix CI env is present and runnable.
set -euo pipefail

ccache --version
clang --version
clang++ --version
clang-format --version
cmake --version
conan --version
curl --version
doxygen --version
dpkg-buildpackage --version
g++ --version
gcc --version
gcov --version
gcovr --version
git --version
gpg --version
less --version
make --version
mold --version
netstat --version
ninja --version
perl --version
pkg-config --version
pre-commit --version
python3 --version
rpmbuild --version
run-clang-tidy --help
vim --version

# A simple test to verify that git can clone a repository over HTTPS
# (i.e. the CA bundle is wired up). Clone to a temp dir and clean up.
tmp_clone="$(mktemp -d)"
git clone --depth 1 https://github.com/XRPLF/actions.git "${tmp_clone}/actions"
rm -rf "${tmp_clone}"

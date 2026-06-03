#!/bin/bash
# Verify that every tool expected in the Nix CI env is present and runnable.
set -euo pipefail

ccache --version
clang --version
clang++ --version
clang-format --version
cmake --version
conan --version
g++ --version
gcc --version
gcovr --version
git --version
less --version
make --version
mold --version
ninja --version
perl --version
pkg-config --version
pre-commit --version
python3 --version
run-clang-tidy --help
vim --version

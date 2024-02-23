#!/usr/bin/env bash

set -o errexit
set -o nounset
set -o xtrace

# Parameters

gcc_version=${GCC_VERSION:-11}
cmake_version=${CMAKE_VERSION:-3.25.1}
cmake_sha256=1c511d09516af493694ed9baf13c55947a36389674d657a2d5e0ccedc6b291d8
conan_version=${CONAN_VERSION:-1.60}

curl https://pyenv.run | bash
export PYENV_ROOT="$HOME/.pyenv"
command -v pyenv >/dev/null || export PATH="$PYENV_ROOT/bin:$PATH"
eval "$(pyenv init -)"

pyenv install 3.11.2
pyenv global 3.11.2

# Download and unpack CMake.
cmake_slug="cmake-${cmake_version}"
cmake_archive="${cmake_slug}.tar.gz"
curl --location --remote-name \
  "https://github.com/Kitware/CMake/releases/download/v${cmake_version}/${cmake_archive}"
echo "${cmake_sha256}  ${cmake_archive}" | sha256sum --check
tar -xzf ${cmake_archive}
rm ${cmake_archive}

# Build and install CMake.
cd ${cmake_slug}
./bootstrap --parallel=$(nproc)
make --jobs $(nproc)
make install
cd ..
rm --recursive --force ${cmake_slug}

# Install Conan.
pip install --upgrade pip
pip install conan==${conan_version}

conan profile new --detect gcc
conan profile update settings.compiler=gcc gcc
conan profile update settings.compiler.version=${gcc_version} gcc
conan profile update settings.compiler.libcxx=libstdc++11 gcc
conan profile update settings.compiler.cppstd=20 gcc
conan profile update env.CC=/usr/bin/gcc gcc
conan profile update env.CXX=/usr/bin/g++ gcc

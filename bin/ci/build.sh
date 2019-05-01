#!/usr/bin/env bash

set -o xtrace
set -o errexit

# The build system. Either 'Unix Makefiles' or 'Ninja'.
GENERATOR=${GENERATOR:-Unix Makefiles}
# The compiler. Either 'gcc' or 'clang'.
COMPILER=${COMPILER:-gcc}
# The build type. Either 'Debug' or 'Release'.
BUILD_TYPE=${BUILD_TYPE:-Debug}
# Additional arguments to CMake.
# We use the `-` substitution here instead of `:-` so that callers can erase
# the default by setting `$CMAKE_ARGS` to the empty string.
CMAKE_ARGS=${CMAKE_ARGS-'-Dwerr=ON'}

if [[ ${COMPILER} == 'gcc' ]]; then
  export CC='gcc'
  export CXX='g++'
elif [[ ${COMPILER} == 'clang' ]]; then
  export CC='clang'
  export CXX='clang++'
fi

mkdir build
cd build
cmake -G "${GENERATOR}" -DCMAKE_BUILD_TYPE=${BUILD_TYPE} ${CMAKE_ARGS} ..
cmake --build . -- -j $(nproc)

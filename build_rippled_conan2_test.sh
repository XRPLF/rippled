#!/bin/bash

# run from outside rippled repo
set -ex

CONAN_HOME=${CONAN_HOME:-~/.conan2/}
NPROC=$(nproc)
conan &> /dev/null

{
  echo "core.upload:parallel = $NPROC"
  echo "core.download:parallel = $NPROC"
  echo "tools.build:jobs = $((NPROC - 2))"
} >> "${CONAN_HOME}/global.conf"

#git clone --depth 1 -b conan2 https://github.com/legleux/rippled

# Upload All Packages to New `dev` Conan Repository
conan_remote="ripple-dev"

## Add Ripple's remote to Conan
# conan remote add "${conan_remote}" http://18.143.149.228:8081/artifactory/api/conan/dev
# conan remote remove conancenter
## or
conan remote add --index 0 "${conan_remote}" http://18.143.149.228:8081/artifactory/api/conan/dev
conan remote disable conancenter

conan remote list

### If uploading ...
## Login to be allowed to upload
#conan remote login -p <passwd>  "${conan_remote}" mlegleux

## Dry run to make sure everything looks good. Also will compress and not re-compress when real upload happens
#conan upload --remote "${conan_remote}" --confirm --recipe-only "*" --dry-run

## If everything looks good, upload.
#conan upload --remote "${conan_remote}" --confirm "*"

## else:
#docker run --rm -it --name conan2_w_ripple_soci_test ghcr.io/legleux/rippled-build-ubuntu:aaf5e3e_conan2 bash
mkdir -p "${CONAN_HOME}/profiles"

## if in repo
#cp rippled/conan_profile/default ~/.conan2/profiles
## else
## write it to file
cp rippled/conan_profile/default "${CONAN_HOME}/profiles/default"
# cat << EOF > "${CONAN_HOME}/profiles/default"
# [settings]
#     {% set os_name = {"Darwin": "Macos"}.get(platform.system(), platform.system()) %}
#     os = {{ os_name }}

#     {% if platform.system() == "Darwin" and platform.machine() == "arm64" %}
#         {% set arch = "armv8" %}
#         {% set compiler = "clang" %}
#         {% set compiler = "" %}
#         {% set compiler, version, compiler_exe = detect_api.detect_default_compiler() %}
#         compiler = {{ compiler }}
#         compiler.version = {{ version }}

#         {% set compiler_version = "15" %}  {# or whatever works #}
#         {% set libcxx = "libc++" %}
#     {% elif os_name == "Linux" %}
#         {% set compiler = os.getenv("COMPILER", "gcc") %}
#         {% set compiler_version = os.getenv("COMPILER_VERSION", "11") %}

#         {% set arch = platform.machine() %}
#         {% set libcxx = "libstdc++11" %}
#     {% endif %}

#     arch = {{ arch }}
#     compiler = {{ compiler }}
#     compiler.version = {{ compiler_version }}
#     compiler.libcxx = {{ libcxx }}

#     {% set build_type = os.getenv("BUILD_TYPE", "release").title() %}
#     build_type = {{ build_type }}

#     # Maybe require cppstd=20 in conanfile ?
#     compiler.cppstd=20

# [tool_requires]
#     !cmake/*: cmake/[>=3 <4]

# [buildenv]
#     {% if compiler == "gcc" %}
#         {% set cc = "gcc" %}
#         {% set cpp = "g++" %}
#     {% elif compiler.endswith("clang") %}
#         {% set cc = "clang" %}
#         {% set cpp = cc + "++" %}
#     {% endif %}

#     {% if os_name != "Macos" %}
#         # Assuming your clang is versioned
#         {% set cc = cc ~ "-" ~ compiler_version %}
#         {% set cpp = cpp ~ "-" ~ compiler_version %}
#     {% endif %}

#     CC=/usr/bin/{{ cc }}
#     CXX=/usr/bin/{{ cpp }}
# [conf]
#     tools.build:compiler_executables={'c': '{{ cc }}', 'cpp': '{{ cpp }}' }
# EOF

conan profile show

# cd rippled && mkdir build && cd build
# conan build ..

## try after cloning, just:
# XRPLD=true conan build rippled
## Build rippled with tests
conan build rippled -of build -o xrpld=True -o tests=True --build missing
# conan build rippled -o tests=True # _Should_ imply -o xrpld=True (unless we have libxrpl specific tests?)

# Run the unit tests
rippled=$(find . -type f -name rippled)
"${rippled}" --version
"${rippled}" --unittest  --unittest-jobs $NPROC

# rippled version 2.4.0
# Git commit hash: 4dc8b8a0be853da94577c1eca758f905e69aa86f
# Git build branch: mlegleux/conan2
# $ ldd ./build/Release/rippled
#         linux-vdso.so.1 (0x000078b7a8762000)
#         libstdc++.so.6 => /lib/x86_64-linux-gnu/libstdc++.so.6 (0x000078b7a5645000)
#         libm.so.6 => /lib/x86_64-linux-gnu/libm.so.6 (0x000078b7a555e000)
#         libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x000078b7a5333000)
#         /lib64/ld-linux-x86-64.so.2 (0x000078b7a8764000)
#         libgcc_s.so.1 => /lib/x86_64-linux-gnu/libgcc_s.so.1 (0x000078b7a5313000)

#!/usr/bin/env python3
import argparse
import itertools
import json
import re

'''
Generate a strategy matrix for GitHub Actions CI.

On each PR commit we will run the following configurations:
- Debian Bookworm on amd64 in Debug for GCC 12-14 and Clang 16-18 with unity.
- MacOS on arm64 in Debug with unity.
- Windows on amd64 in Debug with unity.

Upon merge into the develop, release, or master branch, we will run all
Debian, RHEL, Ubuntu, MacOS, and Windows configurations.

We will further set additional CMake arguments as follows:
- All builds will have the `tests`, `werr`, and `xrpld` options.
- All builds will have the `wextra` option except for GCC 12 and Clang 16.
- All release builds will have the `assert` option.
- Debian Bookworm on amd64 in Debug using GCC 13: reference fee=500
- Debian Bookworm on amd64 in Debug using GCC 14: codecov
- Debian Bookworm on amd64 in Debug using Clang 17: reference fee=1000
- Debian Bookworm on amd64 in Debug using Clang 18: voidstar
'''
def generate_strategy_matrix(pr: bool, architecture: list[dict], os: list[dict], build_type: list[str], cmake_args: list[str]) -> dict:
    configurations = []
    for architecture, os, build_type, cmake_args in itertools.product(architecture, os, build_type, cmake_args):

        # Checks for PR commits.
        if pr:
            # Only run in Debug with unity.
            if build_type != "Debug" or "-Dunity=ON" not in cmake_args:
                continue

            # Checks for Debian Bookworm.
            if f"{os["distro"]}-{os["release"]}" == "debian-bookworm":
                if architecture["platform"] != "linux/amd64":
                    continue
                if not f"{os["compiler_name"]}-{os["compiler_version"]}" in ["gcc-12", "gcc-13", "gcc-14", "clang-16", "clang-17", "clang-18"]:
                    continue

                if f"{os["compiler_name"]}-{os["compiler_version"]}" == "gcc-13":
                    cmake_args = f"{cmake_args} -DUNIT_TEST_REFERENCE_FEE=500"
                elif f"{os["compiler_name"]}-{os["compiler_version"]}" == "gcc-14":
                    cmake_args = f"{cmake_args} -Dcoverage=ON -Dcoverage_format=xml -DCODE_COVERAGE_VERBOSE=ON -DCMAKE_C_FLAGS='-O0' -DCMAKE_CXX_FLAGS='-O0'"
                elif f"{os["compiler_name"]}-{os["compiler_version"]}" == "clang-17":
                    cmake_args = f"{cmake_args} -DUNIT_TEST_REFERENCE_FEE=1000"
                elif f"{os["compiler_name"]}-{os["compiler_version"]}" == "clang-18":
                    cmake_args = f"{cmake_args} -Dvoidstar=ON"

            # Checks for RHEL and Ubuntu.
            if os["distro"] == "rhel" or os["distro"] == "ubuntu":
                continue

            # Checks for MacOS.
            if os["distro"] == "macos" and architecture["platform"] != "macos/arm64":
                continue

            # Checks for Windows.
            if os["distro"] == "windows" and architecture["platform"] != "windows/amd64":
                continue

        # Additional CMake arguments.
        cmake_args = f"{cmake_args} -Dtests=ON -Dwerr=ON -Dxrpld=ON"
        if not f"{os["compiler_name"]}-{os["compiler_version"]}" in ["gcc-12", "clang-16"]:
            cmake_args = f"{cmake_args} -Dwextra=ON"
        if build_type == "Release":
            cmake_args = f"{cmake_args} -Dassert=ON"

        configurations.append({
            "architecture": architecture,
            "os": os,
            "build_type": build_type,
            "cmake_args": cmake_args,
        })

    return {"include": configurations}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("-c", "--config", help="Path to the JSON file containing the strategy matrix configuration.")
    parser.add_argument("-r", "--ref", help="Git reference to generate the strategy matrix for (e.g. /refs/heads/develop).")
    args = parser.parse_args()

    # Only generate a matrix for branches, not tags or other refs.
    if not args.ref.startswith("refs/heads/"):
        print("matrix={}")
        exit(0)

    # Load the JSON configuration file.
    config = None
    try:
        with open(args.config, 'r') as f:
            config = json.load(f)
        if config['architecture'] is None or config['os'] is None or config['build_type'] is None or config['cmake_args'] is None:
            raise Exception("Invalid configuration file.")
    except:
        print("matrix={}")
        exit(0)

    # Check if the ref is a branch that should trigger a full matrix.
    match = re.search(r"refs/heads/(develop|master|release)", args.ref)
    print(f"matrix={json.dumps(generate_strategy_matrix(match is None, config['architecture'], config['os'], config['build_type'], config['cmake_args']))}")

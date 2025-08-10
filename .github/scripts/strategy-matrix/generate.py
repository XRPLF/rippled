#!/usr/bin/env python3
import argparse
import itertools
import json
import re

'''
Generate a strategy matrix for GitHub Actions CI.

On each PR commit we will only build the Debian, RHEL, Ubuntu, MacOS, and
Windows configurations in Debug with unity enabled.

Upon merge into the develop, release, or master branch, we will build all
Debian, RHEL, Ubuntu, MacOS, and Windows configurations.

We will further set additional CMake arguments as follows:
- All builds will have the `tests`, `werr`, and `xrpld` options.
- All builds will have the `wextra` option except for GCC 12 and Clang 16.
- All release builds will have the `assert` option.
- Those specified in the `cmake_extras` dict in the configuration file that only
  apply to certain builds.
'''
def generate_strategy_matrix(pr: bool, architecture: list[dict], os: list[dict], build_type: list[str], cmake_args: list[str], cmake_extras: list[dict]) -> dict:
    configurations = []
    for architecture, os, build_type, cmake_args in itertools.product(architecture, os, build_type, cmake_args):

        # Only run in Debug with unity.
        if pr and (build_type != "Debug" or "-Dunity=ON" not in cmake_args):
            continue

        # Additional CMake arguments.
        cmake_args = f"{cmake_args} -Dtests=ON -Dwerr=ON -Dxrpld=ON"
        if not f"{os["compiler_name"]}-{os["compiler_version"]}" in ["gcc-12", "clang-16"]:
            cmake_args = f"{cmake_args} -Dwextra=ON"
        if build_type == "distro_version":
            cmake_args = f"{cmake_args} -Dassert=ON"
        if (cfg := f"{architecture["platform"]}|{os["distro_name"]}-{os["distro_version"]}-{os["compiler_name"]}-{os["compiler_version"]}|{build_type}") in cmake_extras:
            cmake_args = f"{cmake_args} {cmake_extras[cfg]}"

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
    with open(args.config, 'r') as f:
        config = json.load(f)
    if config['architecture'] is None or config['os'] is None or config['build_type'] is None or config['cmake_args'] is None or config['cmake_extras'] is None:
        raise Exception("Invalid configuration file.")

    # Check if the ref is a branch that should trigger a full matrix.
    match = re.search(r"refs/heads/(develop|master|release)", args.ref)
    print(f"matrix={json.dumps(generate_strategy_matrix(match is None, config['architecture'], config['os'], config['build_type'], config['cmake_args'], config['cmake_extras']))}")

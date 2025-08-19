#!/usr/bin/env python3
import argparse
import itertools
import json
import re

'''
Generate a strategy matrix for GitHub Actions CI.

On each PR commit we will build a selection of Debian, RHEL, Ubuntu, MacOS, and
Windows configurations, while upon merge into the develop, release, or master
branches, we will build all configurations, and test most of them.

We will further set additional CMake arguments as follows:
- All builds will have the `tests`, `werr`, and `xrpld` options.
- All builds will have the `wextra` option except for GCC 12 and Clang 16.
- All release builds will have the `assert` option.
- Certain Debian Bookworm configurations will change the reference fee, enable
  codecov, and enable voidstar in PRs.
'''
def generate_strategy_matrix(all: bool, architecture: list[dict], os: list[dict], build_type: list[str], cmake_args: list[str]) -> dict:
    configurations = []
    for architecture, os, build_type, cmake_args in itertools.product(architecture, os, build_type, cmake_args):
        # The default CMake target is 'all' for Linux and MacOS and 'install'
        # for Windows, but it can get overridden for certain configurations.
        cmake_target = 'install' if os["distro_name"] == 'windows' else 'all'

        # We build and test all configurations by default, except for Windows in
        # Debug, because it is too slow, as well as when code coverage is
        # enabled as that mode already runs the tests.
        build_only = False
        if os['distro_name'] == 'windows' and build_type == 'Debug':
            build_only = True

        # Only generate a subset of configurations in PRs.
        if not all:
            # Debian:
            # - Bookworm using GCC 13: Release and Unity on linux/arm64, set
            #   the reference fee to 500.
            # - Bookworm using GCC 15: Debug and no Unity on linux/amd64, enable
            #   code coverage.
            # - Bookworm using Clang 16: Debug and no Unity on linux/arm64,
            #   enable voidstar.
            # - Bookworm using Clang 17: Release and no Unity on linux/amd64,
            #   set the reference fee to 1000.
            # - Bookworm using Clang 20: Debug and Unity on linux/amd64.
            if os['distro_name'] == 'debian':
                skip = True
                if os['distro_version'] == 'bookworm':
                    if f'{os['compiler_name']}-{os['compiler_version']}' == 'gcc-13' and build_type == 'Release' and '-Dunity=ON' in cmake_args and architecture['platform'] == 'linux/arm64':
                        cmake_args = f'{cmake_args} -DUNIT_TEST_REFERENCE_FEE=500'
                        skip = False
                    if f'{os['compiler_name']}-{os['compiler_version']}' == 'gcc-15' and build_type == 'Debug' and '-Dunity=OFF' in cmake_args and architecture['platform'] == 'linux/amd64':
                        cmake_args = f'{cmake_args} -Dcoverage=ON -Dcoverage_format=xml -DCODE_COVERAGE_VERBOSE=ON -DCMAKE_C_FLAGS=-O0 -DCMAKE_CXX_FLAGS=-O0'
                        cmake_target = 'coverage'
                        build_only = True
                        skip = False
                    if f'{os['compiler_name']}-{os['compiler_version']}' == 'clang-16' and build_type == 'Debug' and '-Dunity=OFF' in cmake_args and architecture['platform'] == 'linux/arm64':
                        cmake_args = f'{cmake_args} -Dvoidstar=ON'
                        skip = False
                    if f'{os['compiler_name']}-{os['compiler_version']}' == 'clang-17' and build_type == 'Release' and '-Dunity=ON' in cmake_args and architecture['platform'] == 'linux/amd64':
                        cmake_args = f'{cmake_args} -DUNIT_TEST_REFERENCE_FEE=1000'
                        skip = False
                    if f'{os['compiler_name']}-{os['compiler_version']}' == 'clang-20' and build_type == 'Debug' and '-Dunity=ON' in cmake_args and architecture['platform'] == 'linux/amd64':
                        skip = False
                if skip:
                    continue

            # RHEL:
            # - 9.4 using GCC 12: Debug and Unity on linux/amd64.
            # - 9.6 using Clang: Release and no Unity on linux/amd64.
            if os['distro_name'] == 'rhel':
                skip = True
                if os['distro_version'] == '9.4':
                    if f'{os['compiler_name']}-{os['compiler_version']}' == 'gcc-12' and build_type == 'Debug' and '-Dunity=ON' in cmake_args and architecture['platform'] == 'linux/amd64':
                        skip = False
                elif os['distro_version'] == '9.6':
                    if f'{os['compiler_name']}-{os['compiler_version']}' == 'clang-any' and build_type == 'Release' and '-Dunity=OFF' in cmake_args and architecture['platform'] == 'linux/amd64':
                        skip = False
                if skip:
                    continue

            # Ubuntu:
            # - Jammy using GCC 12: Debug and no Unity on linux/arm64.
            # - Noble using GCC 14: Release and Unity on linux/amd64.
            # - Noble using Clang 18: Debug and no Unity on linux/amd64.
            # - Noble using Clang 19: Release and Unity on linux/arm64.
            if os['distro_name'] == 'ubuntu':
                skip = True
                if os['distro_version'] == 'jammy':
                    if f'{os['compiler_name']}-{os['compiler_version']}' == 'gcc-12' and build_type == 'Debug' and '-Dunity=OFF' in cmake_args and architecture['platform'] == 'linux/arm64':
                        skip = False
                elif os['distro_version'] == 'noble':
                    if f'{os['compiler_name']}-{os['compiler_version']}' == 'gcc-14' and build_type == 'Release' and '-Dunity=ON' in cmake_args and architecture['platform'] == 'linux/amd64':
                        skip = False
                    if f'{os['compiler_name']}-{os['compiler_version']}' == 'clang-18' and build_type == 'Debug' and '-Dunity=OFF' in cmake_args and architecture['platform'] == 'linux/amd64':
                        skip = False
                    if f'{os['compiler_name']}-{os['compiler_version']}' == 'clang-19' and build_type == 'Release' and '-Dunity=ON' in cmake_args and architecture['platform'] == 'linux/arm64':
                        skip = False
                if skip:
                    continue

            # MacOS:
            # - Debug and no Unity on macos/arm64.
            if os['distro_name'] == 'macos' and not (build_type == 'Debug' and '-Dunity=OFF' in cmake_args and architecture['platform'] == 'macos/arm64'):
                continue

            # Windows:
            # - Release and Unity on windows/amd64.
            if os['distro_name'] == 'windows' and not (build_type == 'Release' and '-Dunity=ON' in cmake_args and architecture['platform'] == 'windows/amd64'):
                continue


        # Additional CMake arguments.
        cmake_args = f'{cmake_args} -Dtests=ON -Dwerr=ON -Dxrpld=ON'
        if not f'{os['compiler_name']}-{os['compiler_version']}' in ['gcc-12', 'clang-16']:
            cmake_args = f'{cmake_args} -Dwextra=ON'
        if build_type == 'Release':
            cmake_args = f'{cmake_args} -Dassert=ON'

        # We skip all RHEL on arm64 due to a build failure that needs further
        # investigation.
        if os['distro_name'] == 'rhel' and architecture['platform'] == 'linux/arm64':
            continue

        # Generate a unique name for the configuration, e.g. macos-arm64-debug
        # or debian-bookworm-gcc-12-amd64-release-unity.
        config_name = os['distro_name']
        if (n := os['distro_version']) != '':
            config_name += f'-{n}'
        if (n := os['compiler_name']) != '':
            config_name += f'-{n}'
        if (n := os['compiler_version']) != '':
            config_name += f'-{n}'
        config_name += f'-{architecture['platform'][architecture['platform'].find('/')+1:]}'
        config_name += f'-{build_type.lower()}'
        if '-Dunity=ON' in cmake_args:
            config_name += '-unity'

        configurations.append({
            'architecture': architecture,
            'os': os,
            'build_type': build_type,
            'build_only': 'yes' if build_only else 'no',
            'cmake_args': cmake_args,
            'cmake_target': cmake_target,
            'config_name': config_name,
        })

    return {'include': configurations}


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('-a', '--all', help='Set to generate all configurations (generally used when merging a PR) or leave unset to generate a subset of configurations (generally used when committing to a PR).', action="store_true")
    parser.add_argument('-c', '--config', help='Path to the JSON file containing the strategy matrix configurations.', required=True, type=str)
    args = parser.parse_args()

    # Load the JSON configuration file.
    config = None
    with open(args.config, 'r') as f:
        config = json.load(f)
    if config['architecture'] is None or config['os'] is None or config['build_type'] is None or config['cmake_args'] is None:
        raise Exception('Invalid configuration file.')

    # Generate the strategy matrix.
    print(f'matrix={json.dumps(generate_strategy_matrix(args.all, config['architecture'], config['os'], config['build_type'], config['cmake_args']))}')

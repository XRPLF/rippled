# rippled SConstruct
#

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

"""

    Target          Builds
    ----------------------------------------------------------------------------

    <none>          Same as 'install'
    install         Default target and copies it to build/rippled (default)

    all             All available variants
    debug           All available debug variants
    release         All available release variants
    profile         All available profile variants

    clang           All clang variants
    clang.debug     clang debug variant
    clang.release   clang release variant
    clang.profile   clang profile variant

    gcc             All gcc variants
    gcc.debug       gcc debug variant
    gcc.release     gcc release variant
    gcc.profile     gcc profile variant

    msvc            All msvc variants
    msvc.debug      MSVC debug variant
    msvc.release    MSVC release variant

    vcxproj         Generate Visual Studio 2013 project file

    count           Show line count metrics

    Any individual target can also have ".nounity" appended for a classic,
    non unity build. Example:

        scons gcc.debug.nounity

If the clang toolchain is detected, then the default target will use it, else
the gcc toolchain will be used. On Windows environments, the MSVC toolchain is
also detected.

The following environment variables modify the build environment:
    CLANG_CC
    CLANG_CXX
    CLANG_LINK
      If set, a clang toolchain will be used. These must all be set together.

    GNU_CC
    GNU_CXX
    GNU_LINK
      If set, a gcc toolchain will be used (unless a clang toolchain is
      detected first). These must all be set together.

    CXX
      If set, used to detect a toolchain.

    BOOST_ROOT
      Path to the boost directory.
    OPENSSL_ROOT
      Path to the openssl directory.

"""

#
"""

TODO

- Fix git-describe support
- Fix printing exemplar command lines
- Fix toolchain detection


"""
#-------------------------------------------------------------------------------

import collections
import os
import sys
import textwrap
import SCons.Action

sys.path.append(os.path.join('src', 'beast', 'python'))
sys.path.append(os.path.join('bin', 'python'))

from beast.build.module import Beast, Boost, GitTag, Jemalloc, OpenSSL, Proto
from ripple.build.module import (
    BeastObjectiveC, Default, Ed25519, Unity, WebSockets)

from beast.build import Module, PhonyTargets, Scons, TagSet, Toolchain
from beast.build.CcWarning import disable_warnings
from beast.System import SYSTEM


GIT_TAG_FILE = 'src/ripple/unity/git_id.cpp'

BOOST_LINK_LIBRARIES = [
    'coroutine',
    'context',
    'date_time',
    'filesystem',
    'program_options',
    'regex',
    'system',
    'thread',
]

UNITY_DIRECTORIES = [
    'basics',
    'crypto',
    'json',
    'ledger',
    'net',
    'overlay',
    'peerfinder',
    'protocol',
    'rpc',
    'shamap',
    'test',
    'unl',
]

PROTO_SOURCE_DIRECTORIES = [
    'src/ripple/proto/ripple.proto',
]

MODULES = [
    Jemalloc,  # Must come first as it potentially changes the environment.
    # TODO: move info to run_first.
    Boost.module(link_libraries=BOOST_LINK_LIBRARIES),
    Beast,
    BeastObjectiveC,
    Default,
    Ed25519,
    GitTag.module(GIT_TAG_FILE),
    OpenSSL,
    Proto.module(*PROTO_SOURCE_DIRECTORIES),
    Unity.module(directories=UNITY_DIRECTORIES),
    Unity.module(directories=['nodestore'],
                 CPPPATH=['src/rocksdb2/include',
                          'src/snappy/snappy',
                          'src/snappy/config']),
    WebSockets,
]

TARGETS_TO_TAGS = TagSet.Targets(
    'install',

    install=TagSet.TagSetList(
        ('gcc', 'clang', 'msvc'),
        ('release', 'debug', 'profile'),
        ('unity', 'nounity'),
    ),

    vcxproj=TagSet.TagSetList(),
    count=TagSet.TagSetList(),
)

def run_build():
    """The top-level function that actually runs the build."""
    scons = Scons.Scons(
        globals(),
        variant_tree={'src': 'src'},
        toolpath=[os.path.join ('src', 'beast', 'python', 'site_tools')],
        tools=['default', 'Protoc', 'VSProject'],
        ENV=os.environ,
        TARGET_ARCH='x86_64')

    scons.run_once(MODULES)

    # Configure the toolchains, variants, default toolchain, and default target.
    detect = Toolchain.detect(scons)
    if not detect:
        raise ValueError('No toolchains detected!')

    targets = list(COMMAND_LINE_TARGETS) or [TARGETS_TO_TAGS.default_argument]
    toolchains = detect.keys()

    for target, tags_list in TARGETS_TO_TAGS.targets_to_tags(targets).items():
        if target == 'install':
            for tags in tags_list:
                clone = scons.clone()
                toolchain = tags[0]
                clone.env.Replace(**detect.get(toolchain, {}))
                add_ripple_files(clone, tags, toolchains)

    for key, value in scons.aliases.iteritems():
        scons.env.Alias(key, value)

    vcxproj = scons.env.VSProject(
        os.path.join('Builds', 'VisualStudio2013', 'RippleD'),
        source=[],
        VSPROJECT_ROOT_DIRS=['src/beast', 'src', '.'],
        VSPROJECT_CONFIGS=scons.msvc_configs)
    scons.env.Alias('vcxproj', vcxproj)
    base_path = os.path.join('src', 'ripple')
    PhonyTargets.build(scons.env,
                       count=PhonyTargets.count_tests(base_path))


def get_soci_sources(scons):
    cpp_path = ['src/soci/src/core', 'src/sqlite']
    scons.add_source_files(
        'src/ripple/unity/soci.cpp',
        CPPPATH=cpp_path)
    if 'unity' in scons.tags:
        scons.add_source_files(
            'src/ripple/unity/soci_ripple.cpp',
            CPPPATH=cpp_path)


def get_all_sources(scons):
    if 'unity' in scons.tags:
        scons.add_source_files(
            'src/ripple/unity/app_ledger.cpp',
            'src/ripple/unity/app_main.cpp',
            'src/ripple/unity/app_misc.cpp',
            'src/ripple/unity/app_paths.cpp',
            'src/ripple/unity/app_tests.cpp',
            'src/ripple/unity/app_tx.cpp',
            'src/ripple/unity/core.cpp',
        )
    else:
        scons.add_source_by_directory(
            'src/ripple/core',
            CPPPATH=['src/soci/src/core', 'src/sqlite'])

        scons.add_source_by_directory(
            'src/ripple/app',
            'src/ripple/legacy',
        )


def add_ripple_files(scons, tags, toolchains):
    scons.tags = list(tags)
    toolchain, variant, tu_style = scons.tags
    default_toolchain = toolchains[0]

    if 'profile' in scons.tags and 'msvc' in scons.tags:
        return
    scons.set_variant_name('.'.join(tags).replace('.unity', ''))

    scons.tags.append(SYSTEM.platform.lower())
    if SYSTEM.linux:
        scons.tags.append('linux')

    if SYSTEM.osx:
        scons.tags.append('osx')

    scons.env.Append(CPPPATH=[os.path.join('src','soci','src')])
    scons.run_private(scons.tags, MODULES)
    scons.run_variants()

    get_all_sources(scons)
    get_soci_sources(scons)

    scons.add_source_files(
        'src/beast/beast/unity/hash_unity.cpp',
        'src/ripple/unity/beast.cpp',
        'src/ripple/unity/lz4.c',
        'src/ripple/unity/protobuf.cpp',
        'src/ripple/unity/ripple.proto.cpp',
        'src/ripple/unity/resource.cpp',
        'src/ripple/unity/server.cpp',
    )
    scons.add_source_files(
        'src/ripple/unity/beastc.c',
        CCFLAGS=disable_warnings(scons.tags, 'array-bounds'))

    scons.add_source_files(
        'src/ripple/unity/secp256k1.cpp',
        CPPPATH=['src/secp256k1'],
        CCFLAGS=disable_warnings(scons.tags, 'unused-function'))

    scons.add_source_files(
        'src/ripple/unity/rocksdb.cpp',
        CPPPATH=[
            'src/rocksdb2',
            'src/rocksdb2/include',
            'src/snappy/snappy',
            'src/snappy/config',
        ],
        CCFLAG=disable_warnings(scons.tags, 'maybe-uninitialized')
    )

    scons.add_source_files(
        'src/ripple/unity/snappy.cpp',
        CCFLAGS=disable_warnings(scons.tags, 'unused-function'),
        CPPPATH=['src/snappy/snappy', 'src/snappy/config']
    )

    target = scons.env.Program(
        target=os.path.join(scons.variant_directory(), 'rippled'),
        source=scons.objects
        )

    # print('   NAME: ', [t.name for t in target])

    # TODO: understand and clean this.
    if 'unity' in scons.tags:
        if toolchain == default_toolchain and 'release' in scons.tags:
            install_target = scons.env.Install(scons.build_dir, source=target)
            scons.env.Alias('install', install_target)
            scons.env.Default(install_target)
            scons.aliases['all'].extend(install_target)
        if 'msvc' in scons.tags:
            config = scons.env.VSProjectConfig(
                variant, 'x64', target, scons.env)
            scons.msvc_configs.append(config)

        if toolchain in toolchains:
            scons.aliases['all'].extend(target)
            scons.aliases[toolchain].extend(target)

    elif 'msvc' in scons.tags:
        config = scons.env.VSProjectConfig(
            variant + '.classic', 'x64', target, scons.env)
        scons.msvc_configs.append(config)

    if toolchain in toolchains:
        scons.aliases[variant].extend(target)
        scons.env.Alias(scons.variant_name, target)


run_build()

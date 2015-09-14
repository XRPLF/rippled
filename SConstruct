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

import os, sys

sys.path.append(os.path.join('src', 'beast', 'python'))
sys.path.append(os.path.join('bin', 'python'))


from beast.build import Git
from beast.build.Build import (
    Env, Module, Target, compose, files, for_tags, not_tags, run_build)
from beast.build.module import (
    AddTarget, Beast, Boost, CountTests, Jemalloc, MSVC, OpenSSL, Proto,
    Vcxproj)

from ripple.build.module import Default, Unity

BOOST = Boost.module(
    link_libraries=[
        'coroutine',
        'context',
        'date_time',
        'filesystem',
        'program_options',
        'regex',
        'system',
        'thread',
    ],
)

BEAST = Beast.MODULE

COUNT_TESTS = CountTests.module(os.path.join('src', 'ripple'))

ED25519 = Module(
    files=files(
        'src/ripple/unity/ed25519.c',
        CPPPATH=['src/ed25519-donna'],
    ),
)

GIT_TAG = Module(
    files=files(
        'src/ripple/unity/git_id.cpp',
        **Git.git_tag()),
)

LEGACY = Module(
    files=compose(
        Unity.files(
            unity_files=[
                'app_ledger.cpp',
                'app_main.cpp',
                'app_misc.cpp',
                'app_paths.cpp',
                'app_tests.cpp',
                'app_tx.cpp',
                'core.cpp',
            ],
            nounity_directories=['app', 'legacy'],
        ),

        files(
            'src/ripple/unity/lz4.c',
            'src/ripple/unity/resource.cpp',
            'src/ripple/unity/server.cpp',
        ),
    ),
)

NODE_STORE = Module(
    files=Unity.files(
        modules=['nodestore'],
        CPPPATH=[
            'src/rocksdb2/include',
            'src/snappy/snappy',
            'src/snappy/config',
        ],
    ),
)

PROTO = Module(
    before=Proto.before('src/ripple/proto/ripple.proto'),

    files=compose(
        Proto.files,
        files(
            'src/ripple/unity/protobuf.cpp',
            'src/ripple/unity/ripple.proto.cpp',
        ),
    ),
)

UNITY = Module(
    files=Unity.files(
        modules=[
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
        ],
    ),
)

ROCKS_DB = Module(
    files = files(
        'src/ripple/unity/rocksdb.cpp',
        CPPPATH=[
            'src/rocksdb2',
            'src/rocksdb2/include',
            'src/snappy/snappy',
            'src/snappy/config',
        ],
        disable_warnings='maybe-uninitialized',
    ),
)

SECP256K1 = Module(
    files=files(
        'src/ripple/unity/secp256k1.cpp',
        CPPPATH=['src/secp256k1'],
        disable_warnings='unused-function',
    ),
)

SOCI = Module(
    setup=compose(
        Env.Append(
            CPPPATH=[
                os.path.join('src','soci','src'),
                os.path.join('src','soci','include'),
            ],
        ),
    ),

    files=compose(
        files(
            'src/ripple/unity/soci.cpp',
            CPPPATH=[
                'src/soci/src/core',
                'src/soci/include/private',
                'src/sqlite',
            ],
            disable_warnings='deprecated-declarations',
        ),

        Unity.files(
            unity_files=['soci_ripple.cpp'],
            nounity_directories=['core'],
            CPPPATH=['src/soci/src/core', 'src/sqlite'],
        ),
    ),
)

SNAPPY = Module(
    files=files(
        'src/ripple/unity/snappy.cpp',
        CPPPATH=['src/snappy/snappy', 'src/snappy/config'],
        disable_warnings='unused-function',
    ),
)

SQLITE = Module(
    files=files(
        'src/ripple/unity/sqlite.c',
        disable_warnings='array-bounds',
    ),
)


TARGETS = Module(
    target=compose(
        for_tags(
            'unity', 'default_toolchain', 'release',
            AddTarget.make_default,
        ),

        for_tags(
            'unity',
            AddTarget.add_to_all,
        ),
    ),
)

VCXPROJ = Vcxproj.module(
    os.path.join('Builds', 'VisualStudio2015', 'RippleD'),
    source=[],
    VSPROJECT_ROOT_DIRS=['src/beast', 'src', '.'],
)

WEBSOCKETS = Module(
    files=compose(
        files('src/ripple/unity/websocket02.cpp'),
        files('src/ripple/unity/websocket04.cpp', CPPPATH='src/websocketpp'),
    ),
)

MODULES = (
    Default.MODULE,
    BOOST,
    BEAST,
    COUNT_TESTS,
    ED25519,
    GIT_TAG,
    Jemalloc.MODULE,
    LEGACY,
    MSVC.MODULE,
    NODE_STORE,
    OpenSSL.MODULE,
    PROTO,
    ROCKS_DB,
    SECP256K1,
    SNAPPY,
    SOCI,
    SQLITE,
    TARGETS,
    UNITY,
    VCXPROJ,
    WEBSOCKETS,
)

MSVC_GROUPS = (
    ('msvc',),
    ('release', 'debug', 'profile'),
    ('unity', 'nounity'),
)


run_build(
    sconstruct_globals=globals(),
    modules=MODULES,

    targets=[
        Target('install', tag_groups=Target.CPP_GROUPS, result_name='rippled'),
        Target('vcxproj', tag_groups=MSVC_GROUPS, result_name='rippled'),
        Target('count_tests'),
    ],
)

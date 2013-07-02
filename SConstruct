#
# Ripple - SConstruct
#

import commands
import copy
import glob
import os
import platform
import re

LevelDB = bool(1)

OSX = bool(platform.mac_ver()[0])
FreeBSD = bool('FreeBSD' == platform.system())
Linux   = bool('Linux' == platform.system())
Ubuntu  = bool(Linux and 'Ubuntu' == platform.linux_distribution()[0])

if OSX or Ubuntu:
    CTAGS = '/usr/bin/ctags'
else:
    CTAGS = '/usr/bin/exuberant-ctags'

#
# scons tools
#

env = Environment(
    tools = ['default', 'protoc']
)

GCC_VERSION = re.split('\.', commands.getoutput(env['CXX'] + ' -dumpversion'))

# Use clang
#env.Replace(CC = 'clang')
#env.Replace(CXX = 'clang++')

#
# Builder for CTags
#
ctags = Builder(action = '$CTAGS $CTAGSOPTIONS -f $TARGET $SOURCES')
env.Append(BUILDERS = { 'CTags' : ctags })
if OSX:
    env.Replace(CTAGS = CTAGS)
else:
    env.Replace(CTAGS = CTAGS, CTAGSOPTIONS = '--tag-relative')

# Use openssl
env.ParseConfig('pkg-config --cflags --libs openssl')

# The required version of boost is documented in the README file.
#
# We whitelist platforms where the non -mt version is linked with pthreads.
#   This can be verified with: ldd libboost_filesystem.*
#   If a threading library is included the platform can be whitelisted.
#
# FreeBSD and Ubuntu non-mt libs do link with pthreads.
if FreeBSD or Ubuntu:
    env.Append(
        LIBS = [
            'boost_date_time',
            'boost_filesystem',
            'boost_program_options',
            'boost_regex',
            'boost_system',
            'boost_thread',
            'boost_random',
        ]
    )
else:
    env.Append(
        LIBS = [
            'boost_date_time-mt',
            'boost_filesystem-mt',
            'boost_program_options-mt',
            'boost_regex-mt',
            'boost_system-mt',
            'boost_thread-mt',
            'boost_random-mt',
        ]
    )

#-------------------------------------------------------------------------------
#
# VFALCO This is my oasis of sanity. Nothing having to do with directories,
#         source files, or include paths should reside outside the boundaries.
#

# List of includes passed to the C++ compiler.
# These are all relative to the repo dir.
#
INCLUDE_PATHS = [
    '.',
    'build/proto',
    'Subtrees',
    'Subtrees/leveldb',
    'Subtrees/leveldb/port',
    'Subtrees/leveldb/include',
    'Subtrees/beast',
    'src/cpp/ripple'
    ]

COMPILED_FILES = [
    'Subtrees/beast/modules/beast_core/beast_core.cpp',
    'Subtrees/beast/modules/beast_basics/beast_basics.cpp',
    'modules/ripple_basics/ripple_basics.cpp',
    'modules/ripple_core/ripple_core.cpp',
    'modules/ripple_data/ripple_data.cpp',
    'modules/ripple_json/ripple_json.cpp',
    'modules/ripple_leveldb/ripple_leveldb.cpp',
    'modules/ripple_websocket/ripple_websocket.cpp',
    'modules/ripple_sqlite/ripple_sqlite.c',
    'modules/ripple_app/ripple_app_pt1.cpp',
    'modules/ripple_app/ripple_app_pt2.cpp',
    'modules/ripple_app/ripple_app_pt3.cpp',
    'modules/ripple_app/ripple_app_pt4.cpp',
    'modules/ripple_app/ripple_app_pt5.cpp',
    'modules/ripple_app/ripple_app_pt6.cpp',
    'modules/ripple_app/ripple_app_pt7.cpp',
    'modules/ripple_app/ripple_app_pt8.cpp'
    ]

#-------------------------------------------------------------------------------

# Map top level source directories to their location in the outputs
#

VariantDir('build/obj/src', 'src', duplicate=0)
VariantDir('build/obj/modules', 'modules', duplicate=0)
VariantDir('build/obj/Subtrees', 'Subtrees', duplicate=0)

#-------------------------------------------------------------------------------

# Add the list of includes to compiler include paths.
#
for path in INCLUDE_PATHS:
    env.Append (CPPPATH = [ path ])

#-------------------------------------------------------------------------------

# Apparently, only linux uses -ldl
if not FreeBSD:
    env.Append(
        LIBS = [
            'dl', # dynamic linking for linux
        ]
    )

# Apparently, pkg-config --libs protobuf on bsd fails to provide this necessary include dir.
if FreeBSD:
    env.Append(LINKFLAGS = ['-I/usr/local/include'])
    env.Append(CXXFLAGS = ['-DOS_FREEBSD'])

env.Append(
    LIBS = [
        'rt',           # for clock_nanosleep in beast
        'protobuf',
        'z'
    ]
)

DEBUGFLAGS  = ['-g', '-DDEBUG']
BOOSTFLAGS  = ['-DBOOST_TEST_DYN_LINK', '-DBOOST_FILESYSTEM_NO_DEPRECATED']

env.Append(LINKFLAGS = ['-rdynamic', '-pthread'])
env.Append(CCFLAGS = ['-pthread', '-Wall', '-Wno-sign-compare', '-Wno-char-subscripts'])
env.Append(CXXFLAGS = ['-O0', '-pthread', '-Wno-invalid-offsetof', '-Wformat']+BOOSTFLAGS+DEBUGFLAGS)

# RTTI is required for Beast and CountedObject.
#
env.Append(CXXFLAGS = ['-frtti'])

if (int(GCC_VERSION[0]) > 4 or (int(GCC_VERSION[0]) == 4 and int(GCC_VERSION[1]) >= 7)):
    env.Append(CXXFLAGS = ['-std=c++11'])

if OSX:
    env.Append(LINKFLAGS = ['-L/usr/local/opt/openssl/lib'])
    env.Append(CXXFLAGS = ['-I/usr/local/opt/openssl/include'])

PROTO_SRCS = env.Protoc([], 'src/cpp/ripple/ripple.proto', PROTOCOUTDIR='build/proto', PROTOCPYTHONOUTDIR=None)
env.Clean(PROTO_SRCS, 'site_scons/site_tools/protoc.pyc')

# Only tag actual Ripple files.
TAG_SRCS    = copy.copy(COMPILED_FILES)

# Derive the object files from the source files.
OBJECT_FILES = []

OBJECT_FILES += PROTO_SRCS

for file in COMPILED_FILES:
    OBJECT_FILES.append('build/obj/' + file)

#
# Targets
#

rippled = env.Program('build/rippled', OBJECT_FILES)

tags    = env.CTags('tags', TAG_SRCS)

Default(rippled, tags)

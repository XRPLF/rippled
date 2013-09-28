#
# Ripple - SConstruct
#

import commands
import copy
import glob
import os
import platform
import re

OSX = bool(platform.mac_ver()[0])
FreeBSD = bool('FreeBSD' == platform.system())
Linux   = bool('Linux' == platform.system())
Ubuntu  = bool(Linux and 'Ubuntu' == platform.linux_distribution()[0])
Archlinux  = bool(Linux and ('','','') == platform.linux_distribution()) #Arch still has issues with the platform module

#
# We expect this to be set
# 
BOOST_HOME = os.environ.get("RIPPLED_BOOST_HOME", None) 


if OSX or Ubuntu or Archlinux:
    CTAGS = 'ctags'
elif FreeBSD:
    CTAGS = 'exctags'
else:
    CTAGS = 'exuberant-ctags'

#
# scons tools
#

env = Environment(
    tools = ['default', 'protoc']
)

# Use a newer gcc on FreeBSD
if FreeBSD:
    env.Replace(CC = 'gcc46')
    env.Replace(CXX = 'g++46')
    env.Append(CCFLAGS = ['-Wl,-rpath=/usr/local/lib/gcc46'])
    env.Append(LINKFLAGS = ['-Wl,-rpath=/usr/local/lib/gcc46'])

if OSX:
    env.Replace(CC= 'clang')
    env.Replace(CXX= 'clang++')
    env.Append(CXXFLAGS = ['-std=c++11', '-stdlib=libc++'])
    env.Append(LINKFLAGS='-stdlib=libc++')
    env['FRAMEWORKS'] = ['AppKit']

GCC_VERSION = re.split('\.', commands.getoutput(env['CXX'] + ' -dumpversion'))

# Add support for ccache. Usage: scons ccache=1
ccache = ARGUMENTS.get('ccache', 0)
if int(ccache):
    env.Prepend(CC = ['ccache'])
    env.Prepend(CXX = ['ccache'])
    ccache_dir = os.getenv('CCACHE_DIR')
    if ccache_dir:
        env.Replace(CCACHE_DIR = ccache_dir)

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
env.ParseConfig('pkg-config --static --cflags --libs openssl')
# Use protobuf
env.ParseConfig('pkg-config --static --cflags --libs protobuf')

# Beast uses kvm on FreeBSD
if FreeBSD:
    env.Append (
        LIBS = [
            'kvm'
        ]
    )

# The required version of boost is documented in the README file.
BOOST_LIBS = [
    'boost_date_time',
    'boost_filesystem',
    'boost_program_options',
    'boost_regex',
    'boost_system',
    'boost_thread',
    'boost_random',
]

# We whitelist platforms where the non -mt version is linked with pthreads. This
# can be verified with: ldd libboost_filesystem.* If a threading library is
# included the platform can be whitelisted.
if FreeBSD or Ubuntu or Archlinux or OSX:
    # non-mt libs do link with pthreads.
    env.Append(
        LIBS = BOOST_LIBS
    )
else:
    env.Append(
        LIBS = [l + '-mt' for l in BOOST_LIBS]
    )

#-------------------------------------------------------------------------------
#
# VFALCO NOTE Clean area.
#
#-------------------------------------------------------------------------------
#
# Nothing having to do with directories, source files,
# or include paths should reside outside the boundaries.
#

# List of includes passed to the C++ compiler.
# These are all relative to the repo dir.
#
INCLUDE_PATHS = [
    '.',
    'src',
    'src/leveldb',
    'src/leveldb/port',
    'src/leveldb/include',
    'src/beast',
    'build/proto'
    ]

# if BOOST_HOME:
#     INCLUDE_PATHS.append(BOOST_HOME)

#-------------------------------------------------------------------------------
#
# Compiled sources
#

COMPILED_FILES = []

# -------------------
# Beast unity sources
#
if OSX:
    # OSX: Use the Objective C++ version of beast_core
    COMPILED_FILES.extend (['src/ripple/beast/ripple_beastobjc.mm'])
else:
    COMPILED_FILES.extend (['src/ripple/beast/ripple_beast.cpp'])
COMPILED_FILES.extend (['src/ripple/beast/ripple_beastc.c'])

# ------------------------------
# New-style Ripple unity sources
#
COMPILED_FILES.extend([
    'src/ripple/http/ripple_http.cpp',
    'src/ripple/json/ripple_json.cpp',
    'src/ripple/rpc/ripple_rpc.cpp',
    'src/ripple/sophia/ripple_sophia.c',
    'src/ripple/sslutil/ripple_sslutil.cpp',
    'src/ripple/testoverlay/ripple_testoverlay.cpp',
    'src/ripple/types/ripple_types.cpp',
    'src/ripple/validators/ripple_validators.cpp'
    ])

# ------------------------------
# Old-style Ripple unity sources
#
COMPILED_FILES.extend([
    'src/ripple_app/ripple_app.cpp',
    'src/ripple_app/ripple_app_pt1.cpp',
    'src/ripple_app/ripple_app_pt2.cpp',
    'src/ripple_app/ripple_app_pt3.cpp',
    'src/ripple_app/ripple_app_pt4.cpp',
    'src/ripple_app/ripple_app_pt5.cpp',
    'src/ripple_app/ripple_app_pt6.cpp',
    'src/ripple_app/ripple_app_pt7.cpp',
    'src/ripple_app/ripple_app_pt8.cpp',
    'src/ripple_basics/ripple_basics.cpp',
    'src/ripple_core/ripple_core.cpp',
    'src/ripple_data/ripple_data.cpp',
    'src/ripple_hyperleveldb/ripple_hyperleveldb.cpp',
    'src/ripple_leveldb/ripple_leveldb.cpp',
    'src/ripple_mdb/ripple_mdb.c',
    'src/ripple_net/ripple_net.cpp',
    'src/ripple_websocket/ripple_websocket.cpp'
    ])

#
#
#-------------------------------------------------------------------------------

# Map top level source directories to their location in the outputs
#

VariantDir('build/obj/src', 'src', duplicate=0)

#-------------------------------------------------------------------------------

# Add the list of includes to compiler include paths.
#
for path in INCLUDE_PATHS:
    env.Append (CPPPATH = [ path ])

if BOOST_HOME:
    env.Prepend (CPPPATH = [ BOOST_HOME ])

#-------------------------------------------------------------------------------

# Apparently, only linux uses -ldl
if Linux: # not FreeBSD:
    env.Append(
        LIBS = [
            'dl', # dynamic linking for linux
        ]
    )

env.Append(
    LIBS = \
        # rt is for clock_nanosleep in beast
        ['rt'] if not OSX else [] +\
        [
            'z'
        ] 
)

# We prepend, in case there's another BOOST somewhere on the path
# such, as installed into `/usr/lib/`
if BOOST_HOME is not None:
    env.Prepend(
        LIBPATH = ["%s/stage/lib" % BOOST_HOME])

if not OSX:
    env.Append(LINKFLAGS = [
        '-rdynamic', '-pthread', 
        ])

DEBUGFLAGS  = ['-g', '-DDEBUG', '-D_DEBUG']

env.Append(CCFLAGS = ['-pthread', '-Wall', '-Wno-sign-compare', '-Wno-char-subscripts']+DEBUGFLAGS)
env.Append(CXXFLAGS = ['-O1', '-pthread', '-Wno-invalid-offsetof', '-Wformat']+DEBUGFLAGS)


# RTTI is required for Beast and CountedObject.
#
env.Append(CXXFLAGS = ['-frtti'])

if (int(GCC_VERSION[0]) == 4 and int(GCC_VERSION[1]) == 6):
    env.Append(CXXFLAGS = ['-std=c++0x'])
elif (int(GCC_VERSION[0]) > 4 or (int(GCC_VERSION[0]) == 4 and int(GCC_VERSION[1]) >= 7)):
    env.Append(CXXFLAGS = ['-std=c++11'])

# FreeBSD doesn't support O_DSYNC
if FreeBSD:
    env.Append(CPPFLAGS = ['-DMDB_DSYNC=O_SYNC'])

if OSX:
    env.Append(LINKFLAGS = ['-L/usr/local/opt/openssl/lib'])
    env.Append(CXXFLAGS = ['-I/usr/local/opt/openssl/include'])

PROTO_SRCS = env.Protoc([], 'src/ripple_data/protocol/ripple.proto', PROTOCOUTDIR='build/proto', PROTOCPYTHONOUTDIR=None)
env.Clean(PROTO_SRCS, 'site_scons/site_tools/protoc.pyc')

# Only tag actual Ripple files.
TAG_SRCS    = copy.copy(COMPILED_FILES)

# Derive the object files from the source files.
OBJECT_FILES = []

OBJECT_FILES.append(PROTO_SRCS[0])

for file in COMPILED_FILES:
    OBJECT_FILES.append('build/obj/' + file)

#
# Targets
#

rippled = env.Program('build/rippled', OBJECT_FILES)

tags    = env.CTags('tags', TAG_SRCS)

Default(rippled, tags)

#
# Ripple - SConstruct
#

import commands
import copy
import glob
import platform
import re

LevelDB	= bool(0)

OSX	= bool(platform.mac_ver()[0])
FreeBSD	= bool('FreeBSD' == platform.system())
Linux	= bool('Linux' == platform.system())
Ubuntu	= bool(Linux and 'Ubuntu' == platform.linux_distribution()[0])

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

#
# Put objects files in their own directory.
#
for dir in ['ripple', 'database', 'json', 'leveldb/db', 'leveldb/port', 'leveldb/include', 'leveldb/table', 'leveldb/util', 'websocketpp']:
	VariantDir('build/obj/'+dir, 'src/cpp/'+dir, duplicate=0)

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
	    ]
    )

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

env.Append(
	LIBS = [
		'protobuf',
		'z'
	]
)

DEBUGFLAGS	= ['-g', '-DDEBUG']
BOOSTFLAGS	= ['-DBOOST_TEST_DYN_LINK', '-DBOOST_FILESYSTEM_NO_DEPRECATED']

env.Append(LINKFLAGS = ['-rdynamic', '-pthread'])
env.Append(CCFLAGS = ['-pthread', '-Wall', '-Wno-sign-compare', '-Wno-char-subscripts', '-DSQLITE_THREADSAFE=1'])
env.Append(CXXFLAGS = ['-O0', '-pthread', '-Wno-invalid-offsetof', '-Wformat']+BOOSTFLAGS+DEBUGFLAGS)

if (int(GCC_VERSION[0]) > 4 or (int(GCC_VERSION[0]) == 4 and int(GCC_VERSION[1]) >= 7)):
    env.Append(CXXFLAGS = ['-std=c++11'])

if OSX:
	env.Append(LINKFLAGS = ['-L/usr/local/opt/openssl/lib'])
	env.Append(CXXFLAGS = ['-I/usr/local/opt/openssl/include'])

if LevelDB:
	env.Append(CXXFLAGS = [ '-Isrc/cpp/leveldb', '-Isrc/cpp/leveldb/port', '-Isrc/cpp/leveldb/include', '-DUSE_LEVELDB', '-DLEVELDB_PLATFORM_POSIX'])

	LEVELDB_PREFIX	= 'src/cpp/leveldb'
	PORTABLE_FILES	= commands.getoutput('find '
	    + LEVELDB_PREFIX + '/db '
	    + LEVELDB_PREFIX + '/util '
	    + LEVELDB_PREFIX + '/table '
	    + ' -name *test*.cc -prune'
	    + ' -o -name *_bench.cc -prune'
	    + ' -o -name leveldb_main.cc -prune'
	    + ' -o -name "*.cc" -print | sort | tr "\n" " "').rstrip()
	LEVELDB_SRCS	= re.split(' ', PORTABLE_FILES)
	LEVELDB_SRCS.append(LEVELDB_PREFIX + '/port/port_posix.cc')

DB_SRCS		= glob.glob('src/cpp/database/*.c') + glob.glob('src/cpp/database/*.cpp')
JSON_SRCS	= glob.glob('src/cpp/json/*.cpp')

WEBSOCKETPP_SRCS = [
	'src/cpp/websocketpp/src/base64/base64.cpp',
	'src/cpp/websocketpp/src/md5/md5.c',
	'src/cpp/websocketpp/src/messages/data.cpp',
	'src/cpp/websocketpp/src/network_utilities.cpp',
	'src/cpp/websocketpp/src/processors/hybi_header.cpp',
	'src/cpp/websocketpp/src/processors/hybi_util.cpp',
	'src/cpp/websocketpp/src/sha1/sha1.cpp',
	'src/cpp/websocketpp/src/uri.cpp'
	]

RIPPLE_SRCS = glob.glob('src/cpp/ripple/*.cpp')
PROTO_SRCS = env.Protoc([], 'src/cpp/ripple/ripple.proto', PROTOCOUTDIR='build/proto', PROTOCPYTHONOUTDIR=None)
env.Append(CXXFLAGS = ['-Ibuild/proto'])

env.Clean(PROTO_SRCS, 'site_scons/site_tools/protoc.pyc')

# Remove unused source files.
UNUSED_SRCS = []

for file in UNUSED_SRCS:
	RIPPLE_SRCS.remove(file)

# Only tag actual Ripple files.
TAG_SRCS    = copy.copy(RIPPLE_SRCS)

# Add other sources.
RIPPLE_SRCS += DB_SRCS + JSON_SRCS + WEBSOCKETPP_SRCS

if LevelDB:
    RIPPLE_SRCS += LEVELDB_SRCS

# Derive the object files from the source files.
RIPPLE_OBJS = []

RIPPLE_OBJS += PROTO_SRCS

for file in RIPPLE_SRCS:
    # Strip src/cpp/
    RIPPLE_OBJS.append('build/obj/' + file[8:])
#
# Targets
#

rippled = env.Program('build/rippled', RIPPLE_OBJS)

tags	= env.CTags('tags', TAG_SRCS)

Default(rippled, tags)


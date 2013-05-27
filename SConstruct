#
# Ripple - SConstruct
#

import commands
import copy
import glob
import platform
import re

LevelDB	= bool(1)

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
		'protobuf',
		'z'
	]
)

DEBUGFLAGS	= ['-g', '-DDEBUG']
BOOSTFLAGS	= ['-DBOOST_TEST_DYN_LINK', '-DBOOST_FILESYSTEM_NO_DEPRECATED']

env.Append(LINKFLAGS = ['-rdynamic', '-pthread'])
env.Append(CCFLAGS = ['-pthread', '-Wall', '-Wno-sign-compare', '-Wno-char-subscripts', '-DSQLITE_THREADSAFE=1'])
env.Append(CXXFLAGS = ['-O0', '-pthread', '-Wno-invalid-offsetof', '-Wformat']+BOOSTFLAGS+DEBUGFLAGS)
env.Append(CXXFLAGS = [ '-Isrc/cpp/leveldb', '-Isrc/cpp/leveldb/port', '-Isrc/cpp/leveldb/include', '-DUSE_LEVELDB'])
env.Append(CXXFLAGS = [ '-Ibuild/proto'])
env.Append(CXXFLAGS = [ '-I.', '-Isrc/cpp/ripple'])

if (int(GCC_VERSION[0]) > 4 or (int(GCC_VERSION[0]) == 4 and int(GCC_VERSION[1]) >= 7)):
    env.Append(CXXFLAGS = ['-std=c++11'])

if OSX:
	env.Append(LINKFLAGS = ['-L/usr/local/opt/openssl/lib'])
	env.Append(CXXFLAGS = ['-I/usr/local/opt/openssl/include'])

RIPPLE_SRCS = [
	'src/cpp/database/sqlite3.c',
	'src/cpp/leveldb_core.cpp',
	'src/cpp/websocket_core.cpp',
	'modules/ripple_basics/ripple_basics.cpp',
	'modules/ripple_client/ripple_client.cpp',
	'modules/ripple_db/ripple_db.cpp',
	'modules/ripple_json/ripple_json.cpp',
	'modules/ripple_ledger/ripple_ledger.cpp',
	'modules/ripple_main/ripple_main.cpp',
	'modules/ripple_mess/ripple_mess.cpp',
	'modules/ripple_net/ripple_net.cpp'
	]

# Put objects files in their own directory.
for dir in ['.', 'ripple', 'database', 'json', 'leveldb/db', 'leveldb/port', 'leveldb/include', 'leveldb/table', 'leveldb/util', 'websocketpp']:
	VariantDir('build/obj/'+dir, 'src/cpp/'+dir, duplicate=0)

for dir in [
             'ripple_basics',
             'ripple_client',
             'ripple_db',
             'ripple_json',
             'ripple_ledger',
	     'ripple_main',
             'ripple_mess',
             'ripple_net'
            ]:
	VariantDir('build/obj/'+dir, 'modules/'+dir, duplicate=0)

PROTO_SRCS = env.Protoc([], 'src/cpp/ripple/ripple.proto', PROTOCOUTDIR='build/proto', PROTOCPYTHONOUTDIR=None)
env.Clean(PROTO_SRCS, 'site_scons/site_tools/protoc.pyc')
# PROTO_SRCS  = [ 'src/cpp/protobuf_core.cpp' ]
# env.Append(CXXFLAGS = ['-Ibuild/proto', '-Isrc/cpp/protobuf/src', '-Isrc/cpp/protobuf/vsprojects' ])

# Remove unused source files.
UNUSED_SRCS = []

for file in UNUSED_SRCS:
	RIPPLE_SRCS.remove(file)

# Only tag actual Ripple files.
TAG_SRCS    = copy.copy(RIPPLE_SRCS)

# Derive the object files from the source files.
RIPPLE_OBJS = []

for file in RIPPLE_SRCS:
    # Strip src/cpp/ or modules/
    RIPPLE_OBJS.append('build/obj/' + file[8:])

#
# Targets
#

rippled = env.Program('build/rippled', RIPPLE_OBJS)

tags	= env.CTags('tags', TAG_SRCS)

Default(rippled, tags)


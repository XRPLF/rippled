#
# Ripple - SConstruct
#

import glob
import platform

OSX	= bool(platform.mac_ver()[0])
FreeBSD	= bool('FreeBSD' == platform.system())
Ubuntu	= bool('Ubuntu' == platform.dist())

if OSX:
	CTAGS = '/usr/bin/ctags'
else:
	CTAGS = '/usr/bin/exuberant-ctags'

#
# scons tools
#

env = Environment(
	tools = ['default', 'protoc']
)

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
for dir in ['ripple', 'database', 'json', 'websocketpp']:
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

env.Append(
	LIBS = [
		'protobuf',
		'dl', # dynamic linking
		'z'
	]
)

DEBUGFLAGS	= ['-g', '-DDEBUG']
BOOSTFLAGS	= ['-DBOOST_TEST_DYN_LINK', '-DBOOST_FILESYSTEM_NO_DEPRECATED']

env.Append(LINKFLAGS = ['-rdynamic', '-pthread'])
env.Append(CCFLAGS = ['-pthread', '-Wall', '-Wno-sign-compare', '-Wno-char-subscripts', '-DSQLITE_THREADSAFE'])
env.Append(CXXFLAGS = ['-O0', '-pthread', '-Wno-invalid-offsetof', '-Wformat']+BOOSTFLAGS+DEBUGFLAGS)

if OSX:
	env.Append(LINKFLAGS = ['-L/usr/local/opt/openssl/lib'])
	env.Append(CXXFLAGS = ['-I/usr/local/opt/openssl/include'])

DB_SRCS   = glob.glob('src/cpp/database/*.c') + glob.glob('src/cpp/database/*.cpp')
JSON_SRCS = glob.glob('src/cpp/json/*.cpp')

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

RIPPLE_SRCS += DB_SRCS + JSON_SRCS + WEBSOCKETPP_SRCS

# Derive the object files from the source files.
RIPPLE_OBJS = []

RIPPLE_OBJS += PROTO_SRCS

for file in RIPPLE_SRCS:
	RIPPLE_OBJS.append('build/obj/' + file[8:])

rippled = env.Program('build/rippled', RIPPLE_OBJS)

tags = env.CTags('tags', RIPPLE_SRCS)

Default(rippled, tags)


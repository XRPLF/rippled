#
# Newcoin - SConstruct
#

import glob

CTAGS = '/usr/bin/exuberant-ctags'

#
# scons tools
#

env = Environment(
    tools = ['default', 'protoc']    
    )

#
# Builder for CTags
#
ctags = Builder(action = '$CTAGS $CTAGSOPTIONS -f $TARGET $SOURCES')
env.Append(BUILDERS = { 'CTags' : ctags })
env.Replace(CTAGS = CTAGS, CTAGSOPTIONS = '--tag-relative')

#
# Put objects files in their own directory.
#
for dir in ['src', 'database', 'json', 'websocketpp']:
    VariantDir('obj/'+dir, dir, duplicate=0)

# Use openssl
env.ParseConfig('pkg-config --cflags --libs openssl')

env.Append(LIBS = [
    'boost_date_time-mt',
    'boost_filesystem-mt',
    'boost_program_options-mt',
    'boost_regex-mt',
    'boost_system-mt',
    'boost_thread-mt',
    'protobuf',
    'dl',			# dynamic linking
    'z'
    ])

DEBUGFLAGS  = ['-g', '-DDEBUG']
BOOSTFLAGS  = ['-DBOOST_TEST_DYN_LINK', '-DBOOST_FILESYSTEM_NO_DEPRECATED']

env.Append(LINKFLAGS = ['-rdynamic', '-pthread'])
env.Append(CCFLAGS = ['-pthread', '-Wall', '-Wno-sign-compare', '-Wno-char-subscripts', '-DSQLITE_THREADSAFE'])
env.Append(CXXFLAGS = ['-O0', '-pthread', '-Wno-invalid-offsetof', '-Wformat']+BOOSTFLAGS+DEBUGFLAGS)

DB_SRCS		    = glob.glob('database/*.c') + glob.glob('database/*.cpp')
JSON_SRCS	    = glob.glob('json/*.cpp')
WEBSOCKETPP_SRCS    = [
			'websocketpp/src/base64/base64.cpp',
			'websocketpp/src/md5/md5.c',
			'websocketpp/src/messages/data.cpp',
			'websocketpp/src/network_utilities.cpp',
			'websocketpp/src/processors/hybi_header.cpp',
			'websocketpp/src/processors/hybi_util.cpp',
			'websocketpp/src/sha1/sha1.cpp',
			'websocketpp/src/uri.cpp'
			]

NEWCOIN_SRCS	    = glob.glob('src/*.cpp')
PROTO_SRCS	    = env.Protoc([], 'src/newcoin.proto', PROTOCOUTDIR='obj', PROTOCPYTHONOUTDIR=None)

env.Clean(PROTO_SRCS, 'site_scons/site_tools/protoc.pyc')

# Remove unused source files.
UNUSED_SRCS	= ['src/HttpReply.cpp', 'src/ValidationCollection.cpp']

for file in UNUSED_SRCS:
    NEWCOIN_SRCS.remove(file)

NEWCOIN_SRCS	+= DB_SRCS + JSON_SRCS + WEBSOCKETPP_SRCS

# Derive the object files from the source files.
NEWCOIN_OBJS	= []

for file in NEWCOIN_SRCS:
    NEWCOIN_OBJS.append('obj/' + file)

NEWCOIN_OBJS	+= PROTO_SRCS

newcoind    = env.Program('newcoind', NEWCOIN_OBJS)

tags	    = env.CTags('obj/tags', NEWCOIN_SRCS)

Default(newcoind, tags)


#
# Newcoin - SConstruct
#

import glob

# Put objects files in their own directory.
for dir in ['src', 'database', 'json', 'util']:
    VariantDir('obj/'+dir, dir, duplicate=0)

env = Environment(
    tools = ['default', 'protoc']    
    )

# Use openssl
env.ParseConfig('pkg-config --cflags --libs openssl')

env.Append(LIBS = [
    'boost_system-mt',
    'boost_filesystem-mt',
    'boost_program_options-mt',
    'boost_thread-mt',
    'protobuf',
    'dl',			# dynamic linking
    'z'
    ])

DEBUGFLAGS  = ['-g', '-DDEBUG']

env.Append(LINKFLAGS = ['-rdynamic', '-pthread'])
env.Append(CCFLAGS = ['-pthread', '-Wall', '-Wno-sign-compare', '-Wno-char-subscripts'])
env.Append(CXXFLAGS = ['-O0', '-pthread', '-Wno-invalid-offsetof', '-Wformat']+DEBUGFLAGS)

DB_SRCS		= glob.glob('database/*.c') + glob.glob('database/*.cpp')
JSON_SRCS	= glob.glob('json/*.cpp')
NEWCOIN_SRCS	= glob.glob('src/*.cpp')
PROTO_SRCS	= env.Protoc([], 'newcoin.proto', PROTOCOUTDIR='obj/src', PROTOCPYTHONOUTDIR=None)
UTIL_SRCS	= glob.glob('util/*.cpp')

env.Clean(PROTO_SRCS, 'site_scons/site_tools/protoc.pyc')

# Remove unused source files.
UNUSED_SRCS	= ['src/HttpReply.cpp', 'src/ValidationCollection.cpp']

for file in UNUSED_SRCS:
    NEWCOIN_SRCS.remove(file)

NEWCOIN_SRCS	+= DB_SRCS + JSON_SRCS + UTIL_SRCS

# Derive the object files from the source files.
NEWCOIN_OBJS	= []

for file in NEWCOIN_SRCS:
    NEWCOIN_OBJS.append('obj/' + file)

NEWCOIN_OBJS	+= PROTO_SRCS

env.Program('newcoind', NEWCOIN_OBJS)


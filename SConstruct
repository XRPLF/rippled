#
# Newcoin - SConstruct
#

import glob

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

DB_SRCS		= glob.glob('database/*.c*')
JSON_SRCS	= glob.glob('json/*.cpp')
NEWCOIN_SRCS	= glob.glob('*.cpp')
PROTO_SRCS	= env.Protoc([], 'newcoin.proto', PROTOCPYTHONOUTDIR=None)
UTIL_SRCS	= glob.glob('util/*.cpp')

UNUSED_SRCS	= ['HttpReply.cpp', 'ValidationCollection.cpp']

for file in UNUSED_SRCS:
    NEWCOIN_SRCS.remove(file)

NEWCOIN_SRCS	+= DB_SRCS + JSON_SRCS + UTIL_SRCS + PROTO_SRCS

env.Program('newcoind', NEWCOIN_SRCS)


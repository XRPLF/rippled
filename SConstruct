#
# Ripple - SConstruct
#

import commands
import copy
import glob
import os
import platform
import re
import sys
import textwrap

OSX = bool(platform.mac_ver()[0])
FreeBSD = bool('FreeBSD' == platform.system())
Linux   = bool('Linux' == platform.system())
Ubuntu  = bool(Linux and 'Ubuntu' == platform.linux_distribution()[0])
Debian  = bool(Linux and 'debian' == platform.linux_distribution()[0])
Fedora  = bool(Linux and 'Fedora' == platform.linux_distribution()[0])
Archlinux  = bool(Linux and ('','','') == platform.linux_distribution()) #Arch still has issues with the platform module

USING_CLANG = OSX or os.environ.get('CC', None) == 'clang'

#
# We expect this to be set
#
BOOST_HOME = os.environ.get("RIPPLED_BOOST_HOME", None)

if OSX or Ubuntu or Debian or Archlinux:
    CTAGS = 'ctags'
elif FreeBSD:
    CTAGS = 'exctags'
else:
    CTAGS = 'exuberant-ctags'

#
# scons tools
#

HONOR_ENVS = ['CC', 'CXX', 'PATH']

env = Environment(
    tools = ['default', 'protoc'],
    ENV = dict((k, os.environ[k]) for k in HONOR_ENVS if k in os.environ)
)

if os.environ.get('CC', None):
    env.Replace(CC = os.environ['CC'])

if os.environ.get('CXX', None):
    env.Replace(CXX = os.environ['CXX'])

if os.environ.get('PATH', None):
    env.Replace(PATH = os.environ['PATH'])

# Use a newer gcc on FreeBSD
if FreeBSD:
    env.Replace(CC = 'gcc46')
    env.Replace(CXX = 'g++46')
    env.Append(CCFLAGS = ['-Wl,-rpath=/usr/local/lib/gcc46'])
    env.Append(LINKFLAGS = ['-Wl,-rpath=/usr/local/lib/gcc46'])

if USING_CLANG:
    env.Replace(CC= 'clang')
    env.Replace(CXX= 'clang++')

    if Linux:
        env.Append(CXXFLAGS = ['-std=c++11', '-stdlib=libstdc++'])
        env.Append(LINKFLAGS='-stdlib=libstdc++')

    if OSX:
        env.Append(CXXFLAGS = ['-std=c++11', '-stdlib=libc++', 
                               '-Wno-deprecated-register'])
        env.Append(LINKFLAGS='-stdlib=libc++')
        env['FRAMEWORKS'] = ['AppKit','Foundation']

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
]

# We whitelist platforms where the non -mt version is linked with pthreads. This
# can be verified with: ldd libboost_filesystem.* If a threading library is
# included the platform can be whitelisted.
# if FreeBSD or Ubuntu or Archlinux:

if not (USING_CLANG and Linux) and (FreeBSD or Ubuntu or Archlinux or Debian or OSX or Fedora):
    # non-mt libs do link with pthreads.
    env.Append(
        LIBS = BOOST_LIBS
    )
elif Linux and USING_CLANG and Ubuntu:
    # It's likely going to be here if using boost 1.55 
    boost_statics = [ ("/usr/lib/x86_64-linux-gnu/lib%s.a" % a) for a in 
                      BOOST_LIBS ]

    if not all(os.path.exists(f) for f in boost_statics):
        # Else here
        boost_statics = [("/usr/lib/lib%s.a" % a) for a in BOOST_LIBS]
    
    env.Append(LIBS = [File(f) for f in boost_statics])
else:
    env.Append(
        LIBS = [l + '-mt' for l in BOOST_LIBS]
    )

#-------------------------------------------------------------------------------
# Change the way that information is printed so that we can get a nice
# output
#-------------------------------------------------------------------------------
BuildLogFile = None

def print_cmd_line_worker(item, fmt, cmd):
    sys.stdout.write(fmt % ("    \033[94m" + item + "\033[0m"))

    global BuildLogFile

    if not BuildLogFile:
        BuildLogFile = open('rippled-build.log', 'w')

    if BuildLogFile:
        wrapper = textwrap.TextWrapper()
        wrapper.break_long_words = False
        wrapper.break_on_hyphens = False
        wrapper.width = 75

        lines = wrapper.wrap(cmd)

        for line in lines:
            BuildLogFile.write("%s\n" % line)


def print_cmd_line(s, target, src, env):
    target = (''.join([str(x) for x in target]))
    source = (''.join([str(x) for x in src]))

    if ('build/rippled' == target):
        print_cmd_line_worker(target, "%s\n", s)
    elif ('tags' == target):
        sys.stdout.write("    Generating tags")
    else:
        print_cmd_line_worker(source, "%s\n", s)


# Originally, we wanted to suppress verbose display when running on Travis,
# but we no longer want that functionality. Just use the following if to
# get the suppression functionality again:
#
#if (os.environ.get('TRAVIS', '0') != 'true') and
#   (os.environ.get('CI', '0') != 'true'):
#
#    env['PRINT_CMD_LINE_FUNC'] = print_cmd_line

env['PRINT_CMD_LINE_FUNC'] = print_cmd_line


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
    'src/leveldb',
    'src/leveldb/port',
    'src/leveldb/include',
    'src/snappy/snappy',
    'src/snappy/config',
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
    'src/snappy/snappy.unity.cpp',
    'src/ripple/http/ripple_http.cpp',
    'src/ripple/json/ripple_json.cpp',
    'src/ripple/peerfinder/ripple_peerfinder.cpp',
    'src/ripple/radmap/ripple_radmap.cpp',
    'src/ripple/resource/ripple_resource.cpp',
    'src/ripple/rocksdb/ripple_rocksdb.cpp',
    'src/ripple/sitefiles/ripple_sitefiles.cpp',
    'src/ripple/sslutil/ripple_sslutil.cpp',
    'src/ripple/testoverlay/ripple_testoverlay.cpp',
    'src/ripple/types/ripple_types.cpp',
    'src/ripple/validators/ripple_validators.cpp',
    'src/ripple/common/ripple_common.cpp',
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
    'src/ripple_app/ripple_app_pt9.cpp',
    'src/ripple_basics/ripple_basics.cpp',
    'src/ripple_core/ripple_core.cpp',
    'src/ripple_data/ripple_data.cpp',
    'src/ripple_hyperleveldb/ripple_hyperleveldb.cpp',
    'src/ripple_leveldb/ripple_leveldb.cpp',
    'src/ripple_net/ripple_net.cpp',
    'src/ripple_overlay/ripple_overlay.cpp',
    'src/ripple_rpc/ripple_rpc.cpp',
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
        '-rdynamic',
        '-pthread',
        ])

DEBUGFLAGS  = ['-g', '-ggdb', '-DDEBUG', '-D_DEBUG']

env.Append(CCFLAGS = ['-pthread', '-Wall', '-Wno-sign-compare', '-Wno-char-subscripts']+DEBUGFLAGS)
if not USING_CLANG:
     more_warnings = ['-Wno-unused-local-typedefs']
else:
     # This disables the "You said it was a struct AND a class, wth is going on
     # warnings"
     more_warnings = ['-Wno-mismatched-tags'] # add '-Wshorten-64-to-32' some day
     # This needs to be a CCFLAGS not a CXXFLAGS
     env.Append(CCFLAGS = more_warnings)

# add '-Wconversion' some day
env.Append(CXXFLAGS = ['-O0', '-fno-strict-aliasing', '-pthread', '-Wno-invalid-offsetof', '-Wformat']+more_warnings+DEBUGFLAGS)

# RTTI is required for Beast and CountedObject.
#
env.Append(CXXFLAGS = ['-frtti'])

UBUNTU_GCC_48_INSTALL_STEPS = '''
https://ripple.com/wiki/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8'''

if not USING_CLANG:
    if (int(GCC_VERSION[0]) == 4 and int(GCC_VERSION[1]) < 8):
        print "\n\033[91mTo compile rippled using GCC you need version 4.8.1 or later.\033[0m\n"

        if Ubuntu:
          print "For information how to update your GCC, please visit:"
          print UBUNTU_GCC_48_INSTALL_STEPS
          print "\n"

        sys.exit(1)
    else:
        env.Append(CXXFLAGS = ['-std=c++11'])

# FreeBSD doesn't support O_DSYNC
if FreeBSD:
    env.Append(CPPFLAGS = ['-DMDB_DSYNC=O_SYNC'])

if OSX:
    env.Append(LINKFLAGS = ['-L/usr/local/opt/openssl/lib'])
    env.Append(CXXFLAGS = ['-I/usr/local/opt/openssl/include'])

# Determine if this is a Travis continuous integration build:
TravisBuild = (os.environ.get('TRAVIS', '0') == 'true') and \
              (os.environ.get('CI', '0') == 'true')

RippleRepository = False

# Determine if we're building against the main ripple repo or a developer repo
if TravisBuild:
    Slug = os.environ.get('TRAVIS_REPO_SLUG', '')

    if (Slug.find ("ripple/") == 0):
        RippleRepository = True

if TravisBuild:
    env.Append(CFLAGS = ['-DTRAVIS_CI_BUILD'])
    env.Append(CXXFLAGS = ['-DTRAVIS_CI_BUILD'])

if RippleRepository:
    env.Append(CFLAGS = ['-DRIPPLE_MASTER_BUILD'])
    env.Append(CXXFLAGS = ['-DRIPPLE_MASTER_BUILD'])

# Display build configuration information for debugging purposes
def print_nv_pair(n, v):
    name = ("%s" % n.rjust(10))
    sys.stdout.write("%s \033[94m%s\033[0m\n" % (name, v))

def print_build_config(var):
    val = env.get(var, '')
    
    if val and val != '':
        name = ("%s" % var.rjust(10))

        wrapper = textwrap.TextWrapper()
        wrapper.break_long_words = False
        wrapper.break_on_hyphens = False
        wrapper.width = 69

        if type(val) is str:
            lines = wrapper.wrap(val)
        else:
            lines = wrapper.wrap(" ".join(str(x) for x in val))

        for line in lines:
            print_nv_pair (name, line)
            name = "          "

config_vars = ['CC', 'CXX', 'CFLAGS', 'CPPFLAGS', 'CXXFLAGS', 'LINKFLAGS', 'LIBS']

if TravisBuild:
    Slug = os.environ.get('TRAVIS_REPO_SLUG', None)
    Branch = os.environ.get('TRAVIS_BRANCH', None)
    Commit = os.environ.get('TRAVIS_COMMIT', None)

    sys.stdout.write("\nBuild Type:\n")

    if (Slug.find ("ripple/") == 0):
        print_nv_pair ("Build", "Travis - Ripple Master Repository")
    else:
        print_nv_pair ("Build", "Travis - Ripple Developer Fork")

    if (Slug):
        print_nv_pair ("Repo", Slug)
        
    if (Branch):
        print_nv_pair ("Branch", Branch)

    if (Commit):
        print_nv_pair ("Commit", Commit)

sys.stdout.write("\nConfiguration:\n")

for var in config_vars:
    print_build_config(var)

sys.stdout.write("\nBuilding:\n")

PROTO_SRCS = env.Protoc([], 'src/ripple/proto/ripple.proto',
    PROTOCOUTDIR='src/ripple/proto',
    PROTOCPROTOPATH=['src/ripple/proto'],
    PROTOCPYTHONOUTDIR=None)

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

#-------------------------------------------------------------------------------

# Returns the list of libraries needed by the test source file. This is
# accomplished by scanning the source file for a special comment line
# with this format, which must match exactly:
#
# // LIBS: <name>...
#
# path = path to source file
#
def get_libs(path):
    prefix = '// LIBS:'
    with open(path, 'rb') as f:
        for line in f:
            line = line.strip()
            if line.startswith(prefix):
                items = line.split(prefix, 1)[1].strip()
                return [x.strip() for x in items.split(' ')]

# Returns the list of source modules needed by the test source file. This
#
# // MODULES: <module>...
#
# path = path to source file
#
def get_mods(path):
    prefix = '// MODULES:'
    with open(path, 'rb') as f:
        for line in f:
            line = line.strip()
            if line.startswith(prefix):
                items = line.split(prefix, 1)[1].strip()
                items = [os.path.normpath(os.path.join(
                    os.path.dirname(path), x.strip())) for
                        x in items.split(' ')]
                return items

# Build a stand alone executable that runs
# all the test suites in one source file
#
def build_test(env,path):
    libs = get_libs(path)
    mods = get_mods(path)
    bin = os.path.basename(os.path.splitext(path)[0])
    bin = os.path.join ("build", bin)
    srcs = ['src/beast/beast/unit_test/tests/main.cpp']
    srcs.append (path)
    if mods:
        srcs.extend (mods)
    # All paths get normalized here, so we can use posix
    # forward slashes for everything including on Windows
    srcs = [os.path.normpath(os.path.join ('build', x)) for x in srcs]
    objs = [os.path.splitext(x)[0]+'.o' for x in srcs]
    env_ = env
    if libs:
        env_.Append(LIBS = libs)
    env_.Program (bin, srcs)

#-------------------------------------------------------------------------------

def main():
    env = Environment()

    env['PRINT_CMD_LINE_FUNC'] = print_cmd_line

    env.VariantDir (os.path.join ('build', 'src'), 'src', duplicate=0)

    # Copy important os environment variables into env
    if os.environ.get ('CC', None):
        env.Replace (CC = os.environ['CC'])
    if os.environ.get ('CXX', None):
        env.Replace (CXX = os.environ['CXX'])
    if os.environ.get ('PATH', None):
        env.Replace (PATH = os.environ['PATH'])

    # Set up boost variables
    home = os.environ.get("BOOST_HOME", None)
    if home is not None:
        env.Prepend (CPPPATH = home)
        env.Append (LIBPATH = os.path.join (home, 'stage', 'lib'))

    # Set up flags
    env.Append(CXXFLAGS = [
        '-std=c++11',
        '-frtti',
        '-g'
        ])

    for root, dirs, files in os.walk('src/ripple'):
        for path in files:
            path = os.path.join(root,path)
            if (path.endswith(".test.cpp")):
                build_test(env,path)

    print_build_config (env)

main()



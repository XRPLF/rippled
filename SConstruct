# rippled SConstruct
#
'''

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

The following extra options may be used:
    --ninja         Generate a `build.ninja` build file for the specified target
                    (see: https://martine.github.io/ninja/). Only gcc and clang targets
                     are supported.

'''
#
'''

TODO

- Fix git-describe support
- Fix printing exemplar command lines
- Fix toolchain detection


'''
#-------------------------------------------------------------------------------

import collections
import os
import subprocess
import sys
import textwrap
import time
import SCons.Action

sys.path.append(os.path.join('src', 'beast', 'site_scons'))
sys.path.append(os.path.join('src', 'ripple', 'site_scons'))

import Beast
import scons_to_ninja

#------------------------------------------------------------------------------

AddOption('--ninja', dest='ninja', action='store_true',
          help='generate ninja build file build.ninja')

def parse_time(t):
    return time.strptime(t, '%a %b %d %H:%M:%S %Z %Y')

CHECK_PLATFORMS = 'Debian', 'Ubuntu'
CHECK_COMMAND = 'openssl version -a'
CHECK_LINE = 'built on: '
BUILD_TIME = 'Mon Apr  7 20:33:19 UTC 2014'
OPENSSL_ERROR = ('Your openSSL was built on %s; '
                 'rippled needs a version built on or after %s.')
UNITY_BUILD_DIRECTORY = 'src/ripple/unity/'

def check_openssl():
    if Beast.system.platform in CHECK_PLATFORMS:
        for line in subprocess.check_output(CHECK_COMMAND.split()).splitlines():
            if line.startswith(CHECK_LINE):
                line = line[len(CHECK_LINE):]
                if parse_time(line) < parse_time(BUILD_TIME):
                    raise Exception(OPENSSL_ERROR % (line, BUILD_TIME))
                else:
                    break
        else:
            raise Exception("Didn't find any '%s' line in '$ %s'" %
                            (CHECK_LINE, CHECK_COMMAND))


def set_implicit_cache():
    '''Use implicit_cache on some targets to improve build times.

    By default, scons scans each file for include dependecies. The implicit
    cache flag lets you cache these dependencies for later builds, and will
    only rescan files that change.

    Failure cases are:
    1) If the include search paths are changed (i.e. CPPPATH), then a file
       may not be rebuilt.
    2) If a same-named file has been added to a directory that is earlier in
       the search path than the directory in which the file was found.
    Turn on if this build is for a specific debug target (i.e. clang.debug)

    If one of the failure cases applies, you can force a rescan of dependencies
    using the command line option `--implicit-deps-changed`
    '''
    if len(COMMAND_LINE_TARGETS) == 1:
        s = COMMAND_LINE_TARGETS[0].split('.')
        if len(s) > 1 and 'debug' in s:
            SetOption('implicit_cache', 1)


def import_environ(env):
    '''Imports environment settings into the construction environment'''
    def set(keys):
        if type(keys) == list:
            for key in keys:
                set(key)
            return
        if keys in os.environ:
            value = os.environ[keys]
            env[keys] = value
    set(['GNU_CC', 'GNU_CXX', 'GNU_LINK'])
    set(['CLANG_CC', 'CLANG_CXX', 'CLANG_LINK'])

def detect_toolchains(env):
    def is_compiler(comp_from, comp_to):
        return comp_from and comp_to in comp_from

    def detect_clang(env):
        n = sum(x in env for x in ['CLANG_CC', 'CLANG_CXX', 'CLANG_LINK'])
        if n > 0:
            if n == 3:
                return True
            raise ValueError('CLANG_CC, CLANG_CXX, and CLANG_LINK must be set together')
        cc = env.get('CC')
        cxx = env.get('CXX')
        link = env.subst(env.get('LINK'))
        if (cc and cxx and link and
            is_compiler(cc, 'clang') and
            is_compiler(cxx, 'clang') and
            is_compiler(link, 'clang')):
            env['CLANG_CC'] = cc
            env['CLANG_CXX'] = cxx
            env['CLANG_LINK'] = link
            return True
        cc = env.WhereIs('clang')
        cxx = env.WhereIs('clang++')
        link = cxx
        if (is_compiler(cc, 'clang') and
            is_compiler(cxx, 'clang') and
            is_compiler(link, 'clang')):
           env['CLANG_CC'] = cc
           env['CLANG_CXX'] = cxx
           env['CLANG_LINK'] = link
           return True
        env['CLANG_CC'] = 'clang'
        env['CLANG_CXX'] = 'clang++'
        env['CLANG_LINK'] = env['LINK']
        return False

    def detect_gcc(env):
        n = sum(x in env for x in ['GNU_CC', 'GNU_CXX', 'GNU_LINK'])
        if n > 0:
            if n == 3:
                return True
            raise ValueError('GNU_CC, GNU_CXX, and GNU_LINK must be set together')
        cc = env.get('CC')
        cxx = env.get('CXX')
        link = env.subst(env.get('LINK'))
        if (cc and cxx and link and
            is_compiler(cc, 'gcc') and
            is_compiler(cxx, 'g++') and
            is_compiler(link, 'g++')):
            env['GNU_CC'] = cc
            env['GNU_CXX'] = cxx
            env['GNU_LINK'] = link
            return True
        cc = env.WhereIs('gcc')
        cxx = env.WhereIs('g++')
        link = cxx
        if (is_compiler(cc, 'gcc') and
            is_compiler(cxx, 'g++') and
            is_compiler(link, 'g++')):
           env['GNU_CC'] = cc
           env['GNU_CXX'] = cxx
           env['GNU_LINK'] = link
           return True
        env['GNU_CC'] = 'gcc'
        env['GNU_CXX'] = 'g++'
        env['GNU_LINK'] = env['LINK']
        return False

    toolchains = []
    if detect_clang(env):
        toolchains.append('clang')
    if detect_gcc(env):
        toolchains.append('gcc')
    if env.Detect('cl'):
        toolchains.append('msvc')
    return toolchains

def files(base):
    def _iter(base):
        for parent, _, files in os.walk(base):
            for path in files:
                path = os.path.join(parent, path)
                yield os.path.normpath(path)
    return list(_iter(base))

def print_coms(target, source, env):
    '''Display command line exemplars for an environment'''
    print ('Target: ' + Beast.yellow(str(target[0])))
    # TODO Add 'PROTOCCOM' to this list and make it work
    Beast.print_coms(['CXXCOM', 'CCCOM', 'LINKCOM'], env)

#-------------------------------------------------------------------------------

# Set construction variables for the base environment
def config_base(env):
    if False:
        env.Replace(
            CCCOMSTR='Compiling ' + Beast.blue('$SOURCES'),
            CXXCOMSTR='Compiling ' + Beast.blue('$SOURCES'),
            LINKCOMSTR='Linking ' + Beast.blue('$TARGET'),
            )
    check_openssl()

    env.Append(CPPDEFINES=[
        'OPENSSL_NO_SSL2'
        ,'DEPRECATED_IN_MAC_OS_X_VERSION_10_7_AND_LATER'
        ,{'HAVE_USLEEP' : '1'}
        ,{'SOCI_CXX_C11' : '1'}
        ,'_SILENCE_STDEXT_HASH_DEPRECATION_WARNINGS'
        ])

    try:
        BOOST_ROOT = os.path.normpath(os.environ['BOOST_ROOT'])
        env.Append(CPPPATH=[
            BOOST_ROOT,
            ])
        env.Append(LIBPATH=[
            os.path.join(BOOST_ROOT, 'stage', 'lib'),
            ])
        env['BOOST_ROOT'] = BOOST_ROOT
    except KeyError:
        pass

    if Beast.system.windows:
        try:
            OPENSSL_ROOT = os.path.normpath(os.environ['OPENSSL_ROOT'])
            env.Append(CPPPATH=[
                os.path.join(OPENSSL_ROOT, 'include'),
                ])
            env.Append(LIBPATH=[
                os.path.join(OPENSSL_ROOT, 'lib'),
                ])
        except KeyError:
            pass
    elif Beast.system.osx:
        OSX_OPENSSL_ROOT = '/usr/local/Cellar/openssl/'
        most_recent = sorted(os.listdir(OSX_OPENSSL_ROOT))[-1]
        openssl = os.path.join(OSX_OPENSSL_ROOT, most_recent)
        env.Prepend(CPPPATH='%s/include' % openssl)
        env.Prepend(LIBPATH=['%s/lib' % openssl])

    # handle command-line arguments
    profile_jemalloc = ARGUMENTS.get('profile-jemalloc')
    if profile_jemalloc:
        env.Append(CPPDEFINES={'PROFILE_JEMALLOC' : profile_jemalloc})
        env.Append(LIBS=['jemalloc'])
        env.Append(LIBPATH=[os.path.join(profile_jemalloc, 'lib')])
        env.Append(CPPPATH=[os.path.join(profile_jemalloc, 'include')])
        env.Append(LINKFLAGS=['-Wl,-rpath,' + os.path.join(profile_jemalloc, 'lib')])

# Set toolchain and variant specific construction variables
def config_env(toolchain, variant, env):
    if variant == 'debug':
        env.Append(CPPDEFINES=['DEBUG', '_DEBUG'])

    elif variant == 'release' or variant == 'profile':
        env.Append(CPPDEFINES=['NDEBUG'])

    if toolchain in Split('clang gcc'):
        if Beast.system.linux:
            env.ParseConfig('pkg-config --static --cflags --libs openssl')
            env.ParseConfig('pkg-config --static --cflags --libs protobuf')

        env.Prepend(CFLAGS=['-Wall'])
        env.Prepend(CXXFLAGS=['-Wall'])

        env.Append(CCFLAGS=[
            '-Wno-sign-compare',
            '-Wno-char-subscripts',
            '-Wno-format',
            '-g'                        # generate debug symbols
            ])

        env.Append(LINKFLAGS=[
            '-rdynamic',
            '-g',
            ])

        if variant == 'profile':
            env.Append(CCFLAGS=[
                '-p',
                '-pg',
                ])
            env.Append(LINKFLAGS=[
                '-p',
                '-pg',
                ])

        if toolchain == 'clang':
            env.Append(CCFLAGS=['-Wno-redeclared-class-member'])
            env.Append(CPPDEFINES=['BOOST_ASIO_HAS_STD_ARRAY'])

        env.Append(CXXFLAGS=[
            '-frtti',
            '-std=c++11',
            '-Wno-invalid-offsetof'])

        env.Append(CPPDEFINES=['_FILE_OFFSET_BITS=64'])

        if Beast.system.osx:
            env.Append(CPPDEFINES={
                'BEAST_COMPILE_OBJECTIVE_CPP': 1,
                })

        # These should be the same regardless of platform...
        if Beast.system.osx:
            env.Append(CCFLAGS=[
                '-Wno-deprecated',
                '-Wno-deprecated-declarations',
                '-Wno-unused-variable',
                '-Wno-unused-function',
                ])
        else:
            if toolchain == 'gcc':
                env.Append(CCFLAGS=[
                    '-Wno-unused-but-set-variable'
                    ])

        boost_libs = [
            'boost_coroutine',
            'boost_context',
            'boost_date_time',
            'boost_filesystem',
            'boost_program_options',
            'boost_regex',
            'boost_system',
            'boost_thread'
        ]
        # We prefer static libraries for boost
        if env.get('BOOST_ROOT'):
            static_libs = ['%s/stage/lib/lib%s.a' % (env['BOOST_ROOT'], l) for
                           l in boost_libs]
            if all(os.path.exists(f) for f in static_libs):
                boost_libs = [File(f) for f in static_libs]

        env.Append(LIBS=boost_libs)
        env.Append(LIBS=['dl'])

        if Beast.system.osx:
            env.Append(LIBS=[
                'crypto',
                'protobuf',
                'ssl',
                ])
            env.Append(FRAMEWORKS=[
                'AppKit',
                'Foundation'
                ])
        else:
            env.Append(LIBS=['rt'])

        if variant == 'release':
            env.Append(CCFLAGS=[
                '-O3',
                '-fno-strict-aliasing'
                ])

        if toolchain == 'clang':
            if Beast.system.osx:
                env.Replace(CC='clang', CXX='clang++', LINK='clang++')
            elif 'CLANG_CC' in env and 'CLANG_CXX' in env and 'CLANG_LINK' in env:
                env.Replace(CC=env['CLANG_CC'],
                            CXX=env['CLANG_CXX'],
                            LINK=env['CLANG_LINK'])
            # C and C++
            # Add '-Wshorten-64-to-32'
            env.Append(CCFLAGS=[])
            # C++ only
            env.Append(CXXFLAGS=[
                '-Wno-mismatched-tags',
                '-Wno-deprecated-register',
                ])

        elif toolchain == 'gcc':
            if 'GNU_CC' in env and 'GNU_CXX' in env and 'GNU_LINK' in env:
                env.Replace(CC=env['GNU_CC'],
                            CXX=env['GNU_CXX'],
                            LINK=env['GNU_LINK'])
            # Why is this only for gcc?!
            env.Append(CCFLAGS=['-Wno-unused-local-typedefs'])

            # If we are in debug mode, use GCC-specific functionality to add
            # extra error checking into the code (e.g. std::vector will throw
            # for out-of-bounds conditions)
            if variant == 'debug':
                env.Append(CPPDEFINES={
                    '_FORTIFY_SOURCE': 2
                    })
                env.Append(CCFLAGS=[
                    '-O0'
                    ])

    elif toolchain == 'msvc':
        env.Append (CPPPATH=[
            os.path.join('src', 'protobuf', 'src'),
            os.path.join('src', 'protobuf', 'vsprojects'),
            ])
        env.Append(CCFLAGS=[
            '/bigobj',              # Increase object file max size
            '/EHa',                 # ExceptionHandling all
            '/fp:precise',          # Floating point behavior
            '/Gd',                  # __cdecl calling convention
            '/Gm-',                 # Minimal rebuild: disabled
            '/GR',                  # Enable RTTI
            '/Gy-',                 # Function level linking: disabled
            '/FS',
            '/MP',                  # Multiprocessor compilation
            '/openmp-',             # pragma omp: disabled
            '/Zc:forScope',         # Language extension: for scope
            '/Zi',                  # Generate complete debug info
            '/errorReport:none',    # No error reporting to Internet
            '/nologo',              # Suppress login banner
            #'/Fd${TARGET}.pdb',     # Path: Program Database (.pdb)
            '/W3',                  # Warning level 3
            '/WX-',                 # Disable warnings as errors
            '/wd"4018"',
            '/wd"4244"',
            '/wd"4267"',
            '/wd"4800"',            # Disable C4800 (int to bool performance)
            ])
        env.Append(CPPDEFINES={
            '_WIN32_WINNT' : '0x6000',
            })
        env.Append(CPPDEFINES=[
            '_SCL_SECURE_NO_WARNINGS',
            '_CRT_SECURE_NO_WARNINGS',
            'WIN32_CONSOLE',
            ])
        env.Append(LIBS=[
            'ssleay32.lib',
            'libeay32.lib',
            'Shlwapi.lib',
            'kernel32.lib',
            'user32.lib',
            'gdi32.lib',
            'winspool.lib',
            'comdlg32.lib',
            'advapi32.lib',
            'shell32.lib',
            'ole32.lib',
            'oleaut32.lib',
            'uuid.lib',
            'odbc32.lib',
            'odbccp32.lib',
            ])
        env.Append(LINKFLAGS=[
            '/DEBUG',
            '/DYNAMICBASE',
            '/ERRORREPORT:NONE',
            #'/INCREMENTAL',
            '/MACHINE:X64',
            '/MANIFEST',
            #'''/MANIFESTUAC:"level='asInvoker' uiAccess='false'"''',
            '/nologo',
            '/NXCOMPAT',
            '/SUBSYSTEM:CONSOLE',
            '/TLBID:1',
            ])

        if variant == 'debug':
            env.Append(CCFLAGS=[
                '/GS',              # Buffers security check: enable
                '/MTd',             # Language: Multi-threaded Debug CRT
                '/Od',              # Optimization: Disabled
                '/RTC1',            # Run-time error checks:
                ])
            env.Append(CPPDEFINES=[
                '_CRTDBG_MAP_ALLOC'
                ])
        else:
            env.Append(CCFLAGS=[
                '/MT',              # Language: Multi-threaded CRT
                '/Ox',              # Optimization: Full
                ])

    else:
        raise SCons.UserError('Unknown toolchain == "%s"' % toolchain)

#-------------------------------------------------------------------------------

# Configure the base construction environment
root_dir = Dir('#').srcnode().get_abspath() # Path to this SConstruct file
build_dir = os.path.join('build')

base = Environment(
    toolpath=[os.path.join ('src', 'beast', 'site_scons', 'site_tools')],
    tools=['default', 'Protoc', 'VSProject'],
    ENV=os.environ,
    TARGET_ARCH='x86_64')
import_environ(base)
config_base(base)
base.Append(CPPPATH=[
    'src',
    os.path.join('src', 'beast'),
    os.path.join(build_dir, 'proto'),
    os.path.join('src','soci','src'),
    os.path.join('src','soci','include'),
    ])

base.Decider('MD5-timestamp')
set_implicit_cache()

# Configure the toolchains, variants, default toolchain, and default target
variants = ['debug', 'release', 'profile']
all_toolchains = ['clang', 'gcc', 'msvc']
if Beast.system.osx:
    toolchains = ['clang']
    default_toolchain = 'clang'
else:
    toolchains = detect_toolchains(base)
    if not toolchains:
        raise ValueError('No toolchains detected!')
    if 'msvc' in toolchains:
        default_toolchain = 'msvc'
    elif 'gcc' in toolchains:
        if 'clang' in toolchains:
            cxx = os.environ.get('CXX', 'g++')
            default_toolchain = 'clang' if 'clang' in cxx else 'gcc'
        else:
            default_toolchain = 'gcc'
    elif 'clang' in toolchains:
        default_toolchain = 'clang'
    else:
        raise ValueError("Don't understand toolchains in " + str(toolchains))

default_tu_style = 'unity'
default_variant = 'release'
default_target = None

for source in [
    'src/ripple/proto/ripple.proto',
    ]:
    base.Protoc([],
        source,
        PROTOCPROTOPATH=[os.path.dirname(source)],
        PROTOCOUTDIR=os.path.join(build_dir, 'proto'),
        PROTOCPYTHONOUTDIR=None)

#-------------------------------------------------------------------------------

class ObjectBuilder(object):
    def __init__(self, env, variant_dirs):
        self.env = env
        self.variant_dirs = variant_dirs
        self.objects = []
        self.child_envs = []

    def add_source_files(self, *filenames, **kwds):
        for filename in filenames:
            env = self.env
            if kwds:
                env = env.Clone()
                env.Prepend(**kwds)
                self.child_envs.append(env)
            o = env.Object(Beast.variantFile(filename, self.variant_dirs))
            self.objects.append(o)

def list_sources(base, suffixes):
    def _iter(base):
        for parent, dirs, files in os.walk(base):
            files = [f for f in files if not f[0] == '.']
            dirs[:] = [d for d in dirs if not d[0] == '.']
            for path in files:
                path = os.path.join(parent, path)
                r = os.path.splitext(path)
                if r[1] and r[1] in suffixes:
                    yield os.path.normpath(path)
    return list(_iter(base))


def append_sources(result, *filenames, **kwds):
    result.append([filenames, kwds])


def get_soci_sources(style):
    result = []
    cpp_path = [
        'src/soci/src/core',
        'src/soci/include/private',
        'src/sqlite', ]
    append_sources(result,
                   'src/ripple/unity/soci.cpp',
                   CPPPATH=cpp_path)
    if style == 'unity':
        append_sources(result,
                       'src/ripple/unity/soci_ripple.cpp',
                       CPPPATH=cpp_path)
    return result

def get_common_sources(toolchain):
    result = []
    if toolchain == 'msvc':
        warning_flags = {}
    else:
        warning_flags = {'CCFLAGS': ['-Wno-unused-function']}
    append_sources(
        result,
        'src/ripple/unity/secp256k1.cpp',
        CPPPATH=['src/secp256k1'],
        **warning_flags)
    return result

def get_classic_sources(toolchain):
    result = []
    append_sources(
        result,
        *list_sources('src/ripple/core', '.cpp'),
        CPPPATH=[
            'src/soci/src/core',
            'src/sqlite']
    )
    append_sources(result, *list_sources('src/ripple/app', '.cpp'))
    append_sources(result, *list_sources('src/ripple/basics', '.cpp'))
    append_sources(result, *list_sources('src/ripple/crypto', '.cpp'))
    append_sources(result, *list_sources('src/ripple/json', '.cpp'))
    append_sources(result, *list_sources('src/ripple/ledger', '.cpp'))
    append_sources(result, *list_sources('src/ripple/legacy', '.cpp'))
    append_sources(result, *list_sources('src/ripple/net', '.cpp'))
    append_sources(result, *list_sources('src/ripple/overlay', '.cpp'))
    append_sources(result, *list_sources('src/ripple/peerfinder', '.cpp'))
    append_sources(result, *list_sources('src/ripple/protocol', '.cpp'))
    append_sources(result, *list_sources('src/ripple/rpc', '.cpp'))
    append_sources(result, *list_sources('src/ripple/shamap', '.cpp'))
    append_sources(result, *list_sources('src/ripple/test', '.cpp'))
    append_sources(result, *list_sources('src/ripple/unl', '.cpp'))

    append_sources(
        result,
        *list_sources('src/ripple/nodestore', '.cpp'),
        CPPPATH=[
            'src/rocksdb2/include',
            'src/snappy/snappy',
            'src/snappy/config',
        ])

    result += get_soci_sources('classic')
    result += get_common_sources(toolchain)

    return result

def get_unity_sources(toolchain):
    result = []
    append_sources(
        result,
        'src/ripple/unity/app_ledger.cpp',
        'src/ripple/unity/app_main.cpp',
        'src/ripple/unity/app_misc.cpp',
        'src/ripple/unity/app_paths.cpp',
        'src/ripple/unity/app_tests.cpp',
        'src/ripple/unity/app_tx.cpp',
        'src/ripple/unity/core.cpp',
        'src/ripple/unity/basics.cpp',
        'src/ripple/unity/crypto.cpp',
        'src/ripple/unity/ledger.cpp',
        'src/ripple/unity/net.cpp',
        'src/ripple/unity/overlay.cpp',
        'src/ripple/unity/peerfinder.cpp',
        'src/ripple/unity/json.cpp',
        'src/ripple/unity/protocol.cpp',
        'src/ripple/unity/rpcx.cpp',
        'src/ripple/unity/shamap.cpp',
        'src/ripple/unity/test.cpp',
        'src/ripple/unity/unl.cpp',
    )

    append_sources(
        result,
        'src/ripple/unity/nodestore.cpp',
        CPPPATH=[
            'src/rocksdb2/include',
            'src/snappy/snappy',
            'src/snappy/config',
        ])

    result += get_soci_sources('unity')
    result += get_common_sources(toolchain)

    return result

# Declare the targets
aliases = collections.defaultdict(list)
msvc_configs = []


def should_prepare_target(cl_target,
                          style, toolchain, variant):
    if not cl_target:
        # default target
        return (style == default_tu_style and
                toolchain == default_toolchain and
                variant == default_variant)
    if 'vcxproj' in cl_target:
        return toolchain == 'msvc'
    s = cl_target.split('.')
    if style == 'unity' and 'nounity' in s:
        return False
    if len(s) == 1:
        return ('all' in cl_target or
                variant in cl_target or
                toolchain in cl_target)
    if len(s) == 2 or len(s) == 3:
        return s[0] == toolchain and s[1] == variant

    return True  # A target we don't know about, better prepare to build it


def should_prepare_targets(style, toolchain, variant):
    if not COMMAND_LINE_TARGETS:
        return should_prepare_target(None, style, toolchain, variant)
    for t in COMMAND_LINE_TARGETS:
        if should_prepare_target(t, style, toolchain, variant):
            return True

def should_build_ninja(style, toolchain, variant):
    """
    Return True if a ninja build file should be generated.

    Typically, scons will be called as follows to generate a ninja build file:
    `scons ninja=1 gcc.debug` where `gcc.debug` may be replaced with any of our
    non-visual studio targets. Raise an exception if we cannot generate the
    requested ninja build file (for example, if multiple targets are requested).
    """
    if not GetOption('ninja'):
        return False
    if len(COMMAND_LINE_TARGETS) != 1:
        raise Exception('Can only generate a ninja file for a single target')
    cl_target = COMMAND_LINE_TARGETS[0]
    if 'vcxproj' in cl_target:
        raise Exception('Cannot generate a ninja file for a vcxproj')
    s = cl_target.split('.')
    if ( style == 'unity' and 'nounity' in s or
         style == 'classic' and 'nounity' not in s or
         len(s) == 1 ):
        return False
    if len(s) == 2 or len(s) == 3:
        return s[0] == toolchain and s[1] == variant
    return False

for tu_style in ['classic', 'unity']:
    for toolchain in all_toolchains:
        for variant in variants:
            if not should_prepare_targets(tu_style, toolchain, variant):
                continue
            if variant == 'profile' and toolchain == 'msvc':
                continue
            # Configure this variant's construction environment
            env = base.Clone()
            config_env(toolchain, variant, env)
            variant_name = '%s.%s' % (toolchain, variant)
            if tu_style == 'classic':
                variant_name += '.nounity'
            variant_dir = os.path.join(build_dir, variant_name)
            variant_dirs = {
                os.path.join(variant_dir, 'src') :
                    'src',
                os.path.join(variant_dir, 'proto') :
                    os.path.join (build_dir, 'proto'),
                }
            for dest, source in variant_dirs.iteritems():
                env.VariantDir(dest, source, duplicate=0)

            object_builder = ObjectBuilder(env, variant_dirs)

            if tu_style == 'classic':
                sources = get_classic_sources(toolchain)
            else:
                sources = get_unity_sources(toolchain)
            for s, k in sources:
                object_builder.add_source_files(*s, **k)

            git_commit_tag = {}
            if toolchain != 'msvc':
                git = Beast.Git(env)
                if git.exists:
                    id = '%s+%s.%s' % (git.tags, git.user, git.branch)
                    git_commit_tag = {'CPPDEFINES':
                                      {'GIT_COMMIT_ID' : '\'"%s"\'' % id }}

            object_builder.add_source_files(
                'src/ripple/unity/git_id.cpp',
                **git_commit_tag)

            object_builder.add_source_files(
                'src/beast/beast/unity/hash_unity.cpp',
                'src/ripple/unity/beast.cpp',
                'src/ripple/unity/lz4.c',
                'src/ripple/unity/protobuf.cpp',
                'src/ripple/unity/ripple.proto.cpp',
                'src/ripple/unity/resource.cpp',
                'src/ripple/unity/server.cpp',
                'src/ripple/unity/websocket02.cpp'
            )

            object_builder.add_source_files(
                'src/ripple/unity/beastc.c',
                CCFLAGS = ([] if toolchain == 'msvc' else ['-Wno-array-bounds']))

            if 'gcc' in toolchain:
                no_uninitialized_warning = {'CCFLAGS': ['-Wno-maybe-uninitialized']}
            else:
                no_uninitialized_warning = {}

            object_builder.add_source_files(
                'src/ripple/unity/ed25519.c',
                CPPPATH=[
                    'src/ed25519-donna',
                ]
            )

            object_builder.add_source_files(
                'src/ripple/unity/rocksdb.cpp',
                CPPPATH=[
                    'src/rocksdb2',
                    'src/rocksdb2/include',
                    'src/snappy/snappy',
                    'src/snappy/config',
                ],
                **no_uninitialized_warning
            )

            object_builder.add_source_files(
                'src/ripple/unity/snappy.cpp',
                CCFLAGS=([] if toolchain == 'msvc' else ['-Wno-unused-function']),
                CPPPATH=[
                    'src/snappy/snappy',
                    'src/snappy/config',
                ]
            )

            object_builder.add_source_files(
                'src/ripple/unity/websocket06.cpp',
                CPPPATH='src/websocketpp',
            )

            if toolchain == "clang" and Beast.system.osx:
                object_builder.add_source_files('src/ripple/unity/beastobjc.mm')

            target = env.Program(
                target=os.path.join(variant_dir, 'rippled'),
                source=object_builder.objects
                )

            if tu_style == default_tu_style:
                if toolchain == default_toolchain and (
                    variant == default_variant):
                    default_target = target
                    install_target = env.Install (build_dir, source=default_target)
                    env.Alias ('install', install_target)
                    env.Default (install_target)
                    aliases['all'].extend(install_target)
                if toolchain == 'msvc':
                    config = env.VSProjectConfig(variant, 'x64', target, env)
                    msvc_configs.append(config)
                if toolchain in toolchains:
                    aliases['all'].extend(target)
                    aliases[toolchain].extend(target)
            elif toolchain == 'msvc':
                config = env.VSProjectConfig(variant + ".classic", 'x64', target, env)
                msvc_configs.append(config)

            if toolchain in toolchains:
                aliases[variant].extend(target)
                env.Alias(variant_name, target)

            # ninja support
            if should_build_ninja(tu_style, toolchain, variant):
                print('Generating ninja: {}:{}:{}'.format(tu_style, toolchain, variant))
                scons_to_ninja.GenerateNinjaFile(
                    [object_builder.env] + object_builder.child_envs,
                    dest_file='build.ninja')

for key, value in aliases.iteritems():
    env.Alias(key, value)

vcxproj = base.VSProject(
    os.path.join('Builds', 'VisualStudio2015', 'RippleD'),
    source = [],
    VSPROJECT_ROOT_DIRS = ['src/beast', 'src', '.'],
    VSPROJECT_CONFIGS = msvc_configs)
base.Alias('vcxproj', vcxproj)

#-------------------------------------------------------------------------------

# Adds a phony target to the environment that always builds
# See: http://www.scons.org/wiki/PhonyTargets
def PhonyTargets(env = None, **kw):
    if not env: env = DefaultEnvironment()
    for target, action in kw.items():
        env.AlwaysBuild(env.Alias(target, [], action))

# Build the list of rippled source files that hold unit tests
def do_count(target, source, env):
    def list_testfiles(base, suffixes):
        def _iter(base):
            for parent, _, files in os.walk(base):
                for path in files:
                    path = os.path.join(parent, path)
                    r = os.path.splitext(path)
                    if r[1] in suffixes:
                        if r[0].endswith('.test'):
                            yield os.path.normpath(path)
        return list(_iter(base))
    testfiles = list_testfiles(os.path.join('src', 'ripple'), env.get('CPPSUFFIXES'))
    lines = 0
    for f in testfiles:
        lines = lines + sum(1 for line in open(f))
    print "Total unit test lines: %d" % lines

PhonyTargets(env, count = do_count)

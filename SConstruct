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

    clang           All clang variants
    clang.debug     clang debug variant
    clang.release   clang release variant

    gcc             All gcc variants
    gcc.debug       gcc debug variant
    gcc.release     gcc release variant

    msvc            All msvc variants
    msvc.debug      MSVC debug variant
    msvc.release    MSVC release variant

    vcxproj         Generate Visual Studio 2013 project file

If the clang toolchain is detected, then the default target will use it, else
the gcc toolchain will be used. On Windows environments, the MSVC toolchain is
also detected.

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

import Beast

#------------------------------------------------------------------------------

def parse_time(t):
    return time.strptime(t, '%a %b %d %H:%M:%S %Z %Y')

CHECK_PLATFORMS = 'Debian', 'Ubuntu'
CHECK_COMMAND = 'openssl version -a'
CHECK_LINE = 'built on: '
BUILD_TIME = 'Mon Apr  7 20:33:19 UTC 2014'
OPENSSL_ERROR = ('Your openSSL was built on %s; '
                 'rippled needs a version built on or after %s.')

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

    #git = Beast.Git(env) #  TODO(TOM)
    if False: #git.exists:
        env.Append(CPPDEFINES={'GIT_COMMIT_ID' : '"%s"' % git.commit_id})

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
                os.path.join(OPENSSL_ROOT, 'lib', 'VC', 'static'),
                ])
        except KeyError:
            pass
    elif Beast.system.osx:
        OSX_OPENSSL_ROOT = '/usr/local/Cellar/openssl/'
        most_recent = sorted(os.listdir(OSX_OPENSSL_ROOT))[-1]
        openssl = os.path.join(OSX_OPENSSL_ROOT, most_recent)
        env.Prepend(CPPPATH='%s/include' % openssl)
        env.Prepend(LIBPATH=['%s/lib' % openssl])

# Set toolchain and variant specific construction variables
def config_env(toolchain, variant, env):
    if variant == 'debug':
        env.Append(CPPDEFINES=['DEBUG', '_DEBUG'])

    elif variant == 'release':
        env.Append(CPPDEFINES=['NDEBUG'])

    if toolchain in Split('clang gcc'):

        if Beast.system.linux:
            env.ParseConfig('pkg-config --static --cflags --libs openssl')
            env.ParseConfig('pkg-config --static --cflags --libs protobuf')

        env.Append(CCFLAGS=[
            '-Wall',
            '-Wno-sign-compare',
            '-Wno-char-subscripts',
            '-Wno-format',
            ])

        env.Append(CXXFLAGS=[
            '-frtti',
            '-std=c++11',
            '-Wno-invalid-offsetof'])

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
            env.Append(CCFLAGS=[
                '-Wno-unused-but-set-variable'
                ])

        boost_libs = [
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

        env.Append(LINKFLAGS=[
            '-rdynamic'
            ])

        if variant == 'debug':
            env.Append(CCFLAGS=[
                '-g'
                ])
        elif variant == 'release':
            env.Append(CCFLAGS=[
                '-O3',
                '-fno-strict-aliasing'
                ])

        if toolchain == 'clang':
            if Beast.system.osx:
                env.Replace(CC='clang', CXX='clang++', LINK='clang++')
            elif 'CLANG_CC' in env and 'CLANG_CXX' in env and 'CLANG_LINK' in env:
                env.Replace(CC=env['CLANG_CC'], CXX=env['CLANG_CXX'], LINK=env['CLANG_LINK'])
            # C and C++
            # Add '-Wshorten-64-to-32'
            env.Append(CCFLAGS=[])
            # C++ only
            # Why is this only for clang?
            env.Append(CXXFLAGS=['-Wno-mismatched-tags'])

        elif toolchain == 'gcc':
            if 'GNU_CC' in env and 'GNU_CXX' in env and 'GNU_LINK' in env:
                env.Replace(CC=env['GNU_CC'], CXX=env['GNU_CXX'], LINK=env['GNU_LINK'])
            # Why is this only for gcc?!
            env.Append(CCFLAGS=['-Wno-unused-local-typedefs'])

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
            'ssleay32MT.lib',
            'libeay32MT.lib',
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

def addSource(path, env, variant_dirs, CPPPATH=[]):
    if CPPPATH:
        env = env.Clone()
        env.Prepend(CPPPATH=CPPPATH)
    return env.Object(Beast.variantFile(path, variant_dirs))

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
    ])

# Configure the toolchains, variants, default toolchain, and default target
variants = ['debug', 'release']
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
default_variant = 'debug'
default_target = None

for source in [
    'src/ripple/proto/ripple.proto',
    ]:
    base.Protoc([],
        source,
        PROTOCPROTOPATH=[os.path.dirname(source)],
        PROTOCOUTDIR=os.path.join(build_dir, 'proto'),
        PROTOCPYTHONOUTDIR=None)

# Declare the targets
aliases = collections.defaultdict(list)
msvc_configs = []
for toolchain in ['gcc', 'clang', 'msvc']:
    for variant in variants:
        # Configure this variant's construction environment
        env = base.Clone()
        config_env(toolchain, variant, env)
        variant_name = '%s.%s' % (toolchain, variant)
        variant_dir = os.path.join(build_dir, variant_name)
        variant_dirs = {
            os.path.join(variant_dir, 'src') :
                'src',
            os.path.join(variant_dir, 'proto') :
                os.path.join (build_dir, 'proto'),
            }
        for dest, source in variant_dirs.iteritems():
            env.VariantDir(dest, source, duplicate=0)
        objects = []
        objects.append(addSource('src/ripple/unity/app.cpp', env, variant_dirs))
        objects.append(addSource('src/ripple/unity/app1.cpp', env, variant_dirs))
        objects.append(addSource('src/ripple/unity/app2.cpp', env, variant_dirs))
        objects.append(addSource('src/ripple/unity/app3.cpp', env, variant_dirs))
        objects.append(addSource('src/ripple/unity/app4.cpp', env, variant_dirs))
        objects.append(addSource('src/ripple/unity/app5.cpp', env, variant_dirs))
        objects.append(addSource('src/ripple/unity/app6.cpp', env, variant_dirs))
        objects.append(addSource('src/ripple/unity/app7.cpp', env, variant_dirs))
        objects.append(addSource('src/ripple/unity/app8.cpp', env, variant_dirs))
        objects.append(addSource('src/ripple/unity/app9.cpp', env, variant_dirs))
        objects.append(addSource('src/ripple/unity/basics.cpp', env, variant_dirs))
        objects.append(addSource('src/ripple/unity/beast.cpp', env, variant_dirs))
        objects.append(addSource('src/ripple/unity/beastc.c', env, variant_dirs))
        objects.append(addSource('src/ripple/unity/common.cpp', env, variant_dirs))
        objects.append(addSource('src/ripple/unity/core.cpp', env, variant_dirs))
        objects.append(addSource('src/ripple/unity/data.cpp', env, variant_dirs))
        objects.append(addSource('src/ripple/unity/http.cpp', env, variant_dirs))
        objects.append(addSource('src/ripple/unity/json.cpp', env, variant_dirs))
        objects.append(addSource('src/ripple/unity/net.cpp', env, variant_dirs))
        objects.append(addSource('src/ripple/unity/overlay.cpp', env, variant_dirs))
        objects.append(addSource('src/ripple/unity/peerfinder.cpp', env, variant_dirs))
        objects.append(addSource('src/ripple/unity/protobuf.cpp', env, variant_dirs))
        objects.append(addSource('src/ripple/unity/ripple.proto.cpp', env, variant_dirs))
        objects.append(addSource('src/ripple/unity/radmap.cpp', env, variant_dirs))
        objects.append(addSource('src/ripple/unity/resource.cpp', env, variant_dirs))
        objects.append(addSource('src/ripple/unity/rpcx.cpp', env, variant_dirs))
        objects.append(addSource('src/ripple/unity/sitefiles.cpp', env, variant_dirs))
        objects.append(addSource('src/ripple/unity/sslutil.cpp', env, variant_dirs))
        objects.append(addSource('src/ripple/unity/testoverlay.cpp', env, variant_dirs))
        objects.append(addSource('src/ripple/unity/types.cpp', env, variant_dirs))
        objects.append(addSource('src/ripple/unity/validators.cpp', env, variant_dirs))
        objects.append(addSource('src/ripple/unity/websocket.cpp', env, variant_dirs))

        objects.append(addSource('src/ripple/unity/nodestore.cpp', env, variant_dirs, [
            'src/leveldb/include',
            #'src/hyperleveldb/include', # hyper 
            'src/rocksdb/include',
            ]))

        objects.append(addSource('src/ripple/unity/leveldb.cpp', env, variant_dirs, [
            'src/leveldb/',
            'src/leveldb/include',
            'src/snappy/snappy',
            'src/snappy/config',
            ]))

        objects.append(addSource('src/ripple/unity/hyperleveldb.cpp', env, variant_dirs, [
            'src/hyperleveldb',
            'src/snappy/snappy',
            'src/snappy/config',
            ]))

        objects.append(addSource('src/ripple/unity/rocksdb.cpp', env, variant_dirs, [
            'src/rocksdb',
            'src/rocksdb/include',
            'src/snappy/snappy',
            'src/snappy/config',
            ]))

        objects.append(addSource('src/ripple/unity/snappy.cpp', env, variant_dirs, [
            'src/snappy/snappy',
            'src/snappy/config',
            ]))

        if toolchain == "clang" and Beast.system.osx:
            objects.append(addSource('src/ripple/unity/beastobjc.mm', env, variant_dirs))

        target = env.Program(
            target = os.path.join(variant_dir, 'rippled'),
            source = objects
            )

        if toolchain == default_toolchain and variant == default_variant:
            default_target = target
            install_target = env.Install (build_dir, source = default_target)
            env.Alias ('install', install_target)
            env.Default (install_target)
            aliases['all'].extend(install_target)
        if toolchain == 'msvc':
            config = env.VSProjectConfig(variant, 'x64', target, env)
            msvc_configs.append(config)
        if toolchain in toolchains:
            aliases['all'].extend(target)
            aliases[variant].extend(target)
            aliases[toolchain].extend(target)
            env.Alias(variant_name, target)

for key, value in aliases.iteritems():
    env.Alias(key, value)

vcxproj = base.VSProject(
    os.path.join('Builds', 'VisualStudio2013', 'RippleD'),
    source = [],
    VSPROJECT_ROOT_DIRS = ['src/beast', 'src', '.'],
    VSPROJECT_CONFIGS = msvc_configs)
base.Alias('vcxproj', vcxproj)

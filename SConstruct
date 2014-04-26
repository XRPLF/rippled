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
import SCons.Action

sys.path.append(os.path.join('src', 'beast', 'site_scons'))

import Beast

#-------------------------------------------------------------------------------

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

def category(ext):
    if ext in ['.c', '.cc', '.cpp']:
        return 'compiled'
    return 'none'

def unity_category(f):
    base, fullname = os.path.split(f)
    name, ext = os.path.splitext(fullname)
    if os.path.splitext(name)[1] == '.unity':
        return category(ext)
    return 'none'

def categorize(groups, func, sources):
    for f in sources:
        groups.setdefault(func(f), []).append(f)

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
    except KeyError:
        pass

    if Beast.system.linux:
        env.ParseConfig('pkg-config --static --cflags --libs openssl')
        env.ParseConfig('pkg-config --static --cflags --libs protobuf')
    elif Beast.system.windows:
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

        env.Append(LIBS=[
            'boost_date_time',
            'boost_filesystem',
            'boost_program_options',
            'boost_regex',
            'boost_system',
            'boost_thread',
            'dl',
            ])
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
            else:
                env.Replace(CC=env['CLANG_CC'], CXX=env['CLANG_CXX'], LINK=env['CLANG_LINK'])
            # C and C++
            # Add '-Wshorten-64-to-32'
            env.Append(CCFLAGS=[])
            # C++ only
            # Why is this only for clang?
            env.Append(CXXFLAGS=['-Wno-mismatched-tags'])

        elif toolchain == 'gcc':
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
            '/wd"4018"',            # Disable warning C4018
            '/wd"4244"',            # Disable warning C4244
            '/wd"4267"',            # Disable warning 4267
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
            #'/NOLOGO',
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
    os.path.join(build_dir, 'proto'),
    os.path.join('src', 'snappy', 'snappy'),
    os.path.join('src', 'snappy', 'config'),
    ])
base.Append(CPPPATH=[
    os.path.join('src', 'leveldb'),
    os.path.join('src', 'leveldb', 'port'),
    os.path.join('src', 'leveldb', 'include'),
    ])
if Beast.system.windows:
    base.Append(CPPPATH=[
        os.path.join('src', 'protobuf', 'src'),
        ])
else:
    base.Append(CPPPATH=[
        os.path.join('src', 'rocksdb'),
        os.path.join('src', 'rocksdb', 'include'),
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

# Collect sources from recursive directory iteration
groups = collections.defaultdict(list)
categorize(groups, unity_category,
      files('src/ripple')
    + files('src/ripple_app')
    + files('src/ripple_basics')
    + files('src/ripple_core')
    + files('src/ripple_data')
    + files('src/ripple_hyperleveldb')
    + files('src/ripple_leveldb')
    + files('src/ripple_net')
    + files('src/ripple_overlay')
    + files('src/ripple_rpc')
    + files('src/ripple_websocket')
    + files('src/snappy')
    )

groups['protoc'].append (
    os.path.join('src', 'ripple', 'proto', 'ripple.proto'))
for source in groups['protoc']:
    outputs = base.Protoc([],
        source,
        PROTOCPROTOPATH=[os.path.dirname(source)],
        PROTOCOUTDIR=os.path.join(build_dir, 'proto'),
        PROTOCPYTHONOUTDIR=None)
    groups['none'].extend(outputs)
if Beast.system.osx:
    mm = os.path.join('src', 'ripple', 'beast', 'ripple_beastobjc.unity.mm')
    groups['compiled'].append(mm)

# Declare the targets
aliases = collections.defaultdict(list)
msvc_configs = []
for toolchain in toolchains:
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
        objects = [env.Object(x) for x in Beast.variantFiles(
            groups['compiled'], variant_dirs)]
        target = env.Program(
            target = os.path.join(variant_dir, 'rippled'),
            source = objects
            )
        # This causes 'msvc.debug' (e.g.) to show up in the node tree...
        #print_action = env.Command(variant_name, [], Action(print_coms, ''))
        #env.Depends(objects, print_action)
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
    VSPROJECT_ROOT_DIRS = ['.'],
    VSPROJECT_CONFIGS = msvc_configs)
base.Alias('vcxproj', vcxproj)

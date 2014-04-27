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

If the clang toolchain is detected, then the default target will use it, else
the gcc toolchain will be used. On Windows environments, the MSVC toolchain is
also detected.

'''
#
#-------------------------------------------------------------------------------

import collections
import os
import sys
import textwrap
import SCons.Action

sys.path.append(os.path.join('src', 'beast', 'site_scons'))
import Beast

#-------------------------------------------------------------------------------

# Display command line exemplars
def print_coms(target, source, env):
    print ('Target: ' + Beast.yellow(str(target[0])))
    # TODO Add 'PROTOCCOM' to this list and make it work
    Beast.print_coms(['CXXCOM', 'CCCOM', 'LINKCOM'], env)

def detect_toolchains(env):
    toolchains = []
    if env.Detect('clang'):
        toolchains.append('clang')
    if env.Detect('g++'):
        toolchains.append('gcc')
    if env.Detect('cl'):
        toolchains.append('msvc')
    return toolchains

def is_unity(path):
    b, e = os.path.splitext(path)
    return os.path.splitext(b)[1] == '.unity' and e in Split('.c .cpp')

def files(base):
    for parent, _, files in os.walk(base):
        for path in files:
            path = os.path.join(parent, path)
            yield os.path.normpath(path)

#-------------------------------------------------------------------------------

# Set construction variables for the base environment
def config_base(env):
    env.Replace(
        CCCOMSTR='Compiling ' + Beast.blue('$SOURCES'),
        CXXCOMSTR='Compiling ' + Beast.blue('$SOURCES'),
        LINKCOMSTR='Linking ' + Beast.blue('$TARGET'),
        )
    #git = Beast.Git(env)
    if False: #git.exists:
        env.Append(CPPDEFINES={'GIT_COMMIT_ID' : '"%s"' % git.commit_id})
    if Beast.system.linux:
        env.ParseConfig('pkg-config --static --cflags --libs openssl')
        env.ParseConfig('pkg-config --static --cflags --libs protobuf')
    elif Beast.system.windows:
        env.Append(CPPPATH=[os.path.join('src', 'protobuf', 'src')])

# Set toolchain and variant specific construction variables
def config_env(toolchain, variant, env):
    try:
        BOOST_ROOT = os.environ['BOOST_ROOT']
        env.Append(CPPPATH=[BOOST_ROOT])
        env.Append(LIBPATH=[os.path.join(BOOST_ROOT, 'stage', 'lib')])
    except KeyError:
        pass

    try:
        env.Append(CPPPATH=[os.environ['OPENSSL_ROOT']])
    except KeyError:
        pass

    if variant == 'debug':
        env.Append(CPPDEFINES=['DEBUG', '_DEBUG'])

    elif variant == 'release':
        env.Append(CPPDEFINES=['NDEBUG'])

    if toolchain in Split('clang gcc'):
        env.Append(CCFLAGS=[
            '-Wall', '-Wno-sign-compare', '-Wno-char-subscripts',
            '-Wno-format', '-Wno-unused-but-set-variable'])
        env.Append(CXXFLAGS=[
            '-frtti', '-std=c++11', '-Wno-invalid-offsetof'])
        env.Append(CFLAGS=[])
        env.Append(LIBS=[
            'boost_date_time', 'boost_filesystem', 'boost_program_options',
            'boost_regex', 'boost_system', 'boost_thread', 'dl', 'rt'])
        env.Append(LINKFLAGS=['-rdynamic'])

        if variant == 'debug':
            env.Append(CCFLAGS=['-g'])
        elif variant == 'release':
            env.Append(CCFLAGS=['-O3', '-fno-strict-aliasing'])

        if toolchain == 'clang':
            env.Replace(CC='clang', CXX='clang++', LINK='g++')
            # C and C++
            # Add '-Wshorten-64-to-32'
            env.Append(CCFLAGS=[])
            # C++ only
            env.Append(CXXFLAGS=['-Wno-mismatched-tags'])
            if Beast.system.osx:
                env.Append(CXXFLAGS=['-Wno-deprecated-register'])
        elif toolchain == 'gcc':
            env.Replace(CC='gcc', CXX='g++')
            env.Append(CCFLAGS=['-Wno-unused-local-typedefs'])

        if Beast.system.osx:
            env.Append(FRAMEWORKS=['AppKit', 'Foundation'])

    elif toolchain == 'msvc':
        env.Append(CCFLAGS=[
            '/bigobj', '/EHa', '/Fd${TARGET}.pdb', '/GR', '/Gy-',
            '/Zc:wchar_t', '/errorReport:prompt', '/nologo', '/openmp-',
            '/Zi', '/WX-', '/W3', '/wd"4018"', '/wd"4244"', '/wd"4267"'])
        env.Append(CPPDEFINES={'_WIN32_WINNT' : '0x6000'})
        env.Append(CPPDEFINES=[
            '_SCL_SECURE_NO_WARNINGS', '_CRT_SECURE_NO_WARNINGS',
            'WIN32 _CONSOLE'])

        if variant == 'debug':
            env.Append(CCFLAGS=['/GS', '/MTd', '/Od', '/Zi'])
            env.Append(CPPDEFINES=['_CRTDBG_MAP_ALLOC'])
        else:
            env.Append(CCFLAGS=['/MT', '/Ox'])

    else:
        raise SCons.UserError('Unknown toolchain == "%s"' % toolchain)

#-------------------------------------------------------------------------------

# Configure the base construction environment
repo_dir = Dir('#').srcnode().get_abspath() # Path to this SConstruct file
bin_dir = os.path.join('build')
base_env = Environment(
    toolpath=[os.path.join ('src', 'beast', 'site_scons', 'site_tools')],
    tools=['default', 'protoc', 'VSProject'],
    ENV=os.environ)
config_base(base_env)

# Configure the toolchains, variants, default toolchain, and default target
toolchains = detect_toolchains(base_env)
variants = ['debug', 'release']
default_variant = 'debug'
default_target = None

# Collect sources from recursive directory iteration
srcs = filter(is_unity,
    list(files('src/ripple')) +
    list(files('src/ripple_app')) +
    list(files('src/ripple_basics')) +
    list(files('src/ripple_core')) +
    list(files('src/ripple_data')) +
    list(files('src/ripple_hyperleveldb')) +
    list(files('src/ripple_leveldb')) +
    list(files('src/ripple_net')) +
    list(files('src/ripple_overlay')) +
    list(files('src/ripple_rpc')) +
    list(files('src/ripple_websocket'))
    )

# Declare the targets
aliases = collections.defaultdict(list)
for toolchain in toolchains:
    for variant in variants:
        # Configure this variant's construction environment
        env = base_env.Clone()
        config_env(toolchain, variant, env)
        variant_name = '%s.%s' % (toolchain, variant)
        variant_dir = os.path.join(bin_dir, variant_name)
        env.VariantDir(os.path.join(variant_dir, 'src'), 'src', duplicate=0)
        env.Append(CPPPATH=[
            'src/leveldb', 'src/leveldb/port', 'src/leveldb/include'])
        # Build the list of sources
        env.Append(CPPPATH=[variant_dir])
        proto_srcdir = os.path.join('src', 'ripple_data', 'protocol')
        proto_srcs = base_env.Protoc([],
            os.path.join('src', 'ripple_data', 'protocol', 'ripple.proto'),
            PROTOCPROTOPATH=[os.path.join('src', 'ripple_data', 'protocol')],
            PROTOCOUTDIR=os.path.join(variant_dir),
            PROTOCPYTHONOUTDIR=None)
        # Declare the other sources
        var_srcs = [os.path.join(variant_dir, str(f)) for f in srcs] + filter(
            lambda x: os.path.splitext(str(x))[1] == '.cc', proto_srcs)
        # Declare the targets
        target = env.Program(
            target = os.path.join(variant_dir, 'rippled'),
            source = var_srcs)
        print_action = env.Command(variant_name, [], Action(print_coms, ''))
        env.Depends(var_srcs, print_action)
        env.Depends(target, proto_srcs)
        if (env.get('CC', None) == base_env.get('CC', None) and
                variant == default_variant):
            default_target = target
            install_target = env.Install (bin_dir, source = default_target)
            env.Alias ('install', install_target)
            env.Default (install_target)
            aliases['all'].append(install_target)
        aliases['all'].append(target)
        aliases[variant].append(target)
        aliases[toolchain].append(target)
        env.Alias(variant_name, target)
for key, value in aliases.iteritems():
    env.Alias(key, value)

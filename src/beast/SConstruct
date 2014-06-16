from __future__ import absolute_import, division, print_function, unicode_literals

import copy
import itertools
import ntpath
import os
import random
import sys

def add_beast_to_path():
    python_home = os.path.join(os.getcwd(), 'python')
    if python_home not in sys.path:
        sys.path.append(python_home)

add_beast_to_path()

from beast.env.AddCommonFlags import add_common_flags
from beast.env.AddUserEnv import add_user_env
from beast.env import Print
from beast.platform import GetEnvironment
from beast.util import Boost
from beast.util import File
from beast.util import Tests

VARIANT_DIRECTORIES = {
    'beast': ('bin', 'beast'),
    'modules': ('bin', 'modules'),
}

BOOST_LIBRARIES = '' #boost_system'
MAIN_PROGRAM_FILE = 'beast/unit_test/tests/main.cpp'
DOTFILE = '~/.scons'

def main():
    File.validate_libraries(Boost.LIBPATH, BOOST_LIBRARIES)
    defaults = GetEnvironment.get_environment(ARGUMENTS)
    working = copy.deepcopy(defaults)
    add_common_flags(defaults)

    add_user_env(working, DOTFILE)
    add_common_flags(working)
    Print.print_build_config(working, defaults)

    env = Environment(**working)

    for name, path in VARIANT_DIRECTORIES.items():
        env.VariantDir(os.path.join(*path), name, duplicate=0)
    env.Replace(PRINT_CMD_LINE_FUNC=Print.print_cmd_line)
    #Tests.run_tests(env, MAIN_PROGRAM_FILE, '.', '.test.cpp')

#main()

#-------------------------------------------------------------------------------

def is_unity(path):
    b, e = os.path.splitext(path)
    return os.path.splitext(b)[1] == '.unity' and e in ['.c', '.cpp']

def files(base):
    for parent, _, files in os.walk(base):
        for path in files:
            path = os.path.join(parent, path)
            yield os.path.normpath(path)

#-------------------------------------------------------------------------------

'''
/MP /GS /W3 /wd"4018" /wd"4244" /wd"4267" /Gy- /Zc:wchar_t
/I"D:\lib\OpenSSL-Win64\include" /I"D:\lib\boost_1_55_0"
/I"..\..\src\protobuf\src" /I"..\..\src\protobuf\vsprojects"
/I"..\..\src\leveldb" /I"..\..\src\leveldb\include" /I"..\..\build\proto"
/Zi /Gm- /Od /Fd"..\..\build\obj\VisualStudio2013\Debug.x64\vc120.pdb"
/fp:precise /D "_CRTDBG_MAP_ALLOC" /D "WIN32" /D "_DEBUG" /D "_CONSOLE"
/D "_VARIADIC_MAX=10" /D "_WIN32_WINNT=0x0600" /D "_SCL_SECURE_NO_WARNINGS"
/D "_CRT_SECURE_NO_WARNINGS" /D "_MBCS" /errorReport:prompt /WX- /Zc:forScope
/RTC1 /GR /Gd /MTd /openmp- /Fa"..\..\build\obj\VisualStudio2013\Debug.x64\"
/EHa /nologo /Fo"..\..\build\obj\VisualStudio2013\Debug.x64\"
/Fp"..\..\build\obj\VisualStudio2013\Debug.x64\rippled.pch" 
'''

# Path to this SConstruct file
base_dir = Dir('#').srcnode().get_abspath()

base_env = Environment(
    tools = ['default', 'VSProject'],
    CCCOMSTR = '',
    CMDLINE_QUIET = 1,
    CPPPATH = [
        os.environ['BOOST_ROOT'],
        os.environ['OPENSSL_ROOT']
        ],
    CPPDEFINES = [
        '_WIN32_WINNT=0x6000']
        )

#base_env.Replace(PRINT_CMD_LINE_FUNC=Print.print_cmd_line)

env = base_env

bin_dir = os.path.join(base_dir, 'bin')

srcs = filter(is_unity, list(files('beast')) + list(files('modules')))
for variant in ['Debug']: #, 'Release']:
    for platform in ['Win32']:
        #env = base_env.Clone()
        #env.Replace(PRINT_CMD_LINE_FUNC=Print.print_cmd_line)
        variant_dir = os.path.join(bin_dir, variant + '.' + platform)
        env.VariantDir(os.path.join(variant_dir, 'beast'), 'beast', duplicate=0)
        env.VariantDir(os.path.join(variant_dir, 'modules'), 'modules', duplicate=0)
        env.Append(CCFLAGS=[
            '/EHsc',
            '/bigobj',
            '/Fd${TARGET}.pdb'
            ])
        if variant == 'Debug':
            env.Append(CCFLAGS=[
                '/MTd',
                '/Od',
                '/Zi'
                ])
        else:
            env.Append(CCFLAGS=[
                '/MT',
                '/Ox'
                ])
        variant_srcs = [os.path.join(variant_dir, os.path.relpath(f, base_dir)) for f in srcs]

        beast = env.StaticLibrary(
            target = os.path.join(variant_dir, 'beast.lib'),
            source = variant_srcs)

env.VSProject (
    'out',
    buildtarget = beast,
    source = filter(is_unity, list(files('beast')) + list(files('modules'))))

env.Default ('out.vcxproj')
#env.Default (os.path.join(bin_dir,'Debug.Win32', 'beast.lib'))


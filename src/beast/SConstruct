# Beast scons file
#
#-------------------------------------------------------------------------------

import ntpath
import os
import sys
import textwrap
#import re
#import json
#import urlparse
#import posixpath
#import string
#import subprocess
#import platform
#import itertools

#-------------------------------------------------------------------------------

# Format a name value pair
def print_nv_pair(n, v):
    name = ("%s" % n.rjust(10))
    sys.stdout.write("%s \033[94m%s\033[0m\n" % (name, v))

# Pretty-print values as a build configuration
def print_build_vars(env,var):
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

def print_build_config(env):
    config_vars = ['CC', 'CXX', 'CFLAGS', 'CCFLAGS', 'CPPFLAGS',
                   'CXXFLAGS', 'LIBPATH', 'LINKFLAGS', 'LIBS']
    sys.stdout.write("\nConfiguration:\n")
    for var in config_vars:
        print_build_vars(env,var)
    print

def print_cmd_line(s, target, src, env):
    target = (''.join([str(x) for x in target]))
    source = (''.join([str(x) for x in src]))
    name = target
    print '           \033[94m' + name + '\033[0m'

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
    bin = os.path.join ("bin", bin)
    srcs = ['beast/unit_test/tests/main.cpp']
    srcs.append (path)
    if mods:
        srcs.extend (mods)
    # All paths get normalized here, so we can use posix
    # forward slashes for everything including on Windows
    srcs = [os.path.normpath(os.path.join ('bin', x)) for x in srcs]
    objs = [os.path.splitext(x)[0]+'.o' for x in srcs]
    env_ = env
    if libs:
        env_.Append(LIBS = libs)
    env_.Program (bin, srcs)

#-------------------------------------------------------------------------------

def main():
    env = Environment()

    env['PRINT_CMD_LINE_FUNC'] = print_cmd_line

    env.VariantDir (os.path.join ('bin', 'beast'), 'beast', duplicate=0)
    env.VariantDir (os.path.join ('bin', 'modules'), 'modules', duplicate=0)

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

    for root, dirs, files in os.walk('.'):
        for path in files:
            path = os.path.join(root,path)
            if (path.endswith(".test.cpp")):
                build_test(env,path)

    print_build_config (env)

main()


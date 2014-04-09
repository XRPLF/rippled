from __future__ import absolute_import, division, print_function, unicode_literals

import os

from beast.util import File

LIBS_PREFIX = '// LIBS:'
MODS_PREFIX = '// MODULES:'

def build_executable(env, path, main_program_file):
    """Build a stand alone executable that runs
       all the test suites in one source file."""
    libs = File.first_fields_after_prefix(path, LIBS_PREFIX)
    source_modules = File.first_fields_after_prefix(path, MODS_PREFIX)
    source_modules = File.sibling_files(path, source_modules)

    bin = os.path.basename(os.path.splitext(path)[0])
    bin = os.path.join('bin', bin)

    # All paths get normalized here, so we can use posix
    # forward slashes for everything including on Windows
    srcs = File.child_files('bin', [main_program_file, path] + source_modules)
    objs = [File.replace_extension(f, '.o') for f in srcs]
    if libs:
        env.Append(LIBS=libs)  # DANGER: will append the file over and over.
    env.Program(bin, srcs)

def run_tests(env, main_program_file, root, suffix):
    root = os.path.normpath(root)
    for path in File.find_files_with_suffix(root, suffix):
        build_executable(env, path, main_program_file)

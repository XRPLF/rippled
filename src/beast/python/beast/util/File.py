from __future__ import absolute_import, division, print_function, unicode_literals

from beast.util import String

import os

LIBRARY_PATTERNS = 'lib%s.a', 'lib%s.dylib'

def first_fields_after_prefix(filename, prefix):
    with open(filename, 'r') as f:
        return String.first_fields_after_prefix(prefix, f)

def find_files_with_suffix(base, suffix):
    for parent, _, files in os.walk(base):
        for path in files:
            path = os.path.join(parent, path)
            if path.endswith(suffix):
                yield os.path.normpath(path)

def child_files(parent, files):
    return [os.path.normpath(os.path.join(parent, f)) for f in files]

def sibling_files(path, files):
    return child_files(os.path.dirname(path), files)

def replace_extension(file, ext):
    return os.path.splitext(file)[0] + ext

def validate_libraries(path, libraries):
    bad = []
    for lib in libraries:
        found = False
        for pat in LIBRARY_PATTERNS:
            libfile = os.path.join(path, pat % lib)
            if os.path.isfile(libfile):
                found = True
                break
        if not found:
            bad.append(libfile)
    if bad:
        libs = 'library' if len(bad) == 1 else 'libraries'
        raise Exception('Missing %s: %s' % (libs, ', '.join(bad)))

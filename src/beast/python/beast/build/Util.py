# Beast.py
# Copyright 2014 by:
#   Vinnie Falco <vinnie.falco@gmail.com>
#   Tom Ritchford <tom@swirly.com>
#   Nik Bougalis <?>
# This file is part of Beast: http://github.com/vinniefalco/Beast

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

import os
from itertools import takewhile


def commonprefix(paths, sep=os.path.sep):
    """Correct implementation of os.path.commonprefix.
    See http://rosettacode.org/wiki/Find_Common_Directory_Path#Python"""

    bydirectorylevels = zip(*[p.split(sep) for p in paths])
    def allnamesequal(name):
        return all(n == name[0] for n in name[1:])

    return sep.join(x[0] for x in takewhile(allnamesequal, bydirectorylevels))


def variant_file(path, variant_dirs):
    """Returns the path to the corresponding dict entry in variant_dirs."""
    for dest, source in variant_dirs.iteritems():
        common = commonprefix([path, source])
        if common == source:
            return os.path.join(dest, path[len(common) + 1:])
    return path


def iterate_files(root, function, condition=lambda x: True, walk=os.walk):
    """"Call a function for every file contained in root which isn't a dotfile
    or contained in a dot directory."""
    for parent, dirs, files in walk(root):
        files = [f for f in files if not f[0] == '.']
        dirs[:] = [d for d in dirs if not d[0] == '.']
        for path in files:
            path = os.path.join(parent, path)
            if condition(path):
                yield function(path)


def match_suffix(suffixes, path):
    ext = os.path.splitext(path)
    return ext[1] and ext[1] in suffixes

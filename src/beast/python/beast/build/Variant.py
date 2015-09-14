# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

import collections, copy, os

from . Util import iterate_files, match_suffix, variant_file
from . CcWarning import update_warning_kwds

from .. System import SYSTEM

def list_sources(root, suffixes, walk=os.walk):
    condition = lambda path: match_suffix(suffixes, path)
    return iterate_files(root, os.path.normpath, condition, walk)


class Variant(object):
    def __init__(self, state, tags, toolchains, program):
        self.variant_name = '.'.join(tags).replace('.unity', '')
        self.state = state
        self.tags = list(tags) + self.state.tags

        self.toolchains = toolchains
        self.program = program

        self.variant_directory = os.path.join(
            self.state.build_dir, self.variant_name)
        self.toolchain, self.variant = self.tags[:2]
        self.default_toolchain = toolchains.keys()[0]
        if self.toolchain == self.default_toolchain:
            self.tags.append('default_toolchain')

        self.env = state.env.Clone()
        self.env.Replace(**toolchains.get(self.toolchain, {}))
        self.objects = []
        self.files_seen = set()
        self.variant_directory_tree = {
            os.path.join(self.variant_directory, k): v
            for (k, v) in self.state.variant_tree.items()
        }

    def add_module(self, module):
        # Set up environment.
        module.setup(self)

        # Add all the files.
        module.files(self)

        # Now produce all the variant trees.
        for dest, source in self.variant_directory_tree.items():
            self.env.VariantDir(dest, source, duplicate=0)

        # Finally, make the program target.
        self.target = (self.program and self.env.Program(
            target=os.path.join(self.variant_directory, self.program),
            source=self.objects)) or []

        # Now we run the "target" phase.
        module.target(self)
        if self.target and self.toolchain in self.toolchains:
            self.env.Alias(self.variant_name, self.target)

    def add_source_files(self, *filenames, **kwds):
        update_warning_kwds(self.tags, kwds)
        variants = self.variant_directory_tree
        for filename in filenames:
            vfile = variant_file(filename, variants)
            if vfile in self.files_seen:
                print('WARNING: target %s was seen twice.' % vfile)
                continue
            env = self.env
            if kwds:
                env = env.Clone()
                env.Prepend(**kwds)
            self.objects.append(env.Object(vfile))

    def add_source_directories(self, *directories, **kwds):
        for d in directories:
            self.add_source_files(*list_sources(d, '.cpp'), **kwds)


def add_variant(state, tags, toolchains, result_name, module):
    variant = Variant(state, tags, toolchains, result_name)
    variant.add_module(module)

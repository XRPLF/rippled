# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

import collections, copy, os

from beast.build.Util import DictToAttr, list_sources, variant_file


class Scons(object):
    """
    Represents the state of a scons build environment.
    """
    def __init__(self, sconstruct_globals,
                 variant_tree=None, build_dir='build', environ=None,
                 **kwds):
        self.sconstruct_globals = DictToAttr(**sconstruct_globals)
        self.env = self.sconstruct_globals.Environment(**kwds)
        self.tags = []
        self.variant_tree = variant_tree or {}
        self.build_dir = build_dir
        self.environ = environ or sconstruct_globals.get('ENV') or os.environ
        self.aliases = collections.defaultdict(list)
        self.msvc_configs = []

        self.subst = getattr(self.env, 'subst', lambda x: x)
        self.check_for_fast_build()

    def set_variant_name(self, variant_name):
        self.variant_name = variant_name
        self.objects = []
        self.files_seen = set()

    def clone(self):
        clone = copy.copy(self)
        clone.env = clone.env.Clone()
        return clone

    def run_variants(self):
        for dest, source in self.variant_directory_tree().items():
            self.env.VariantDir(dest, source, duplicate=0)

    def variant_directory(self):
        return os.path.join(self.build_dir, self.variant_name)

    def variant_directory_tree(self):
        vdir = self.variant_directory()
        items = self.variant_tree.items()
        return {os.path.join(vdir, k): v for (k, v) in items}

    def get_environ(self, key, default=None):
        return self.subst(self.environ.get(key, default))

    @staticmethod
    def __run(prop, modules, *args):
        for m in modules:
            m = getattr(m, 'MODULE', m)
            attr = getattr(m, prop, lambda *x: None)
            attr(*args)

    def run_private(self, tags, modules):
        Scons.__run('run_private', modules, tags, self)

    def run_once(self, modules):
        Scons.__run('run_once', modules, self)

    def add_source_files(self, *filenames, **kwds):
        variants = self.variant_directory_tree()
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

    def add_source_by_directory(self, *directories, **kwds):
        for d in directories:
            self.add_source_files(*list_sources(d, '.cpp'), **kwds)

    def check_for_fast_build(self):
        self.sconstruct_globals.AddOption('--fast', action='store_true')
        if not self.sconstruct_globals.GetOption('fast'):
            return

        print('Warning: turning on fast build mode.')

        # http://www.scons.org/wiki/GoFastButton

        self.env.Decider('MD5-timestamp')
        self.sconstruct_globals.SetOption('implicit_cache', 1)
        self.sconstruct_globals.SetOption('max_drift', 1)
        self.env.SourceCode('.', None)

        """By default, scons scans each file for include dependecies. The
        implicit_cache flag lets you cache these dependencies for later builds,
        and will only rescan files that change.

        Failure cases are:
        1) If the include search paths are changed (i.e. CPPPATH), then a file
           may not be rebuilt.
        2) If a same-named file has been added to a directory that is earlier in
           the search path than the directory in which the file was found.
        Turn on if this build is for a specific debug target (i.e. clang.debug)

        If one of the failure cases applies, you can force a rescan of
        dependencies using the command line option `--implicit-deps-changed`

        """

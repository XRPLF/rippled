# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

import collections, copy, os

from . CheckForFastBuild import check_for_fast_build
from . import Variant, Module, Target, Toolchain
from .. System import SYSTEM


class State(object):
    """
    Represents the state of a scons build environment.
    """
    def __init__(self, sconstruct_globals, environment, variant_tree, build_dir,
                 environ=None):
        class Sconstruct(object):
            """Turn the SConstruct globals() back into a namespace,
            so you can write things like self.sconstruct.COMMAND_LINE_TARGETS.
            """
            def __getattr__(self, key):
                return sconstruct_globals[key]

        self.sconstruct = Sconstruct()
        self.env = self.sconstruct.Environment(**environment)
        self.variant_tree = variant_tree
        self.build_dir = build_dir
        self.environ = environ or sconstruct_globals.get('ENV', os.environ)
        self.aliases = collections.defaultdict(list)
        self.msvc_configs = []
        self.tags = [SYSTEM.platform.lower()]
        if SYSTEM.linux:
            self.tags.append('linux')

        if SYSTEM.osx:
            self.tags.append('osx')

        self.subst = getattr(self.env, 'subst', lambda x: x)
        check_for_fast_build(self)

    def get_environment_variable(self, key, default=None):
        return self.subst(self.environ.get(key, default))

    def add_aliases(self, key, *value):
        self.aliases[key].extend(value)

    def run_build(self, modules, targets):
        targets = Target.Targets(*targets)
        module = Module.compose(*modules)
        module.before(self)

        # Configure the toolchains, variants, default toolchain, and default
        # target.
        toolchains = Toolchain.detect(self)
        if not toolchains:
            raise ValueError('No toolchains detected!')

        target_line = list(self.sconstruct.COMMAND_LINE_TARGETS)

        for target, tags_list in targets.targets_to_tags(target_line).items():
            for tags in tags_list:
                Variant.add_variant(
                    self, tags, toolchains, target.result_name, module)

        for variant_name, target in self.aliases.iteritems():
            self.env.Alias(variant_name, target)

        module.after(self)

def _get_sitetools_dir():
    this_dir = os.path.dirname(os.path.abspath(__file__))
    py_dir = os.path.dirname(os.path.dirname(this_dir))
    return os.path.join(py_dir, 'site_tools')

DEFAULTS = {
    'build_dir': 'build',

    'environment': {
        'toolpath': [_get_sitetools_dir()],
        'tools': ['default', 'Protoc', 'VSProject'],
        'ENV': os.environ,
        'TARGET_ARCH': 'x86_64',
    },

    'variant_tree': {'src': 'src'},
}


def _run_build(sconstruct_globals, environment, variant_tree, build_dir,
            modules, targets):
    state = State(sconstruct_globals, environment, variant_tree, build_dir)
    return state.run_build(modules, targets)


def run_build(**kwds):
    _run_build(**dict(DEFAULTS, **kwds))

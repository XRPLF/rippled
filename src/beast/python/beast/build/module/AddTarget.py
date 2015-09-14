# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)


ALL_TARGET = 'all'
DEFAULT_TARGET = 'install'

def add_to_all(variant, all_target=ALL_TARGET):
    if variant.toolchain in variant.toolchains:
        variant.state.add_aliases(all_target, *variant.target)
        variant.state.add_aliases(variant.toolchain, *variant.target)

def make_default(variant, default_target=DEFAULT_TARGET, all_target=ALL_TARGET):
    install_target = variant.env.Install(
        variant.state.build_dir, source=variant.target)
    variant.env.Alias(default_target, install_target)
    variant.env.Default(install_target)
    variant.state.add_aliases(all_target, *install_target)

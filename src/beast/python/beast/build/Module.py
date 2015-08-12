# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

"""Modules are called in five phases:  before, setup, files, target, after.

The "before" phase happens once before any variants are created.  This is
where you can create singleton targets common to all variants.

The "setup" phase is the first thing that happens for each variant.  This is
where you can add variables to the environment which will be available to all
modules.

The "files" phase, right after "setup", is when the Module gets to add files to
be built.  Environment variables you set here will only apply to the current
module.

The "target" phase happens after all the files have been added, and once the
target has been given a value.  It's also called for each variant.

Finally, the "after" phase happens after all the variants have been processed.
This is where you can add things like PhonyTargets.

"""

PHASES = 'before', 'setup', 'files', 'target', 'after'

class Module(object):
    def __init__(self, **kwds):
        for phase in PHASES:
            setattr(self, phase, kwds.pop(phase, lambda build: None))
        assert not kwds, 'Unknown kwd: ' + str(kwds)


class Composer(object):
    """Combine a series of modules through composition."""
    def __init__(self, *modules):
        # Skip parent constructor!
        def make_method(phase):
            def f(build):
                for module in modules:
                    getattr(module, phase)(build)
            return f

        for p in PHASES:
            setattr(self, p, make_method(p))


compose = Composer

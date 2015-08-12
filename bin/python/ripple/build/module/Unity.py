from __future__ import (
    absolute_import, division, print_function, unicode_literals)

import os

from beast.build.Build import compose, for_tags


def files(modules=None,
          unity_files=None,
          nounity_directories=None,
          source_root='src/ripple',
          unity_root='src/ripple/unity',
          **kwds):
    def _join(root, parts, suffix=''):
        def _add_suffix(f):
            return f if f.endswith(suffix) else f + suffix
        items = (modules or []) + (parts or [])
        return (_add_suffix(os.path.join(root, m)) for m in items)

    def _unity(variant):
        files = _join(unity_root, unity_files, suffix='.cpp')
        variant.add_source_files(*files, **kwds)

    def _nounity(variant):
        directories = _join(source_root, nounity_directories)
        variant.add_source_directories(*directories, **kwds)

    return compose(for_tags('unity', _unity),
                   for_tags('nounity', _nounity))

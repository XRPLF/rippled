from __future__ import absolute_import, division, print_function, unicode_literals

"""A function that can be specified at the command line, with an argument."""

import importlib
import re

MATCHER = re.compile(r'([\w.]+)(.*)')

def _split_function(desc):
    m = MATCHER.match(desc)
    if not m:
        raise ValueError('"%s" is not a function' % desc)
    name, args = (g.strip() for g in m.groups())
    args = eval(args or '()')  # Yes, really eval()!
    if not isinstance(args, tuple):
        args = (args,)
    return name, args

class Function(object):
    def __init__(self, desc='', default_path=''):
        self.desc = desc.strip()
        if not self.desc:
            # Make an empty function that does nothing.
            self.args = ()
            self.function = lambda *args, **kwds: None
            return

        self.function, self.args = _split_function(self.desc)
        if '.' not in self.function:
            if default_path and not default_path.endswith('.'):
                default_path += '.'
            self.function = default_path + self.function
        p, m = self.function.rsplit('.', 1)
        try:
            mod = importlib.import_module(p)
        except:
            raise ValueError('Can\'t find Python module "%s"' % p)

        try:
            self.function = getattr(mod, m)
        except:
            raise ValueError('No function "%s" in module "%s"' % (p, m))

    def __str__(self):
        return self.desc

    def __call__(self, *args, **kwds):
        return self.function(*(args + self.args), **kwds)

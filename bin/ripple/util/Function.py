from __future__ import absolute_import, division, print_function, unicode_literals

"""A function that can be specified at the command line, with an argument."""

import importlib
import re
import tokenize

from StringIO import StringIO

MATCHER = re.compile(r'([\w.]+)(.*)')

REMAPPINGS = {
    'false': False,
    'true': True,
    'null': None,
    'False': False,
    'True': True,
    'None': None,
}

def eval_arguments(args):
    tokens = tokenize.generate_tokens(StringIO(args or '()').readline)
    def remap():
        for type, name, _, _, _ in tokens:
            if type == tokenize.NAME and name not in REMAPPINGS:
                yield tokenize.STRING, '"%s"' % name
            else:
                yield type, name
    untok = tokenize.untokenize(remap())
    if untok[1:-1].strip():
        untok = untok[:-1] + ',)'  # Force a tuple.
    return eval(untok, REMAPPINGS)

class Function(object):
    def __init__(self, desc='', default_path=''):
        self.desc = desc.strip()
        if not self.desc:
            # Make an empty function that does nothing.
            self.args = ()
            self.function = lambda *args, **kwds: None
            return

        m = MATCHER.match(desc)
        if not m:
            raise ValueError('"%s" is not a function' % desc)
        self.function, self.args = (g.strip() for g in m.groups())
        self.args = eval_arguments(self.args)

        if '.' not in self.function:
            if default_path and not default_path.endswith('.'):
                default_path += '.'
            self.function = default_path + self.function
        p, m = self.function.rsplit('.', 1)
        mod = importlib.import_module(p)
        # Errors in modules are swallowed here.
        # except:
        #    raise ValueError('Can\'t find Python module "%s"' % p)

        try:
            self.function = getattr(mod, m)
        except:
            raise ValueError('No function "%s" in module "%s"' % (m, p))

    def __str__(self):
        return self.desc

    def __call__(self, *args, **kwds):
        return self.function(*(args + self.args), **kwds)

    def __eq__(self, other):
        return self.function == other.function and self.args == other.args

    def __ne__(self, other):
        return not (self == other)

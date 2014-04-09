from __future__ import absolute_import, division, print_function, unicode_literals

from beast.util.Terminal import warn

_TAGS = frozenset(['debug', 'optimize'])

def _to_tag(name, value):
    return '%s%s' % ('' if value else 'no', name)

def get_tags(arguments, print=print):
    result = {}
    bad_tags = set(arguments) - _TAGS
    if bad_tags:
        warn("don't understand tags " + ' '.join(bad_tags), print=print)
    debug = result.get('debug', True)
    optimize = result.get('optimize', not debug)
    return _to_tag('debug', debug), _to_tag('optimize', optimize)

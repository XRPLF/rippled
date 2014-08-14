from __future__ import absolute_import, division, print_function, unicode_literals

def count_all_subitems(x):
    """Count the subitems of a Python object, including the object itself."""
    if isinstance(x, list):
        return 1 + sum(count_all_subitems(i) for i in x)
    if isinstance(x, dict):
        return 1 + sum(count_all_subitems(i) for i in x.itervalues())
    return 1

def prune(item, level, count_recursively=True):
    def subitems(x):
        i = count_all_subitems(x) - 1 if count_recursively else len(x)
        return '1 subitem' if i == 1 else '%d subitems' % i

    assert level >= 0
    if not item:
        return item

    if isinstance(item, list):
        if level:
            return [prune(i, level - 1, count_recursively) for i in item]
        else:
            return '[list with %s]' % subitems(item)

    if isinstance(item, dict):
        if level:
            return dict((k, prune(v, level - 1, count_recursively))
                        for k, v in item.iteritems())
        else:
           return '{dict with %s}' % subitems(item)

    return item

from __future__ import absolute_import, division, print_function, unicode_literals

def compose(*dicts):
    result = {}
    for d in dicts:
        result.update(**d)
    return result

def get_items_with_prefix(key, mapping):
    """Get all elements from the mapping whose keys are a prefix of the given
    key, sorted by increasing key length."""
    for k, v in sorted(mapping.items()):
        if key.startswith(k):
            yield v

def compose_prefix_dicts(key, mapping):
    return compose(*get_items_with_prefix(key, mapping))

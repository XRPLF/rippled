from __future__ import absolute_import, division, print_function, unicode_literals

"""
Convert a discontiguous range of integers to and from a human-friendly form.

Real world example is the server_info.complete_ledgers:
  8252899-8403772,8403824,8403827-8403830,8403834-8403876

"""

def from_string(desc, **aliases):
    if not desc:
        return []
    result = set()
    for d in desc.split(','):
        nums = [aliases.get(x, None) or int(x) for x in d.split('-')]
        if len(nums) == 1:
            result.add(nums[0])
        elif len(nums) == 2:
            result.update(range(nums[0], nums[1] + 1))
    return result

def to_string(r):
    groups = []
    next_group = []
    for i, x in enumerate(sorted(r)):
        if next_group and (x - next_group[-1]) > 1:
            groups.append(next_group)
            next_group = []
        next_group.append(x)
    if next_group:
        groups.append(next_group)

    def display(g):
        if len(g) == 1:
            return str(g[0])
        else:
            return '%s-%s' % (g[0], g[-1])

    return ','.join(display(g) for g in groups)

def is_range(desc, *names):
    try:
        from_string(desc, **dict((n, 1) for n in names))
        return True;
    except ValueError:
        return False

def join_ranges(*ranges, **aliases):
    result = set()
    for r in ranges:
        result.update(from_string(r, **aliases))
    return result

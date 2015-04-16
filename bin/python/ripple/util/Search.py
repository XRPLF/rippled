from __future__ import absolute_import, division, print_function, unicode_literals

FIRST, LAST = range(2)

def binary_search(begin, end, condition, location=FIRST):
    """Search for an i in the interval [begin, end] where condition(i) is true.
       If location is FIRST, return the first such i.
       If location is LAST, return the last such i.
       If there is no such i, then throw an exception.
    """
    b = condition(begin)
    e = condition(end)
    if b and e:
        return begin if location == FIRST else end

    if not (b or e):
        raise ValueError('%d/%d' % (begin, end))

    if b and location is FIRST:
        return begin

    if e and location is LAST:
        return end

    width = end - begin + 1
    if width == 1:
        if not b:
            raise ValueError('%d/%d' % (begin, end))
        return begin
    if width == 2:
        return begin if b else end

    mid = (begin + end) // 2
    m = condition(mid)

    if m == b:
        return binary_search(mid, end, condition, location)
    else:
        return binary_search(begin, mid, condition, location)

def linear_search(items, condition):
    """Yields each i in the interval [begin, end] where condition(i) is true.
    """
    for i in items:
        if condition(i):
            yield i

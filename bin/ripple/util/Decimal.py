from __future__ import absolute_import, division, print_function, unicode_literals

"""Fixed point numbers."""

POSITIONS = 10
POSITIONS_SHIFT = 10 ** POSITIONS

class Decimal(object):
    def __init__(self, desc='0'):
        if isinstance(desc, int):
            self.value = desc
            return
        if desc.startswith('-'):
            sign = -1
            desc = desc[1:]
        else:
            sign = 1
        parts = desc.split('.')
        if len(parts) == 1:
            parts.append('0')
        elif len(parts) > 2:
            raise Exception('Too many decimals in "%s"' % desc)
        number, decimal = parts
        # Fix the number of positions.
        decimal = (decimal + POSITIONS * '0')[:POSITIONS]
        self.value = sign * int(number + decimal)

    def accumulate(self, item):
        if not isinstance(item, Decimal):
            item = Decimal(item)
        self.value += item.value

    def __str__(self):
        if self.value >= 0:
            sign = ''
            value = self.value
        else:
            sign = '-'
            value = -self.value
        number = value // POSITIONS_SHIFT
        decimal = (value % POSITIONS_SHIFT) * POSITIONS_SHIFT

        if decimal:
            return '%s%s.%s' % (sign, number, str(decimal).rstrip('0'))
        else:
            return '%s%s' % (sign, number)

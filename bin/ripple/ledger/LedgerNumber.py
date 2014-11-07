from __future__ import absolute_import, division, print_function, unicode_literals

from ripple.util import Range

FIRST_EVER = 32570

LEDGERS = {
    'closed': 'the most recently closed ledger',
    'current': 'the current ledger',
    'first': 'the first complete ledger on this server',
    'last': 'the last complete ledger on this server',
    'validated': 'the most recently validated ledger',
    }

HELP = """
Ledgers are either represented by a number, or one of the special ledgers;
""" + ',\n'.join('%s, %s' % (k, v) for k, v in sorted(LEDGERS.items())
)

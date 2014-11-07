from __future__ import absolute_import, division, print_function, unicode_literals

import argparse
import importlib
import os

from ripple.ledger import LedgerNumber
from ripple.util import File
from ripple.util import Log
from ripple.util import PrettyPrint
from ripple.util import Range
from ripple.util.Function import Function

NAME = 'LedgerTool'
VERSION = '0.1'
NONE = '(none)'

_parser = argparse.ArgumentParser(
    prog=NAME,
    description='Retrieve and process Ripple ledgers.',
    epilog=LedgerNumber.HELP,
    )

# Positional arguments.
_parser.add_argument(
    'command',
    nargs='*',
    help='Command to execute.'
)

# Flag arguments.
_parser.add_argument(
    '--binary',
    action='store_true',
    help='If true, searches are binary - by default linear search is used.',
    )

_parser.add_argument(
    '--cache',
    default='~/.local/share/ripple/ledger',
    help='The cache directory.',
    )

_parser.add_argument(
    '--complete',
    action='store_true',
    help='If set, only match complete ledgers.',
    )

_parser.add_argument(
    '--condition', '-c',
    help='The name of a condition function used to match ledgers.',
    )

_parser.add_argument(
    '--config',
    help='The rippled configuration file name.',
    )

_parser.add_argument(
    '--database', '-d',
    nargs='*',
    default=NONE,
    help='Specify a database.',
    )

_parser.add_argument(
    '--display',
    help='Specify a function to display ledgers.',
    )

_parser.add_argument(
    '--full', '-f',
    action='store_true',
    help='If true, request full ledgers.',
    )

_parser.add_argument(
    '--indent', '-i',
    type=int,
    default=2,
    help='How many spaces to indent when display in JSON.',
    )

_parser.add_argument(
    '--offline', '-o',
    action='store_true',
    help='If true, work entirely from cache, do not try to contact the server.',
    )

_parser.add_argument(
    '--position', '-p',
    choices=['all', 'first', 'last'],
    default='last',
    help='Select which ledgers to display.',
    )

_parser.add_argument(
    '--rippled', '-r',
    help='The filename of a rippled binary for retrieving ledgers.',
    )

_parser.add_argument(
    '--server', '-s',
    help='IP address of a rippled JSON server.',
    )

_parser.add_argument(
    '--utc', '-u',
    action='store_true',
    help='If true, display times in UTC rather than local time.',
    )

_parser.add_argument(
    '--validations',
    default=3,
    help='The number of validations needed before considering a ledger valid.',
    )

_parser.add_argument(
    '--version',
    action='version',
    version='%(prog)s ' + VERSION,
    help='Print the current version of %(prog)s',
    )

_parser.add_argument(
    '--verbose', '-v',
    action='store_true',
    help='If true, give status messages on stderr.',
    )

_parser.add_argument(
    '--window', '-w',
    type=int,
    default=0,
    help='How many ledgers to display around the matching ledger.',
    )

_parser.add_argument(
    '--yes', '-y',
    action='store_true',
    help='If true, don\'t ask for confirmation on large commands.',
)

# Read the arguments from the command line.
ARGS = _parser.parse_args()
ARGS.NONE = NONE

Log.VERBOSE = ARGS.verbose

# Now remove any items that look like ledger numbers from the command line.
_command = ARGS.command
_parts = (ARGS.command, ARGS.ledgers) = ([], [])

for c in _command:
    _parts[Range.is_range(c, *LedgerNumber.LEDGERS)].append(c)

ARGS.command = ARGS.command or ['print' if ARGS.ledgers else 'info']

ARGS.cache = File.normalize(ARGS.cache)

if not ARGS.ledgers:
    if ARGS.condition:
        Log.warn('--condition needs a range of ledgers')
    if ARGS.display:
        Log.warn('--display needs a range of ledgers')

ARGS.condition = Function(
    ARGS.condition or 'all_ledgers', 'ripple.ledger.conditions')
ARGS.display = Function(
    ARGS.display or 'ledger_number', 'ripple.ledger.displays')

if ARGS.window < 0:
    raise ValueError('Window cannot be negative: --window=%d' %
                     ARGS.window)

PrettyPrint.INDENT = (ARGS.indent * ' ')

_loaders = (ARGS.database != NONE) + bool(ARGS.rippled) + bool(ARGS.server)

if not _loaders:
    ARGS.rippled = 'rippled'

elif _loaders > 1:
    raise ValueError('At most one of --database,  --rippled and --server '
                     'may be specified')

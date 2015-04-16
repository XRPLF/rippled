from __future__ import absolute_import, division, print_function, unicode_literals

from ripple.ledger.Args import ARGS
from ripple.util import Log
from ripple.util import Range
from ripple.util.PrettyPrint import pretty_print

SAFE = True

HELP = """cache
return server_info"""

def cache(server, clear=False):
    cache = server.cache(ARGS.full)
    name = ['summary', 'full'][ARGS.full]
    files = cache.file_count()
    if not files:
        Log.error('No files in %s cache.' % name)

    elif clear:
        if not clear.strip() == 'clear':
            raise Exception("Don't understand 'clear %s'." % clear)
        if not ARGS.yes:
            yes = raw_input('OK to clear %s cache? (y/N) ' % name)
            if not yes.lower().startswith('y'):
                Log.out('Cancelled.')
                return
        cache.clear(ARGS.full)
        Log.out('%s cache cleared - %d file%s deleted.' %
                (name.capitalize(), files, '' if files == 1 else 's'))

    else:
        caches = (int(c) for c in cache.cache_list())
        Log.out(Range.to_string(caches))

from __future__ import absolute_import, division, print_function, unicode_literals

import datetime

# Format for human-readable dates in rippled
_DATE_FORMAT = '%Y-%b-%d'
_TIME_FORMAT = '%H:%M:%S'
_DATETIME_FORMAT = '%s %s' % (_DATE_FORMAT, _TIME_FORMAT)

_FORMATS = _DATE_FORMAT, _TIME_FORMAT, _DATETIME_FORMAT

def parse_datetime(desc):
    for fmt in _FORMATS:
        try:
            return datetime.date.strptime(desc, fmt)
        except:
            pass
    raise ValueError("Can't understand date '%s'." % date)

def format_datetime(dt):
    return dt.strftime(_DATETIME_FORMAT)

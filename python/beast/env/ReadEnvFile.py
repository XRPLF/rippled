from __future__ import absolute_import, division, print_function, unicode_literals

import json
import re

from beast.util import String
from beast.util.Terminal import warn

ENV_LINE_MATCH = re.compile(r'(?: export \s+)? \s* ([^=\s]*) \s* = (.*)',
                            re.VERBOSE)

def read_env_file(data, print=print):
    try:
        return json.loads(data)
    except ValueError:
        pass

    bad_lines = []
    results = {}
    for number, raw_line in enumerate(data.splitlines()):
        line = String.remove_comment(raw_line).strip()
        if line:
            match = ENV_LINE_MATCH.match(line)
            if match:
                name, value = match.groups()
                results[name.strip()] = String.remove_quotes(value.strip())
            else:
                bad_lines.append([number, raw_line])
    if bad_lines:
         warn("Didn't understand the following environment file lines:", print)
         for number, line in bad_lines:
             print('%d. >>> %s' % (number + 1, line))

    return results

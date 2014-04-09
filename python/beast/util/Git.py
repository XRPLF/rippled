from __future__ import absolute_import, division, print_function, unicode_literals

import os

from beast.util import Execute
from beast.util import String

def describe(**kwds):
    return String.single_line(Execute.execute('git describe --tags', **kwds))

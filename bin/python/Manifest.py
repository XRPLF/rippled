#!/usr/bin/env python

import sys
from ripple.util import Sign

result = Sign.run_command(sys.argv[1:])
sys.exit(0 if result else -1)

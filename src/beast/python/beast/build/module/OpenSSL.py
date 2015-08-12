# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import absolute_import, division, print_function, unicode_literals

import os
import subprocess
import time

from beast.build import Module

def parse_time(t):
    return time.strptime(t, '%a %b %d %H:%M:%S %Z %Y')

CHECK_PLATFORMS = 'Debian', 'Ubuntu'
CHECK_COMMAND = 'openssl version -a'
CHECK_LINE = 'built on: '
BUILD_TIME = 'Mon Apr  7 20:33:19 UTC 2014'
OPENSSL_ERROR = ('Your openSSL was built on %s; '
                 'rippled needs a version built on or after %s.')

def check(scons):
    for line in subprocess.check_output(CHECK_COMMAND.split()).splitlines():
        if line.startswith(CHECK_LINE):
            line = line[len(CHECK_LINE):]
            if parse_time(line) < parse_time(BUILD_TIME):
                raise Exception(OPENSSL_ERROR % (line, BUILD_TIME))
            return
    else:
        raise Exception("Didn't find any '%s' line in '$ %s'" %
                        (CHECK_LINE, CHECK_COMMAND))


def env_windows(scons):
    root = os.path.normpath(scons.environ['OPENSSL_ROOT'])
    scons.env.Append(CPPPATH=os.path.join(root, 'include'),
                     LIBPATH=os.path.join(root, 'lib', 'VC', 'static'))


def env_darwin(scons):
    base_root = '/usr/local/Cellar/openssl/'
    most_recent = sorted(os.listdir(base_root))[-1]
    root = os.path.join(base_root, most_recent)
    scons.env.Prepend(CPPPATH='%s/include' % root,
                      LIBPATH='%s/lib' % root)

MODULE = Module.Module(
    Module.Env.Append(CPPDEFINES=['OPENSSL_NO_SSL2']),

    ['linux', check, Module.pkg_config('openssl')],
    ['windows', env_windows],
    ['darwin', env_darwin],
)

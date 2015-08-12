# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

import os
import platform

class __System(object):
    """Provides information about the host platform"""
    def __init__(self):
        self.name = platform.system()
        self.linux = self.name == 'Linux'
        self.osx = self.name == 'Darwin'
        self.windows = self.name == 'Windows'
        self.distro = None
        self.version = None

        # True if building under the Travis CI (http://travis-ci.org)
        self.travis = (
            os.environ.get('TRAVIS', '0') == 'true') and (
            os.environ.get('CI', '0') == 'true')

        if self.linux:
            self.distro, self.version, _ = platform.linux_distribution()
            self.__display = '%s %s (%s)' % (
                self.distro, self.version, self.name)

        elif self.osx:
            parts = platform.mac_ver()[0].split('.')
            while len(parts) < 3:
                parts.append('0')
            self.__display = '%s %s' % (self.name, '.'.join(parts))
        elif self.windows:
            release, version, csd, ptype = platform.win32_ver()
            self.__display = '%s %s %s (%s)' % (
                self.name, release, version, ptype)

        else:
            raise Exception('Unknown system platform "' + self.name + '"')

        self.platform = self.distro or self.name

    def __str__(self):
        return self.__display

SYSTEM = __System()

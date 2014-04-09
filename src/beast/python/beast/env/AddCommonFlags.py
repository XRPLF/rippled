from __future__ import absolute_import, division, print_function, unicode_literals

from beast.util import Boost
from beast.util import Git

def add_common_flags(env):
    git_flag = '-DTIP_BRANCH="%s"' % Git.describe()
    env['CPPFLAGS'] = '%s %s' % (env['CPPFLAGS'], git_flag)
    env['CPPPATH'].insert(0, Boost.CPPPATH)
    env['LIBPATH'].append(Boost.LIBPATH)
    env['BOOST_HOME'] = Boost.CPPPATH

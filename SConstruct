from __future__ import absolute_import, division, print_function, unicode_literals

import copy
import os
import sys

def add_beast_to_path():
    python_home = os.path.join(os.getcwd(), 'python')
    if python_home not in sys.path:
        sys.path.append(python_home)

add_beast_to_path()

from beast.env.AddCommonFlags import add_common_flags
from beast.env.AddUserEnv import add_user_env
from beast.env import Print
from beast.platform import GetEnvironment
from beast.util import Boost
from beast.util import File
from beast.util import Tests

VARIANT_DIRECTORIES = {
    'beast': ('bin', 'beast'),
    'modules': ('bin', 'modules'),
}

BOOST_LIBRARIES = 'boost_system',
MAIN_PROGRAM_FILE = 'beast/unit_test/tests/main.cpp'
DOTFILE = '~/.scons'

def main():
    File.validate_libraries(Boost.LIBPATH, BOOST_LIBRARIES)
    defaults = GetEnvironment.get_environment(ARGUMENTS)
    working = copy.deepcopy(defaults)
    add_common_flags(defaults)

    add_user_env(working, DOTFILE)
    add_common_flags(working)
    Print.print_build_config(working, defaults)

    env = Environment(**working)

    for name, path in VARIANT_DIRECTORIES.items():
        env.VariantDir(os.path.join(*path), name, duplicate=0)
    env.Replace(PRINT_CMD_LINE_FUNC=Print.print_cmd_line)
    Tests.run_tests(env, MAIN_PROGRAM_FILE, '.', '.test.cpp')

main()

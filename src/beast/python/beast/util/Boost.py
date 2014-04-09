from __future__ import absolute_import, division, print_function, unicode_literals

import os
import re

ROOT_ENV_VARIABLE = 'BOOST_ROOT'
MINIMUM_VERSION = 1, 55, 0
VERSION_FILE = 'boost', 'version.hpp'
LIBRARY_PATH_SEGMENT = 'stage', 'lib'
VERSION_MATCHER = re.compile(r'#define\s+BOOST_VERSION\s+(\d+)')

CANT_OPEN_VERSION_FILE_ERROR = """Unable to open boost version file %s.
You have set the environment variable BOOST_ROOT to be %s.
Please check to make sure that this points to a valid installation of boost."""

CANT_UNDERSTAND_VERSION_ERROR = (
    "Didn't understand version string '%s' from file %s'")
VERSION_TOO_OLD_ERROR = ('Your version of boost, %s, is older than the minimum '
                         'required, %s.')

def _text(major, minor, release):
    return '%d.%02d.%02d' % (major, minor, release)

def _raw_boost_path():
    try:
      path = os.environ[ROOT_ENV_VARIABLE]
      if path:
        return os.path.normpath(path)
    except KeyError:
        pass
    raise KeyError('%s environment variable is not set.' % ROOT_ENV_VARIABLE)

def _get_version_number(path):
    version_file = os.path.join(path, *VERSION_FILE)
    try:
        with open(version_file) as f:
            for line in f:
                match = VERSION_MATCHER.match(line)
                if match:
                    version = match.group(1)
                    try:
                        return int(version)
                    except ValueError:
                        raise Exception(CANT_UNDERSTAND_VERSION_ERROR %
                                        (version, version_file))
    except IOError:
        raise Exception(CANT_OPEN_VERSION_FILE_ERROR % (version_file, path))

def _validate_version(v):
    version = v // 100000, (v // 100) % 100, v % 100
    if version < MINIMUM_VERSION:
        raise Exception(VERSION_TOO_OLD_ERROR % (
    _text(*version), _text(*MINIMUM_VERSION)))

def _boost_path():
    path = _raw_boost_path()
    _validate_version(_get_version_number(path))
    return path

CPPPATH = _boost_path()
LIBPATH = os.path.join(CPPPATH, *LIBRARY_PATH_SEGMENT)

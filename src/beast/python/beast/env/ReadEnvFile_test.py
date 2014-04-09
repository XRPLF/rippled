from __future__ import absolute_import, division, print_function, unicode_literals

from unittest import TestCase
from beast.env.ReadEnvFile import read_env_file

from beast.util import Terminal
Terminal.CAN_CHANGE_COLOR = False

JSON = """
{
  "FOO": "foo",
  "BAR": "bar bar bar",
  "CPPFLAGS": "-std=c++11 -frtti -fno-strict-aliasing -DWOMBAT"
}"""

ENV = """
# An env file.

FOO=foo
export BAR="bar bar bar"
CPPFLAGS=-std=c++11 -frtti -fno-strict-aliasing -DWOMBAT

# export BAZ=baz should be ignored.

"""

RESULT = {
    'FOO': 'foo',
    'BAR': 'bar bar bar',
    'CPPFLAGS': '-std=c++11 -frtti -fno-strict-aliasing -DWOMBAT',
    }

BAD_ENV = ENV + """
This line isn't right.
NO SPACES IN NAMES="valid value"
"""

class test_ReadEnvFile(TestCase):
  def test_read_json(self):
    self.assertEqual(read_env_file(JSON), RESULT)

  def test_read_env(self):
    self.assertEqual(read_env_file(ENV), RESULT)

  def test_read_env_error(self):
    errors = []
    self.assertEqual(read_env_file(BAD_ENV, errors.append), RESULT)
    self.assertEqual(errors, [
        "WARNING: Didn't understand the following environment file lines:",
        "11. >>> This line isn't right.",
        '12. >>> NO SPACES IN NAMES="valid value"'])

from __future__ import absolute_import, division, print_function, unicode_literals

from unittest import TestCase

from beast.util import String
from beast.util import Terminal

Terminal.CAN_CHANGE_COLOR = False

class String_test(TestCase):
  def test_comments(self):
    self.assertEqual(String.remove_comment(''), '')
    self.assertEqual(String.remove_comment('#'), '')
    self.assertEqual(String.remove_comment('# a comment'), '')
    self.assertEqual(String.remove_comment('hello # a comment'), 'hello ')
    self.assertEqual(String.remove_comment(
      r'hello \# not a comment # a comment'),
      'hello # not a comment ')

  def test_remove_quotes(self):
    errors = []
    self.assertEqual(String.remove_quotes('hello', print=errors.append),
                     'hello')
    self.assertEqual(String.remove_quotes('"hello"', print=errors.append),
                     'hello')
    self.assertEqual(String.remove_quotes('hello"', print=errors.append),
                     'hello"')
    self.assertEqual(errors, [])

  def test_remove_quotes_error(self):
    errors = []
    self.assertEqual(String.remove_quotes('"hello', print=errors.append),
                     'hello')
    self.assertEqual(errors,
                     ['WARNING: line started with " but didn\'t end with one:',
                      '"hello'])

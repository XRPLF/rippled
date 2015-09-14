# This file is part of Beast: http://github.com/vinniefalco/Beast
#
# This file copyright (c) 2015, Tom Ritchford <tom@swirly.com>
# under the Boost software license http://www.boost.org/LICENSE_1_0.txt

from __future__ import (
    absolute_import, division, print_function, unicode_literals)

import os
import unittest

from beast.build import TagSet

TAG_SET_LIST = TagSet.TagSetList(
    ('release', 'debug', 'profile'),
    ('gcc', 'clang', 'msvc'),
    ('unity', 'nounity'),
)

TARGETS = TagSet.Targets(
    'install',
    install=TAG_SET_LIST,
    vcxproj=TagSet.TagSetList(),
    count=TagSet.TagSetList(),
)


class test_TagSet(unittest.TestCase):
    def test_empty(self):
        self.assertEquals(
            list(TAG_SET_LIST.match_tags([])),
            [('release', 'gcc', 'unity'),
             ('release', 'gcc', 'nounity'),
             ('release', 'clang', 'unity'),
             ('release', 'clang', 'nounity'),
             ('release', 'msvc', 'unity'),
             ('release', 'msvc', 'nounity'),
             ('debug', 'gcc', 'unity'),
             ('debug', 'gcc', 'nounity'),
             ('debug', 'clang', 'unity'),
             ('debug', 'clang', 'nounity'),
             ('debug', 'msvc', 'unity'),
             ('debug', 'msvc', 'nounity'),
             ('profile', 'gcc', 'unity'),
             ('profile', 'gcc', 'nounity'),
             ('profile', 'clang', 'unity'),
             ('profile', 'clang', 'nounity'),
             ('profile', 'msvc', 'unity'),
             ('profile', 'msvc', 'nounity'),
         ])

    def test_debug(self):
        self.assertEquals(
            list(TAG_SET_LIST.match_tags(['debug'])),
            [
                ('debug', 'gcc', 'unity'),
                ('debug', 'gcc', 'nounity'),
                ('debug', 'clang', 'unity'),
                ('debug', 'clang', 'nounity'),
                ('debug', 'msvc', 'unity'),
                ('debug', 'msvc', 'nounity'),
                ])

    def test_single(self):
        self.assertEquals(
            list(TAG_SET_LIST.match_tags(['debug', 'clang', 'nounity'])),
            [('debug', 'clang', 'nounity')])

    def test_single2(self):
        self.assertEquals(
            list(TAG_SET_LIST.match_tags(['debug', 'clang'])),
            [('debug', 'clang', 'unity'), ('debug', 'clang', 'nounity')])

    def test_all_case(self):
        self.assertEquals(
            list(TAG_SET_LIST.match_tags(['nounity'])),
            [('release', 'gcc', 'nounity'),
             ('release', 'clang', 'nounity'),
             ('release', 'msvc', 'nounity'),
             ('debug', 'gcc', 'nounity'),
             ('debug', 'clang', 'nounity'),
             ('debug', 'msvc', 'nounity'),
             ('profile', 'gcc', 'nounity'),
             ('profile', 'clang', 'nounity'),
             ('profile', 'msvc', 'nounity'),
          ])

    def test_duplicate_tagset(self):
        with self.assertRaises(ValueError) as ve:
            TagSet.TagSetList(
                ('debug', 'release', 'profile'),
                ('debug', 'apple'))
        self.assertEquals(ve.exception[0], 'Duplicated tags: debug')

    def test_mutually_exclusive(self):
        with self.assertRaises(ValueError) as ve:
            TAG_SET_LIST.match_tags(['debug', 'release'])
        self.assertEquals(ve.exception[0],
                          'Mutually exclusive tags: release, debug')

    def test_default_argument(self):
        self.assertEquals(TARGETS.default_argument,
                          'release.gcc.unity')

    def test_trivial_target(self):
        self.assertEquals(dict(TARGETS.targets_to_tags([])), {})

    def test_single_target(self):
        self.assertEquals(
            dict(TARGETS.targets_to_tags(['gcc'])),
            {'install': set([('debug', 'gcc', 'nounity'),
                             ('debug', 'gcc', 'unity'),
                             ('profile', 'gcc', 'nounity'),
                             ('profile', 'gcc', 'unity'),
                             ('release', 'gcc', 'nounity'),
                             ('release', 'gcc', 'unity')])})

    def test_double_target(self):
        self.assertEquals(
            dict(TARGETS.targets_to_tags(['gcc.debug', 'vcxproj'])),
            {'install': set([('debug', 'gcc', 'nounity'),
                             ('debug', 'gcc', 'unity')]),
             'vcxproj': set([()])})

    def test_all(self):
        self.assertEquals(
            dict(TARGETS.targets_to_tags(['all'])),
            {
                'count': set([()]),
                'install': set([
                    ('debug', 'clang', 'nounity'),
                    ('debug', 'clang', 'unity'),
                    ('debug', 'gcc', 'nounity'),
                    ('debug', 'gcc', 'unity'),
                    ('debug', 'msvc', 'nounity'),
                    ('debug', 'msvc', 'unity'),
                    ('profile', 'clang', 'nounity'),
                    ('profile', 'clang', 'unity'),
                    ('profile', 'gcc', 'nounity'),
                    ('profile', 'gcc', 'unity'),
                    ('profile', 'msvc', 'nounity'),
                    ('profile', 'msvc', 'unity'),
                    ('release', 'clang', 'nounity'),
                    ('release', 'clang', 'unity'),
                    ('release', 'gcc', 'nounity'),
                    ('release', 'gcc', 'unity'),
                    ('release', 'msvc', 'nounity'),
                    ('release', 'msvc', 'unity')]),
                'vcxproj': set([()])
            })



if __name__ == "__main__":
    unittest.main()

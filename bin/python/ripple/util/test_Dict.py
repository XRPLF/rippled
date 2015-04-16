from __future__ import absolute_import, division, print_function, unicode_literals

from ripple.util import Dict

from unittest import TestCase

class test_Dict(TestCase):
    def test_count_all_subitems(self):
        self.assertEquals(Dict.count_all_subitems({}), 1)
        self.assertEquals(Dict.count_all_subitems({'a': {}}), 2)
        self.assertEquals(Dict.count_all_subitems([1]), 2)
        self.assertEquals(Dict.count_all_subitems([1, 2]), 3)
        self.assertEquals(Dict.count_all_subitems([1, {2: 3}]), 4)
        self.assertEquals(Dict.count_all_subitems([1, {2: [3]}]), 5)
        self.assertEquals(Dict.count_all_subitems([1, {2: [3, 4]}]), 6)

    def test_prune(self):
        self.assertEquals(Dict.prune({}, 0), {})
        self.assertEquals(Dict.prune({}, 1), {})

        self.assertEquals(Dict.prune({1: 2}, 0), '{dict with 1 subitem}')
        self.assertEquals(Dict.prune({1: 2}, 1), {1: 2})
        self.assertEquals(Dict.prune({1: 2}, 2), {1: 2})

        self.assertEquals(Dict.prune([1, 2, 3], 0), '[list with 3 subitems]')
        self.assertEquals(Dict.prune([1, 2, 3], 1), [1, 2, 3])

        self.assertEquals(Dict.prune([{1: [2, 3]}], 0),
                          '[list with 4 subitems]')
        self.assertEquals(Dict.prune([{1: [2, 3]}], 1),
                          ['{dict with 3 subitems}'])
        self.assertEquals(Dict.prune([{1: [2, 3]}], 2),
                          [{1: u'[list with 2 subitems]'}])
        self.assertEquals(Dict.prune([{1: [2, 3]}], 3),
                          [{1: [2, 3]}])

    def test_prune_nosub(self):
        self.assertEquals(Dict.prune({}, 0, False), {})
        self.assertEquals(Dict.prune({}, 1, False), {})

        self.assertEquals(Dict.prune({1: 2}, 0, False), '{dict with 1 subitem}')
        self.assertEquals(Dict.prune({1: 2}, 1, False), {1: 2})
        self.assertEquals(Dict.prune({1: 2}, 2, False), {1: 2})

        self.assertEquals(Dict.prune([1, 2, 3], 0, False),
                          '[list with 3 subitems]')
        self.assertEquals(Dict.prune([1, 2, 3], 1, False), [1, 2, 3])

        self.assertEquals(Dict.prune([{1: [2, 3]}], 0, False),
                          '[list with 1 subitem]')
        self.assertEquals(Dict.prune([{1: [2, 3]}], 1, False),
                          ['{dict with 1 subitem}'])
        self.assertEquals(Dict.prune([{1: [2, 3]}], 2, False),
                          [{1: u'[list with 2 subitems]'}])
        self.assertEquals(Dict.prune([{1: [2, 3]}], 3, False),
                          [{1: [2, 3]}])

"""Unified-diff hunk arithmetic.

Every node match is an interval test against these ranges, so an off-by-one here
silently mis-attributes or drops changes. The fixtures use hunk headers written
by hand, with the correct answer stated in the header itself.
"""

import pytest

from diffparse import FileDiff, parse_unified_diff, subtract_ranges


def test_single_added_line_without_count():
    # `+1392` with no count means exactly one line.
    diff = "--- a/f.cpp\n+++ b/f.cpp\n@@ -1391,0 +1392 @@\n+    added\n"
    parsed = parse_unified_diff(diff)["f.cpp"]
    assert parsed.ranges == [(1392, 1392)]
    assert parsed.added_lines == {1392: "    added"}


def test_multi_line_addition_records_each_line():
    diff = "--- a/f.cpp\n+++ b/f.cpp\n@@ -10,0 +11,3 @@\n+a\n+b\n+c\n"
    parsed = parse_unified_diff(diff)["f.cpp"]
    assert parsed.ranges == [(11, 13)]
    assert parsed.added_lines == {11: "a", 12: "b", 13: "c"}


def test_pure_deletion_records_the_seam():
    """A new-side count of 0 has no line to point at; the seam is kept so a
    delete-only change inside a fork body still touches that fork."""
    diff = "--- a/f.cpp\n+++ b/f.cpp\n@@ -20,2 +19,0 @@\n-gone\n-also gone\n"
    assert parse_unified_diff(diff)["f.cpp"].ranges == [(19, 20)]


def test_deletion_at_start_of_file_clamps_to_line_one():
    diff = "--- a/f.cpp\n+++ b/f.cpp\n@@ -1,2 +0,0 @@\n-a\n-b\n"
    assert parse_unified_diff(diff)["f.cpp"].ranges == [(1, 2)]


def test_modification_counts_as_new_side_range():
    diff = "--- a/f.cpp\n+++ b/f.cpp\n@@ -5,1 +5,1 @@\n-old\n+new\n"
    parsed = parse_unified_diff(diff)["f.cpp"]
    assert parsed.ranges == [(5, 5)]
    assert parsed.added_lines == {5: "new"}


def test_adjacent_hunks_merge_but_distant_ones_do_not():
    diff = (
        "--- a/f.cpp\n+++ b/f.cpp\n"
        "@@ -1,0 +2,1 @@\n+a\n"
        "@@ -2,0 +3,1 @@\n+b\n"  # adjacent to the previous range
        "@@ -50,0 +60,1 @@\n+c\n"  # far away
    )
    assert parse_unified_diff(diff)["f.cpp"].ranges == [(2, 3), (60, 60)]


def test_multiple_files_are_kept_apart():
    diff = (
        "--- a/one.cpp\n+++ b/one.cpp\n@@ -1,0 +1,1 @@\n+x\n"
        "--- a/two.cpp\n+++ b/two.cpp\n@@ -9,0 +9,1 @@\n+y\n"
    )
    parsed = parse_unified_diff(diff)
    assert parsed["one.cpp"].ranges == [(1, 1)]
    assert parsed["two.cpp"].ranges == [(9, 9)]


def test_added_file_uses_new_path():
    diff = "--- /dev/null\n+++ b/new.cpp\n@@ -0,0 +1,2 @@\n+a\n+b\n"
    parsed = parse_unified_diff(diff)
    assert parsed["new.cpp"].ranges == [(1, 2)]
    assert not parsed["new.cpp"].deleted


def test_deleted_file_is_flagged_with_no_ranges():
    """A deleted file has no new-side lines; it is recorded under its old path
    so it is visible, but it can never produce a span match."""
    diff = "--- a/gone.cpp\n+++ /dev/null\n@@ -1,2 +0,0 @@\n-a\n-b\n"
    parsed = parse_unified_diff(diff)
    assert parsed["gone.cpp"].deleted
    assert parsed["gone.cpp"].ranges == []


def test_rename_is_attributed_to_the_new_path():
    diff = (
        "diff --git a/old.cpp b/new.cpp\n"
        "similarity index 95%\n"
        "rename from old.cpp\n"
        "rename to new.cpp\n"
        "--- a/old.cpp\n+++ b/new.cpp\n@@ -3,1 +3,1 @@\n-a\n+b\n"
    )
    parsed = parse_unified_diff(diff)
    assert "new.cpp" in parsed and "old.cpp" not in parsed


def test_binary_file_is_represented_with_no_ranges():
    diff = (
        "diff --git a/img.png b/img.png\n"
        "Binary files a/img.png and b/img.png differ\n"
    )
    parsed = parse_unified_diff(diff)
    assert list(parsed) == ["img.png"]
    assert parsed["img.png"].ranges == []


def test_pure_rename_is_still_represented():
    """A 100%-similarity rename emits no ---/+++ pair at all. Parsing only those
    made the moved file vanish from the report entirely."""
    diff = (
        "diff --git a/old.cpp b/moved.cpp\n"
        "similarity index 100%\n"
        "rename from old.cpp\n"
        "rename to moved.cpp\n"
    )
    parsed = parse_unified_diff(diff)
    assert list(parsed) == ["moved.cpp"]
    assert parsed["moved.cpp"].ranges == []


def test_added_line_starting_with_plusplus_is_not_a_header():
    """Regression: content `++ ohno` reads as a `+++ ` file header. That created
    a phantom file and reassigned every later hunk in the real file to it."""
    diff = (
        "--- a/f.cpp\n+++ b/f.cpp\n"
        "@@ -3,1 +3,1 @@\n-old\n+++ ohno\n"
        "@@ -31,1 +31,1 @@\n-old\n+REALLY IMPORTANT\n"
    )
    parsed = parse_unified_diff(diff)
    assert list(parsed) == ["f.cpp"]
    assert parsed["f.cpp"].ranges == [(3, 3), (31, 31)]
    assert parsed["f.cpp"].added_lines == {3: "++ ohno", 31: "REALLY IMPORTANT"}


def test_context_lines_advance_the_new_side_counter():
    """With context (a GitHub .patch is -U3), added lines must still land on
    their real new-side numbers."""
    diff = (
        "--- a/f.cpp\n+++ b/f.cpp\n@@ -1,5 +1,6 @@\n"
        " ctx1\n ctx2\n+added\n ctx3\n ctx4\n ctx5\n"
    )
    assert parse_unified_diff(diff)["f.cpp"].added_lines == {3: "added"}


def test_added_line_text_is_not_confused_with_hunk_markers():
    # A literal '+++' inside added content must not be read as a file header.
    diff = '--- a/f.cpp\n+++ b/f.cpp\n@@ -1,0 +1,1 @@\n+auto s = "+++";\n'
    parsed = parse_unified_diff(diff)
    assert list(parsed) == ["f.cpp"]
    assert parsed["f.cpp"].added_lines == {1: 'auto s = "+++";'}


def test_empty_diff():
    assert parse_unified_diff("") == {}


# --- interval helpers -------------------------------------------------------


def test_touches_is_inclusive_at_both_ends():
    fd = FileDiff("f.cpp", ranges=[(10, 12), (40, 40)])
    assert fd.touches(12, 20) == [(10, 12)]
    assert fd.touches(1, 10) == [(10, 12)]
    assert fd.touches(13, 39) == []
    assert fd.touches(1, 100) == [(10, 12), (40, 40)]


def test_merge_ranges_handles_unsorted_input():
    """Regression: the accumulator was seeded with the *unsorted* first element
    while iterating the sorted rest, so any range sorting before it was dropped.
    Node spans reach subtract_ranges in node order, never line order."""
    from diffparse import _merge_ranges

    assert _merge_ranges([(100, 200), (1, 5), (7, 9)]) == [(1, 5), (7, 9), (100, 200)]
    assert _merge_ranges([(300, 350), (100, 200)]) == [(100, 200), (300, 350)]


def test_subtract_ranges_with_unsorted_covers():
    assert subtract_ranges([(1, 200)], [(100, 110), (10, 20)]) == [
        (1, 9),
        (21, 99),
        (111, 200),
    ]


@pytest.mark.parametrize(
    "ranges,covers,expected",
    [
        ([(1, 10)], [(1, 10)], []),  # fully covered
        ([(1, 10)], [], [(1, 10)]),  # nothing covers
        ([(1, 10)], [(4, 6)], [(1, 3), (7, 10)]),  # split in two
        ([(1, 10)], [(1, 4)], [(5, 10)]),  # trimmed at the front
        ([(1, 10)], [(8, 20)], [(1, 7)]),  # trimmed at the back
        ([(1, 3), (20, 25)], [(1, 3)], [(20, 25)]),  # one range removed whole
        ([(5, 5)], [(1, 4), (6, 9)], [(5, 5)]),  # gap between covers
        ([(1, 100)], [(80, 90), (10, 20)], [(1, 9), (21, 79), (91, 100)]),  # unsorted
    ],
)
def test_subtract_ranges(ranges, covers, expected):
    assert subtract_ranges(ranges, covers) == expected

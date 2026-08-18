"""Location model, comment stripping, and macro/enum row span extraction.

Spans are the substrate the PR mapper stands on: a span off by two lines maps a
diff to the wrong node, or to none. These tests pin the line arithmetic with
synthetic fixtures whose correct answers are obvious by inspection.
"""

import pytest

from graph import (
    FEATURE_TRANSACTOR,
    LOC_IMPL,
    LOC_MACRO,
    LOC_REFERENCE,
    FeatureNode,
    Location,
    add_locations,
    feature_id,
)
from macro_extractor import macro_invocation_spans, parse_sfield_rows
from privilege_extractor import locate_privilege_checks, parse_privilege_rows
from sourceutil import line_count, repo_relative, strip_comments, whole_file_location

# --- Location model ---------------------------------------------------------


def test_location_rejects_malformed():
    with pytest.raises(ValueError, match="role"):
        Location("a.cpp", 1, 2, "bogus")
    with pytest.raises(ValueError, match="1-based"):
        Location("a.cpp", 0, 2, LOC_MACRO)
    with pytest.raises(ValueError, match="precedes"):
        Location("a.cpp", 5, 4, LOC_MACRO)
    # A single-line span is the degenerate but legal case.
    assert Location("a.cpp", 5, 5, LOC_MACRO).end_line == 5


def test_add_locations_merges_dedups_and_sorts():
    node = FeatureNode(feature_id(FEATURE_TRANSACTOR, "T"), FEATURE_TRANSACTOR, "T")
    b = Location("b.cpp", 1, 2, LOC_MACRO)
    a = Location("a.cpp", 9, 9, LOC_IMPL)
    add_locations(node, [b])
    add_locations(node, [b, a])  # b repeated -> deduplicated, not duplicated
    assert node.locations == [a, b]


# --- comment stripping ------------------------------------------------------


def test_strip_comments_preserves_line_numbering():
    text = "one\n// two\n/* three\n four */\nfive\n"
    stripped = strip_comments(text)
    assert len(stripped.splitlines()) == len(text.splitlines())
    lines = stripped.splitlines()
    assert lines[0] == "one"
    assert lines[1].strip() == ""
    assert lines[2].strip() == ""
    assert lines[3].strip() == ""
    assert lines[4] == "five"


def test_strip_comments_honors_string_literals():
    # The `//` and `/*` live inside literals, so nothing may be stripped.
    text = 'auto url = "http://example.com"; char c = \'/\'; auto s = "/* not */";'
    assert strip_comments(text) == text


def test_strip_comments_handles_escaped_quote():
    text = 'auto s = "a\\"// still in string"; // gone'
    out = strip_comments(text)
    assert "still in string" in out
    assert "gone" not in out


# --- macro row spans --------------------------------------------------------

MACRO_FIXTURE = "\n".join(
    [
        "// header comment",  # 1
        "",  # 2
        "TRANSACTION(ttONE, 0, One,",  # 3
        "    Delegation::Delegable,",  # 4
        "    ({",  # 5
        "        {sfAlpha, soeREQUIRED},",  # 6
        "    }))",  # 7
        "",  # 8
        "// TRANSACTION(ttCOMMENTED, 9, Commented, ({}))",  # 9
        "TRANSACTION(ttTWO, 1, Two, ({}))",  # 10
    ]
)


def test_macro_spans_multiline_and_single_line():
    assert macro_invocation_spans(MACRO_FIXTURE, "TRANSACTION") == [(3, 7), (10, 10)]


def test_macro_spans_allow_space_before_paren():
    # features.macro aligns XRPL_FIX with padding before the open paren.
    text = "XRPL_FIX    (Alpha, Supported::Yes, VoteBehavior::DefaultNo)"
    assert macro_invocation_spans(text, "XRPL_FIX") == [(1, 1)]


def test_macro_spans_ignore_paren_inside_comment():
    # An unbalanced paren in a trailing comment must not extend the span.
    text = "TRANSACTION(ttONE, 0, One, ({}))  // note: see foo( for details\nnext\n"
    assert macro_invocation_spans(text, "TRANSACTION") == [(1, 1)]


def test_macro_spans_unterminated_is_fatal():
    with pytest.raises(ValueError, match="unterminated"):
        macro_invocation_spans("TRANSACTION(ttONE, 0, One,\n    ({}\n", "TRANSACTION")


def test_macro_spans_do_not_match_longer_prefix():
    # XRPL_RETIRE_FEATURE must not be picked up when scanning for XRPL_FEATURE.
    text = "XRPL_RETIRE_FEATURE(Old)\nXRPL_FEATURE(New, Supported::Yes, V::N)\n"
    assert macro_invocation_spans(text, "XRPL_FEATURE") == [(2, 2)]


# --- sfields.macro rows -----------------------------------------------------


def test_sfield_rows_report_the_declaring_line(tmp_path):
    """Regression: a `\\s*` indent class ate the preceding blank lines and
    reported the line where the run of whitespace started, not the declaration.
    The blank lines and comment before sfBeta are what make this bite."""
    macro = tmp_path / "sfields.macro"
    macro.write_text(
        "\n".join(
            [
                "TYPED_SFIELD(sfAlpha, UINT8, 1)",  # 1
                "",  # 2
                "",  # 3
                "// 8-bit integers (uncommon)",  # 4
                "TYPED_SFIELD(sfBeta,  UINT8, 2)",  # 5
                "UNTYPED_SFIELD(sfGamma, OBJECT, 3)",  # 6
                "CONSTRUCT_TYPED_SFIELD(sfDelta, AMOUNT, 4)",  # 7
            ]
        )
    )
    rows = parse_sfield_rows(macro)
    assert rows["sfAlpha"] == (1, 1)
    assert rows["sfBeta"] == (5, 5)
    assert rows["sfGamma"] == (6, 6)
    assert rows["sfDelta"] == (7, 7)


def test_sfield_rows_empty_file_is_fatal(tmp_path):
    macro = tmp_path / "sfields.macro"
    macro.write_text("// nothing here\n")
    with pytest.raises(ValueError, match="No SField declarations"):
        parse_sfield_rows(macro)


# --- privilege enum rows ----------------------------------------------------


def test_privilege_rows_lines_and_exclusions(tmp_path):
    header = tmp_path / "InvariantCheckPrivilege.h"
    header.write_text(
        "\n".join(
            [
                "#pragma once",  # 1
                "// mentions ChangeNftCounts in prose only",  # 2
                "enum Privilege {",  # 3
                "    NoPriv = 0x0000,",  # 4
                "",  # 5
                "    ChangeNftCounts = 0x0020,  // burn or mint",  # 6
                "    OverrideFreeze = 0x0040,",  # 7
                "};",  # 8
            ]
        )
    )
    rows = parse_privilege_rows(header)
    assert rows == {"ChangeNftCounts": 6, "OverrideFreeze": 7}


def test_privilege_rows_missing_enum_is_fatal(tmp_path):
    header = tmp_path / "h.h"
    header.write_text("enum Other { A = 0x1 };\n")
    with pytest.raises(ValueError, match="enum Privilege not found"):
        parse_privilege_rows(header)


def test_privilege_checks_report_precise_reference_spans(tmp_path):
    source = tmp_path / "src/libxrpl/tx/invariants"
    header = tmp_path / "include/xrpl/tx/invariants"
    source.mkdir(parents=True)
    header.mkdir(parents=True)
    path = source / "Example.cpp"
    path.write_text(
        "\n".join(
            [
                "void check(Tx const& tx)",
                "{",
                "    if (!hasPrivilege(",
                "            tx, ChangeNftCounts | OverrideFreeze))",
                "        fail();",
                "}",
            ]
        )
    )
    checks = locate_privilege_checks(tmp_path)
    expected = Location(
        "src/libxrpl/tx/invariants/Example.cpp",
        3,
        4,
        LOC_REFERENCE,
    )
    assert checks["ChangeNftCounts"] == [expected]
    assert checks["OverrideFreeze"] == [expected]


# --- path and whole-file helpers -------------------------------------------


def test_repo_relative_is_posix_and_anchored(tmp_path):
    (tmp_path / "src").mkdir()
    target = tmp_path / "src" / "a.cpp"
    target.write_text("x\n")
    assert repo_relative(target, tmp_path) == "src/a.cpp"


def test_repo_relative_rejects_outside_root(tmp_path):
    outside = tmp_path.parent / "elsewhere.cpp"
    with pytest.raises(ValueError):
        repo_relative(outside, tmp_path / "root")


def test_line_count_and_whole_file_never_zero(tmp_path):
    empty = tmp_path / "empty.cpp"
    empty.write_text("")
    assert line_count(empty) == 1
    loc = whole_file_location(empty, tmp_path, LOC_IMPL)
    assert (loc.start_line, loc.end_line, loc.role) == (1, 1, LOC_IMPL)

    three = tmp_path / "three.cpp"
    three.write_text("a\nb\nc\n")
    assert line_count(three) == 3
    # The span must cover the real last line, not merely start at 1.
    assert whole_file_location(three, tmp_path, LOC_IMPL).end_line == 3

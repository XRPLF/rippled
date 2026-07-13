#!/usr/bin/env python3
"""
Tests for check_doxygen_style.py.

Run directly (no test framework needed):
    ./bin/pre-commit/test_check_doxygen_style.py
or under pytest:
    pytest bin/pre-commit/test_check_doxygen_style.py
"""

import sys
import textwrap

from check_doxygen_style import Finding, check_source


def findings_for(text: str) -> list[Finding]:
    """Return the style violations for the given source text.

    The text is dedented and its leading newline stripped, so fixtures can be
    written as indented triple-quoted here-docs while keeping honest 1-based
    line numbers.
    """
    text = textwrap.dedent(text).lstrip("\n")
    return check_source(text)


def labels_for(text: str) -> list[str]:
    return [f.category.label for f in findings_for(text)]


def messages_for(text: str) -> list[str]:
    return [f.message for f in findings_for(text)]


# --- well-formed input produces nothing -------------------------------------


def test_clean_block_ok() -> None:
    code = """
    /**
     * Brief.
     *
     * @tparam T a type
     * @param x the x
     * @return the result
     */
    """
    assert findings_for(code) == []


def test_blank_lines_inside_block_ok() -> None:
    code = """
    /**
     * a
     *
     * b
     */
    """
    assert findings_for(code) == []


def test_member_and_divider_allowed() -> None:
    assert findings_for("int x;  ///< ok member\n") == []
    assert findings_for("//////////\n") == []
    assert findings_for("//// text\n") == []


# --- line-comment forms ------------------------------------------------------


def test_triple_slash() -> None:
    code = "/// doc\n"
    assert labels_for(code) == ["triple-slash"]


def test_qt_line() -> None:
    code = "//! doc\n"
    assert labels_for(code) == ["qt-line"]


def test_qt_member() -> None:
    code = "int x;  //!< doc\n"
    assert labels_for(code) == ["qt-member"]


def test_block_member() -> None:
    code = "int x;  /**< doc */\n"
    assert labels_for(code) == ["block-member"]


def test_qt_block_member() -> None:
    code = "int x;  /*!< doc */\n"
    assert labels_for(code) == ["qt-block-member"]


def test_doc_in_line_comment() -> None:
    code = "// @param x\n"
    assert labels_for(code) == ["doc-in-line-comment"]


# --- block forms -------------------------------------------------------------


def test_qt_comment() -> None:
    code = """
    /*!
     * brief
     */
    """
    assert labels_for(code) == ["qt-comment"]


def test_qt_comment_single_line() -> None:
    # /*! ... */ on one line -> qt-comment (plus single-line-block)
    code = "/*! brief */\n"
    assert labels_for(code) == ["qt-comment", "single-line-block"]


def test_single_line_block() -> None:
    code = "/** brief */\n"
    assert labels_for(code) == ["single-line-block"]


def test_single_line_markers_allowed() -> None:
    for marker in ("@{", "@}", "@cond LABEL", "@endcond", "@file foo.h"):
        code = f"/** {marker} */\n"
        assert findings_for(code) == [], marker


def test_text_on_opener() -> None:
    code = """
    /** text here
     * more
     */
    """
    assert labels_for(code) == ["text-on-opener"]


def test_bare_continuation() -> None:
    code = """
    /**
     * a
       bare line
     */
    """
    assert labels_for(code) == ["bare-continuation"]


def test_over_indented_first_line() -> None:
    code = """
    /**
     *   over
     */
    """
    assert labels_for(code) == ["over-indented"]


def test_over_indented_tag() -> None:
    # a flush first line consumes "first content", isolating the tag check
    code = """
    /**
     * brief
     *   @param x
     */
    """
    assert labels_for(code) == ["over-indented-tag"]


def test_combined_marker() -> None:
    code = """
    /**
     * @{
     */
    """
    assert labels_for(code) == ["combined-marker"]


def test_prose_label() -> None:
    for word in ("Returns", "Throws", "Exceptions"):
        code = f"""
        /**
         * {word}:
         */
        """
        assert labels_for(code) == ["prose-label"], word


def test_content_on_closer() -> None:
    code = """
    /**
     * a
     * b */
    """
    assert labels_for(code) == ["content-on-closer"]


def test_plain_block_doc() -> None:
    assert labels_for("/* @param x */\n") == ["plain-block-doc"]
    assert findings_for("/* just an ordinary note */\n") == []


def test_tag_order() -> None:
    out_of_order = """
    /**
     * @param x
     * @tparam T
     */
    """
    assert labels_for(out_of_order) == ["tag-order"]

    correct = """
    /**
     * @tparam T
     * @param x
     * @return r
     */
    """
    assert findings_for(correct) == []

    single = """
    /**
     * @param x
     */
    """
    assert findings_for(single) == []  # single tag: never out of order


# --- command spelling (must work on body/closer lines, not just the opener) --


def test_backslash_command_on_body_line() -> None:
    code = r"""
    /**
     * \brief x
     */
    """
    assert labels_for(code) == ["backslash-command"]


def test_backslash_command_suggests_canonical_spelling() -> None:
    # a backslash + non-canonical spelling is fixed in one pass, not two:
    # \sa -> @see (not @sa), \returns -> @return (not @returns)
    sa = r"""
    /**
     * \sa other
     */
    """
    assert messages_for(sa) == [r"use @see instead of \sa"]

    returns = r"""
    /**
     * \returns x
     */
    """
    assert messages_for(returns) == [r"use @return instead of \returns"]


def test_wrong_command_on_body_line() -> None:
    code = """
    /**
     * @returns x
     */
    """
    assert labels_for(code) == ["wrong-command"]


def test_body_line_commands_regression() -> None:
    # regression: these live on body lines of a multi-line block
    code = r"""
    /**
     * @returns bad
     * @throw ex
     * @sa other
     * \param y
     */
    """
    assert labels_for(code) == [
        "wrong-command",
        "wrong-command",
        "wrong-command",
        "backslash-command",
    ]


def test_command_on_closer_line() -> None:
    code = """
    /**
     * a
     * @sa b */
    """
    assert labels_for(code) == ["wrong-command", "content-on-closer"]


def test_no_double_count_across_opener_body_closer() -> None:
    code = """
    /** @returns opener
     * @throw body
     * @sa closer */
    """
    assert labels_for(code).count("wrong-command") == 3


def test_code_with_word_allowed() -> None:
    # @code{.cpp} is valid Doxygen and must not be flagged
    code = """
    /**
     * @code{.cpp}
     * int x;
     * @endcode
     */
    """
    assert findings_for(code) == []


# --- rendered message text ---------------------------------------------------


def test_message_uses_category_description() -> None:
    # a static category renders its default description
    code = "/// doc\n"
    assert messages_for(code) == ["use a /** ... */ block instead of ///"]


def test_message_detail_overrides() -> None:
    # dynamic categories render the offending text via Finding.detail
    backslash = r"""
    /**
     * \param y
     */
    """
    assert messages_for(backslash) == [r"use @param instead of \param"]

    wrong = """
    /**
     * @returns x
     */
    """
    assert messages_for(wrong) == ["use @return instead of @returns"]

    prose = """
    /**
     * Throws:
     */
    """
    assert messages_for(prose) == ['use @throws instead of prose "Throws:"']


# --- robustness --------------------------------------------------------------


def test_empty_file_no_crash() -> None:
    assert findings_for("") == []


def test_mid_line_plain_block_skipped() -> None:
    # a /* opened mid-line (after code) and spanning lines is skipped, so its
    # comment-like contents are not analyzed
    code = """
    int x = 0;  /* note: @returns is not a real tag here
     * @param also not real
     */
    int y = 0;
    """
    assert findings_for(code) == []


def test_unclosed_block_scanned_to_eof() -> None:
    # an unterminated /** block is still scanned to EOF (no crash, body checked)
    code = """
    /**
     * @returns x
    """
    assert labels_for(code) == ["wrong-command"]


def test_banner_and_empty_comment_not_flagged() -> None:
    code = """
    /***
     * banner
     ***/
    """
    assert findings_for(code) == []
    assert findings_for("/**/\n") == []


def main() -> int:
    tests = sorted(
        (name, fn)
        for name, fn in globals().items()
        if name.startswith("test_") and callable(fn)
    )
    failed = 0
    for name, fn in tests:
        try:
            fn()
            print(f"PASS {name}")
        except AssertionError as exc:
            failed += 1
            print(f"FAIL {name}: {exc!r}")
    print(f"\n{len(tests) - failed}/{len(tests)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""
Tests for fix_gtest_names.py.

Run directly (no test framework needed):
    ./bin/pre-commit/test_fix_gtest_names.py
or under pytest:
    pytest bin/pre-commit/test_fix_gtest_names.py
"""

import sys
import textwrap

from fix_gtest_names import convert, fix_source


def dedent(text: str) -> str:
    """Removes a fixture's common indentation and its leading newline.

    Lets fixtures be written as indented triple-quoted here-docs while keeping
    honest 1-based line numbers.
    """
    return textwrap.dedent(text).lstrip("\n")


def fixed(text: str) -> str:
    return fix_source(dedent(text))[0]


def reports(text: str) -> list[str]:
    return fix_source(dedent(text))[1]


# --- conversion --------------------------------------------------------------


def test_snake_case_conversion() -> None:
    assert convert("BadInputs") == "bad_inputs"
    assert convert("mulDiv") == "mul_div"
    assert convert("already_snake") == "already_snake"
    assert convert("base64") == "base64"


def test_acronyms_stay_whole() -> None:
    assert convert("SetAndResetAccountTxnID") == "set_and_reset_account_txn_id"
    assert convert("XRPToIOU") == "xrp_to_iou"
    assert convert("STAmountMath") == "st_amount_math"


def test_disabled_prefix_preserved() -> None:
    assert convert("DISABLED_FooBar") == "DISABLED_foo_bar"
    assert convert("DISABLED_foo_bar") == "DISABLED_foo_bar"
    assert convert("DISABLED_") == "DISABLED_"


# --- what counts as a test definition ---------------------------------------


def test_all_macros_recognized() -> None:
    code = """
    TEST(Suite, oneName)
    TEST_F(Fixture, twoName)
    TEST_P(Fixture, threeName)
    TYPED_TEST(Fixture, fourName)
    TYPED_TEST_P(Fixture, fiveName)
    """
    assert fixed(code) == dedent("""
        TEST(Suite, one_name)
        TEST_F(Fixture, two_name)
        TEST_P(Fixture, three_name)
        TYPED_TEST(Fixture, four_name)
        TYPED_TEST_P(Fixture, five_name)
        """)


def test_suite_and_fixture_names_untouched() -> None:
    code = """
    TEST(AccountSet, bad_inputs)
    TEST_F(MutexMakeTest, default_constructor)
    """
    assert reports(code) == []
    assert fixed(code) == dedent(code)


def test_lookalikes_ignored() -> None:
    code = """
    // TEST(Suite, notATest)
    TEST_EXPECT(someCall())
    TEST_EXPECTS(amount == value, amount.getText())
    INSTANTIATE_TEST_SUITE_P(Prefix, Fixture, testValues());
    auto x = TEST(Suite, notATest);
    TYPED_TEST_SUITE(Fixture, MyTypes);
    """
    assert reports(code) == []
    assert fixed(code) == dedent(code)


def test_indented_and_wrapped_definitions() -> None:
    code = """
    namespace ripple {
        TEST(Suite, indentedName)
    }
    TEST_F(
        SomeVeryLongFixtureName,
        wrappedName)
    """
    assert fixed(code) == dedent("""
        namespace ripple {
            TEST(Suite, indented_name)
        }
        TEST_F(
            SomeVeryLongFixtureName,
            wrapped_name)
        """)


# --- rewriting --------------------------------------------------------------


def test_only_the_name_is_rewritten() -> None:
    code = """
    TEST(SuiteName, mulDiv)
    {
        auto const mulDiv = 1;  // mulDiv stays
    }
    """
    assert fixed(code) == dedent("""
        TEST(SuiteName, mul_div)
        {
            auto const mulDiv = 1;  // mulDiv stays
        }
        """)


def test_reports_carry_line_numbers() -> None:
    code = """
    #include <foo.h>

    TEST(Suite, firstName)

    TEST(Suite, secondName)
    """
    assert reports(code) == [
        "3: renamed 'firstName' to 'first_name'",
        "5: renamed 'secondName' to 'second_name'",
    ]


# --- collisions -------------------------------------------------------------


def test_collision_reported_and_not_applied() -> None:
    # Both would define `Suite_mul_div_Test`.
    code = """
    TEST(Suite, mulDiv)
    TEST(Suite, mul_div)
    """
    assert reports(code) == [
        "1: cannot rename 'mulDiv' to 'mul_div': another test case in suite "
        "'Suite' already generates that name"
    ]
    assert fixed(code) == dedent(code)


def test_collision_across_generated_class_names() -> None:
    # `Suite_a_b_c_Test` from both spellings.
    code = """
    TEST(Suite_a, bC)
    TEST(Suite, a_b_c)
    """
    assert len(reports(code)) == 1
    assert "cannot rename 'bC'" in reports(code)[0]
    assert fixed(code) == dedent(code)


def test_same_name_in_different_suites_is_not_a_collision() -> None:
    code = """
    TEST(SuiteOne, mulDiv)
    TEST(SuiteTwo, mulDiv)
    """
    assert fixed(code) == dedent("""
        TEST(SuiteOne, mul_div)
        TEST(SuiteTwo, mul_div)
        """)


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

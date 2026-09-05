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

from fix_gtest_names import camel_case, fix_source, snake_case


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
    assert snake_case("BadInputs") == "bad_inputs"
    assert snake_case("mulDiv") == "mul_div"
    assert snake_case("already_snake") == "already_snake"
    assert snake_case("base64") == "base64"


def test_snake_case_keeps_acronyms_whole() -> None:
    assert snake_case("SetAndResetAccountTxnID") == "set_and_reset_account_txn_id"
    assert snake_case("XRPToIOU") == "xrp_to_iou"
    assert snake_case("STAmountMath") == "st_amount_math"


def test_camel_case_conversion() -> None:
    assert camel_case("json_value") == "JsonValue"
    assert camel_case("mulDiv") == "MulDiv"
    assert camel_case("scope") == "Scope"
    assert camel_case("base64") == "Base64"


def test_camel_case_leaves_acronyms_alone() -> None:
    # `inflection.camelize` would give `ShaMapTest` / `ParseStatmRsSkB` here.
    assert camel_case("SHAMapTest") == "SHAMapTest"
    assert camel_case("parseStatmRSSkB") == "ParseStatmRSSkB"
    assert camel_case("XRPAmount") == "XRPAmount"
    assert camel_case("CSPRNG") == "CSPRNG"


def test_disabled_prefix_preserved() -> None:
    assert snake_case("DISABLED_FooBar") == "DISABLED_foo_bar"
    assert snake_case("DISABLED_foo_bar") == "DISABLED_foo_bar"
    assert snake_case("DISABLED_") == "DISABLED_"
    assert camel_case("DISABLED_foo_bar") == "DISABLED_FooBar"
    assert camel_case("DISABLED_") == "DISABLED_"


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


def test_conforming_definitions_untouched() -> None:
    code = """
    TEST(AccountSet, bad_inputs)
    TEST_F(MutexMakeTest, default_constructor)
    TEST(SHAMap, DISABLED_slow_path)
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


def test_only_the_two_names_are_rewritten() -> None:
    code = """
    TEST(mulDiv, mulDiv)
    {
        auto const mulDiv = 1;  // mulDiv stays
    }
    """
    assert fixed(code) == dedent("""
        TEST(MulDiv, mul_div)
        {
            auto const mulDiv = 1;  // mulDiv stays
        }
        """)


def test_suite_name_camel_cased() -> None:
    code = """
    TEST(json_value, limits)
    TEST(scope, ScopeExit)
    """
    assert reports(code) == [
        "1: renamed suite 'json_value' to 'JsonValue'",
        "2: renamed suite 'scope' to 'Scope'",
        "2: renamed test case 'ScopeExit' to 'scope_exit'",
    ]
    assert fixed(code) == dedent("""
        TEST(JsonValue, limits)
        TEST(Scope, scope_exit)
        """)


def test_fixture_reported_but_not_renamed() -> None:
    # The first argument names a class, so only a human (or clang-tidy) can
    # rename it; the test-case name is still fixed.
    code = """
    TEST_F(my_fixture, someTest)
    """
    assert reports(code) == [
        "1: fixture 'my_fixture' is not CamelCase: rename the class to "
        "'MyFixture' by hand",
        "1: renamed test case 'someTest' to 'some_test'",
    ]
    assert fixed(code) == dedent("""
        TEST_F(my_fixture, some_test)
        """)


def test_reports_carry_line_numbers() -> None:
    code = """
    #include <foo.h>

    TEST(Suite, firstName)

    TEST(Suite, secondName)
    """
    assert reports(code) == [
        "3: renamed test case 'firstName' to 'first_name'",
        "5: renamed test case 'secondName' to 'second_name'",
    ]


# --- collisions -------------------------------------------------------------


def test_collision_reported_and_not_applied() -> None:
    # Both would define `Suite_mul_div_Test`.
    code = """
    TEST(Suite, mulDiv)
    TEST(Suite, mul_div)
    """
    assert reports(code) == [
        "1: cannot rename 'Suite, mulDiv' to 'Suite, mul_div': another test "
        "case already generates that name"
    ]
    assert fixed(code) == dedent(code)


def test_collision_between_converging_suites() -> None:
    # Both suites camel-case to `SuiteA`, so both would define
    # `SuiteA_one_test_Test`.
    code = """
    TEST(SuiteA, oneTest)
    TEST(Suite_a, one_test)
    """
    assert [r.split(":")[1].strip() for r in reports(code)] == [
        "cannot rename 'SuiteA, oneTest' to 'SuiteA, one_test'",
        "cannot rename 'Suite_a, one_test' to 'SuiteA, one_test'",
    ]
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

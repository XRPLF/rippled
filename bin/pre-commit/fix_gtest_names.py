#!/usr/bin/env python3

"""
Rewrites gtest names to the required style in this project: the suite name is
CamelCase, the test-case name is snake_case.

    TEST(SuiteName, test_case_name)

The gtest `DISABLED_` prefix is kept verbatim on either name.

Test-case names are converted with `inflection`, which folds acronyms the way a
reader expects: `SetAndResetAccountTxnID` -> `set_and_reset_account_txn_id`, not
`set_and_reset_account_txn_i_d`.

The first argument of `TEST_F`, `TEST_P`, `TYPED_TEST` and `TYPED_TEST_P` is a
fixture class rather than a free identifier, so rewriting it here would leave
the class it names behind. Those are reported for a human to rename (clang-tidy
checks the class declaration itself, via readability-identifier-naming).

Usage: ./bin/pre-commit/fix_gtest_names.py <file1> <file2> ...
"""

import re
import sys
from collections import Counter
from pathlib import Path

import inflection

# A test-case definition, `MACRO(SuiteOrFixture, TestName)`, anchored at the
# start of a line so that commented-out definitions and project macros that
# merely look similar (`TEST_EXPECT(...)`) are left alone. The `\s*` between
# arguments allows for a definition clang-format wrapped over several lines.
PATTERN = re.compile(
    r"(?P<head>^[ \t]*(?P<macro>TYPED_TEST_P|TYPED_TEST|TEST_F|TEST_P|TEST)\s*\(\s*)"
    r"(?P<suite>\w+)(?P<mid>\s*,\s*)(?P<name>\w+)(?P<tail>\s*\))",
    re.MULTILINE,
)

# The macros whose first argument names a fixture class, not a free identifier.
FIXTURE_MACROS = ("TEST_F", "TEST_P", "TYPED_TEST", "TYPED_TEST_P")

DISABLED = "DISABLED_"


def _split_disabled(name: str) -> tuple[str, str]:
    """Splits off gtest's `DISABLED_` prefix, which is kept verbatim."""
    if name.startswith(DISABLED):
        return DISABLED, name[len(DISABLED) :]
    return "", name


def snake_case(name: str) -> str:
    """Returns the name in snake_case."""
    prefix, core = _split_disabled(name)
    return prefix + inflection.underscore(core)


def camel_case(name: str) -> str:
    """Returns the name in CamelCase, capitalizing each underscored word.

    Only the letters that have to change are touched, so acronyms survive:
    `inflection.camelize` would turn `SHAMapTest` into `ShaMapTest`, whereas
    here it is already CamelCase and stays put. `json_value` -> `JsonValue`,
    `parseStatmRSSkB` -> `ParseStatmRSSkB`.
    """
    prefix, core = _split_disabled(name)
    return prefix + "".join(w[:1].upper() + w[1:] for w in core.split("_") if w)


def _corrected(match: re.Match) -> tuple[str, str]:
    """Returns the suite and test-case names this definition should end up with."""
    suite = match["suite"]
    return (
        suite if match["macro"] in FIXTURE_MACROS else camel_case(suite),
        snake_case(match["name"]),
    )


def fix_source(text: str) -> tuple[str, list[str]]:
    """Returns the corrected text and one `line: message` report per bad name."""
    # gtest joins the suite and test names into one class name, so two test
    # cases whose joined names agree cannot coexist: `TEST(a, b_c)` and
    # `TEST(a_b, c)` both define `a_b_c_Test`. A rename that would introduce
    # such a clash is reported for a human instead of applied.
    joined = Counter("_".join(_corrected(m)) for m in PATTERN.finditer(text))
    reports = []

    def rewrite(match: re.Match) -> str:
        suite, name = match["suite"], match["name"]
        new_suite, new_name = _corrected(match)
        line = text.count("\n", 0, match.start()) + 1

        if match["macro"] in FIXTURE_MACROS and camel_case(suite) != suite:
            reports.append(
                f"{line}: fixture '{suite}' is not CamelCase: rename the class "
                f"to '{camel_case(suite)}' by hand"
            )
        if (new_suite, new_name) == (suite, name):
            return match[0]
        if joined[f"{new_suite}_{new_name}"] > 1:
            reports.append(
                f"{line}: cannot rename '{suite}, {name}' to '{new_suite}, "
                f"{new_name}': another test case already generates that name"
            )
            return match[0]
        if new_suite != suite:
            reports.append(f"{line}: renamed suite '{suite}' to '{new_suite}'")
        if new_name != name:
            reports.append(f"{line}: renamed test case '{name}' to '{new_name}'")
        return match["head"] + new_suite + match["mid"] + new_name + match["tail"]

    return PATTERN.sub(rewrite, text), reports


def fix_names(path: Path) -> bool:
    """Corrects one file's gtest names, reporting each on stdout."""
    original = path.read_text(encoding="utf-8")
    fixed, reports = fix_source(original)
    for report in reports:
        print(f"{path}:{report}")
    if fixed != original:
        path.write_text(fixed, encoding="utf-8")
    return not reports


def main() -> int:
    files = [Path(f) for f in sys.argv[1:]]
    success = True

    for path in files:
        success &= fix_names(path)

    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())

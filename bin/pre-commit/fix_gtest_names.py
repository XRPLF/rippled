#!/usr/bin/env python3

"""
Rewrites gtest test-case names to snake_case, which is the required style in
this project.

Only the test-case name (the second macro argument) is touched: the first names
a fixture class or mirrors the type under test, so it follows the project's
type-naming style instead. The gtest `DISABLED_` prefix is kept verbatim.

Names are converted with `inflection`, which folds acronyms the way a reader
expects: `SetAndResetAccountTxnID` -> `set_and_reset_account_txn_id`, not
`set_and_reset_account_txn_i_d`.

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
    r"(?P<head>^[ \t]*(?:TEST|TEST_F|TEST_P|TYPED_TEST|TYPED_TEST_P)\s*\(\s*"
    r"(?P<suite>\w+)\s*,\s*)(?P<name>\w+)(?P<tail>\s*\))",
    re.MULTILINE,
)

DISABLED = "DISABLED_"


def convert(name: str) -> str:
    """Returns the name in snake_case, keeping any `DISABLED_` prefix."""
    prefix = DISABLED if name.startswith(DISABLED) else ""
    return prefix + inflection.underscore(name[len(prefix) :])


def fix_source(text: str) -> tuple[str, list[str]]:
    """Returns the corrected text and one `line: message` report per bad name."""
    # gtest joins the suite and test names into one class name, so two test
    # cases whose joined names agree cannot coexist: `TEST(a, b_c)` and
    # `TEST(a_b, c)` both define `a_b_c_Test`. A rename that would introduce
    # such a clash is reported for a human instead of applied.
    joined = Counter(
        f"{m['suite']}_{convert(m['name'])}" for m in PATTERN.finditer(text)
    )
    reports = []

    def rewrite(match: re.Match) -> str:
        old = match["name"]
        new = convert(old)
        if new == old:
            return match[0]
        line = text.count("\n", 0, match.start()) + 1
        if joined[f"{match['suite']}_{new}"] > 1:
            reports.append(
                f"{line}: cannot rename '{old}' to '{new}': another test case in "
                f"suite '{match['suite']}' already generates that name"
            )
            return match[0]
        reports.append(f"{line}: renamed '{old}' to '{new}'")
        return match["head"] + new + match["tail"]

    return PATTERN.sub(rewrite, text), reports


def fix_names(path: Path) -> bool:
    """Corrects one file's test-case names, reporting each on stdout."""
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

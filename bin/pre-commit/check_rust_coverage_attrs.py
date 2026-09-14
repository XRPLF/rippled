#!/usr/bin/env python3
"""
Check that Rust unit tests stay out of the coverage report.

cargo-llvm-cov instruments the test code along with everything else, so a test
module that is not excluded counts its own body as covered and inflates the
reported number. Excluding it takes two attributes:

  * every `#[cfg(test)]` module carries
    `#[cfg_attr(coverage_nightly, coverage(off))]`;
  * every crate root (lib.rs, main.rs) carries
    `#![cfg_attr(coverage_nightly, feature(coverage_attribute))]`, which the
    attribute above needs in order to compile.

Both are inert outside the coverage job: cargo-llvm-cov defines
`coverage_nightly` only when it runs on a nightly toolchain.

The crate-root gate is checked even in a crate that has no tests yet, because
that is what lets the first test module added later carry the attribute without
a build failure. Missing it is a hard error, so it cannot go unnoticed; a
missing `coverage(off)` fails open, which is why this check exists.

Matching is on exact attribute text, which works because `cargo fmt` runs over
the whole workspace in the hook ahead of this one: rustfmt puts every attribute
on its own line and normalizes what is inside it, turning `#[cfg( test )]`
and `#[cfg(test,)]` alike into `#[cfg(test)]`. So there is nothing here that
parses Rust. The price is that a cfg this file does not spell out literally --
`all(test, ...)`, `any(test, ...)`, `not(test)` -- is reported rather than
classified, on the grounds that guessing at coverage semantics is how a check
like this ends up quietly wrong.

Usage: ./bin/pre-commit/check_rust_coverage_attrs.py <file1> <file2> ...

Exit status is non-zero if any violation is found.
"""

import re
import sys
from dataclasses import dataclass
from pathlib import Path

CRATE_ROOTS = {"lib.rs", "main.rs"}

FEATURE_ATTR = "#![cfg_attr(coverage_nightly, feature(coverage_attribute))]"
COVERAGE_OFF_ATTR = "#[cfg_attr(coverage_nightly, coverage(off))]"
CFG_TEST_ATTR = "#[cfg(test)]"

# Any other cfg that mentions `test`. String literals are blanked before this
# runs, so `feature = "test"` does not read as the `test` cfg.
RE_CFG_MENTIONS_TEST = re.compile(r"^#\[cfg\(.*\btest\b.*\)\]$")
RE_STRING = re.compile(r'"(?:[^"\\]|\\.)*"')
RE_MOD = re.compile(r"^(?:pub(?:\([^)]*\))?\s+)?mod\s+([A-Za-z_]\w*)")


@dataclass(frozen=True)
class Finding:
    line: int
    label: str
    message: str


def _check_module(attrs: list[str], line: int, name: str) -> list[Finding]:
    """Findings for one module, given the attributes attached to it."""
    if COVERAGE_OFF_ATTR in attrs:
        return []  # excluded from coverage; which cfg gates it does not matter
    if CFG_TEST_ATTR in attrs:
        return [
            Finding(
                line,
                "missing-coverage-off",
                f"`mod {name}` is #[cfg(test)] but not excluded from coverage; "
                f"add {COVERAGE_OFF_ATTR}",
            )
        ]
    unclassified = [
        attr for attr in attrs if RE_CFG_MENTIONS_TEST.match(RE_STRING.sub('""', attr))
    ]
    if unclassified:
        return [
            Finding(
                line,
                "unclassified-cfg",
                f"`mod {name}` is gated on {unclassified[0]}, which this check "
                f"cannot tell apart from a module that ships in the library; "
                f"add {COVERAGE_OFF_ATTR} if it is test-only, or teach this "
                f"check the cfg if it is not",
            )
        ]
    return []


def _check_test_modules(lines: list[str]) -> list[Finding]:
    """Findings for every test module that is not excluded from coverage."""
    findings: list[Finding] = []
    attrs: list[str] = []
    attrs_line = 0
    for number, raw in enumerate(lines, start=1):
        stripped = raw.strip()
        # Blank lines and comments are allowed between an attribute and its item.
        if not stripped or stripped.startswith("//"):
            continue
        if stripped.startswith("#["):
            if not attrs:
                attrs_line = number
            attrs.append(stripped)
            continue
        module = RE_MOD.match(stripped)
        if module is not None and attrs:
            findings += _check_module(attrs, attrs_line, module.group(1))
        attrs = []
    return findings


def _check_crate_root(name: str, lines: list[str]) -> list[Finding]:
    """A finding if a crate root is missing the coverage_attribute feature gate."""
    if name not in CRATE_ROOTS:
        return []
    if any(line.strip() == FEATURE_ATTR for line in lines):
        return []
    return [
        Finding(
            1,
            "missing-feature-gate",
            f"crate root is missing {FEATURE_ATTR}",
        )
    ]


def check_source(name: str, text: str) -> list[Finding]:
    """Findings for one file's contents; `name` is its base name (lib.rs, ...)."""
    lines = text.splitlines()
    return _check_crate_root(name, lines) + _check_test_modules(lines)


def check_file(path: Path) -> list[Finding]:
    return check_source(path.name, path.read_text(encoding="utf-8"))


def main() -> int:
    total = 0
    for path in (Path(name) for name in sys.argv[1:]):
        for finding in check_file(path):
            total += 1
            print(f"{path}:{finding.line}: {finding.label}: {finding.message}")
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())

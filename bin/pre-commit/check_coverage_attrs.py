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

Usage: ./bin/pre-commit/check_coverage_attrs.py <file1> <file2> ...

Exit status is non-zero if any violation is found.
"""

import re
import sys
from dataclasses import dataclass
from pathlib import Path

CRATE_ROOTS = {"lib.rs", "main.rs"}

FEATURE_ATTR = "#![cfg_attr(coverage_nightly, feature(coverage_attribute))]"
COVERAGE_OFF_ATTR = "#[cfg_attr(coverage_nightly, coverage(off))]"

# A cfg that holds only under `cargo test`: bare `test`, or `test` within an
# all(...). `any(test, ...)` also holds outside a test build, so a module gated
# that way ships in the library and its coverage is not this check's business.
RE_CFG_TEST = re.compile(r"^#\[cfg\((?:test|all\([^)]*\btest\b[^)]*\))\)\]$")
RE_MOD = re.compile(r"^(?:pub(?:\([^)]*\))?\s+)?mod\s+([A-Za-z_]\w*)")


@dataclass(frozen=True)
class Finding:
    line: int
    label: str
    message: str


def _split_leading_attrs(line: str) -> tuple[list[str], str]:
    """Split attributes written on an item's own line from the item itself.

    rustfmt puts each attribute on its own line, so this only matters for code
    that has not been formatted yet.

        "#[cfg(test)] #[cfg_attr(coverage_nightly, coverage(off))] mod tests {"
            -> (["#[cfg(test)]", "#[cfg_attr(...)]"], "mod tests {")
        "#[cfg(test)]"  -> (["#[cfg(test)]"], "")     attributes only
        "mod tests {"   -> ([], "mod tests {")        item only

    An empty remainder is what tells the caller the attributes belong to an item
    on a later line. Depth counting rather than the first "]" keeps an attribute
    with nested brackets intact -- "#[foo(bar = [1, 2])] mod x {" splits after
    the outer bracket. A line whose brackets never balance ("#[cfg(test") is
    returned unsplit rather than guessed at.
    """
    attrs: list[str] = []
    rest = line
    while rest.startswith("#["):
        depth = 0
        for index, char in enumerate(rest):
            if char == "[":
                depth += 1
            elif char == "]":
                depth -= 1
                if depth == 0:
                    attrs.append(rest[: index + 1])
                    rest = rest[index + 1 :].lstrip()
                    break
        else:
            break  # unbalanced brackets: leave the remainder alone
    return attrs, rest


def _check_test_modules(lines: list[str]) -> list[Finding]:
    """Findings for every cfg(test) module that is not excluded from coverage."""
    findings: list[Finding] = []
    pending: list[str] = []  # attributes seen on their own lines
    pending_line = 0
    for number, raw in enumerate(lines, start=1):
        stripped = raw.strip()
        # Blank lines and comments are allowed between an attribute and its item.
        if not stripped or stripped.startswith("//"):
            continue
        attrs, rest = _split_leading_attrs(stripped)
        if attrs and not pending:
            pending_line = number
        block = pending + attrs
        if not rest:
            pending = block
            continue
        pending = []
        module = RE_MOD.match(rest)
        if module is None or not any(RE_CFG_TEST.match(attr) for attr in block):
            continue
        if COVERAGE_OFF_ATTR not in block:
            findings.append(
                Finding(
                    pending_line or number,
                    "missing-coverage-off",
                    f"`mod {module.group(1)}` is #[cfg(test)] but not excluded "
                    f"from coverage; add {COVERAGE_OFF_ATTR}",
                )
            )
    return findings


def _check_crate_root(path: Path, lines: list[str]) -> list[Finding]:
    """A finding if a crate root is missing the coverage_attribute feature gate."""
    if path.name not in CRATE_ROOTS:
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


def check_file(path: Path) -> list[Finding]:
    lines = path.read_text(encoding="utf-8").splitlines()
    return _check_crate_root(path, lines) + _check_test_modules(lines)


def main() -> int:
    total = 0
    for path in (Path(name) for name in sys.argv[1:]):
        for finding in check_file(path):
            total += 1
            print(f"{path}:{finding.line}: {finding.label}: {finding.message}")
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""
Tests for build_pkg.py: the version a sanitizer package gets.

Run directly (no test framework needed):
    ./package/test_build_pkg.py
or under pytest:
    pytest package/test_build_pkg.py
"""

import sys

import build_pkg as b

COMMIT_HASH = "0123456789abcdef0123456789abcdef01234567"


def test_sanitizer_package_has_the_version_of_the_plain_package() -> None:
    # A Release build without a sanitizer reports the tag as it is. A sanitizer
    # build appends its name, after the commit hash if the tag has no metadata.
    for tag, channel in (
        ("3.5.0", "stable"),
        ("3.5.0-b1", "beta"),
        ("3.5.0-b1+hotfix1", "beta"),
        ("3.4.0-custom-1+fix2", "custom"),
        (f"{b.DEV_VERSION}+0123456", "develop"),
    ):
        for variant, name in b.SANITIZER_METADATA.items():
            appended = f".{name}" if "+" in tag else f"+{COMMIT_HASH}.{name}"
            version = b.without_sanitizer_metadata(tag + appended, variant, COMMIT_HASH)
            assert version == tag, (tag, variant)
            assert b.package_version(version, channel) == b.package_version(
                tag, channel
            ), (tag, variant)


def test_build_without_a_commit_hash() -> None:
    assert b.without_sanitizer_metadata("3.5.0+ASAN", "asan", "") == "3.5.0"


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

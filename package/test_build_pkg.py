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


def test_sanitizer_package_has_the_version_of_the_plain_package() -> None:
    # A Release build without a sanitizer reports the tag as it is. A sanitizer
    # build appends its name to the build metadata.
    for tag, channel in (
        ("3.5.0", "stable"),
        ("3.5.0-b1", "beta"),
        ("3.5.0-b1+hotfix1", "beta"),
        ("3.5.0-b1+ASAN", "beta"),
        ("3.5.0-b1+fix.TSAN", "beta"),
        ("3.5.0-b1+0123456789abcdef0123456789abcdef01234567", "beta"),
        ("3.4.0-custom-1+fix2", "custom"),
        (f"{b.DEV_VERSION}+0123456", "develop"),
    ):
        for variant, name in b.SANITIZER_METADATA.items():
            reported = tag + ("." if "+" in tag else "+") + name
            version = b.without_sanitizer_metadata(reported, variant)
            assert version == tag, (tag, variant)
            assert b.package_version(version, channel) == b.package_version(
                tag, channel
            ), (tag, variant)


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

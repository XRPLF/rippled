#!/usr/bin/env python3
"""
Tests for generate.py: the event rules, and invariants over the JSON files.

Run directly (no test framework needed):
    ./.github/scripts/strategy-matrix/test_generate.py
or under pytest:
    pytest .github/scripts/strategy-matrix/test_generate.py
"""

import sys
from pathlib import Path
from typing import Any

import generate as g

THIS_DIR = Path(__file__).parent.resolve()

LINUX = g.LinuxFile.load(THIS_DIR / "linux.json")
PLATFORMS = [g.PlatformFile.load(THIS_DIR / f) for f in ("macos.json", "windows.json")]
EVENTS = (*g.MATRIX_EVENTS, g.FULL_BUILD)


def build_names(event: str) -> list[str]:
    entries = g.expand_linux_matrix(LINUX, event)
    for pf in PLATFORMS:
        entries += g.expand_platform_matrix(pf, event)
    return [e.config_name for e in entries]


def linux_config(**overrides: Any) -> g.LinuxConfig:
    fields: dict[str, Any] = {
        "compiler": ["gcc"],
        "build_type": ["Debug"],
        "arch": ["amd64"],
        "events": [g.PUSH],
    }
    return g.LinuxConfig(**{**fields, **overrides})


def rejected(**overrides: Any) -> bool:
    try:
        linux_config(**overrides)
    except AssertionError:
        return True
    return False


# --- event rules --------------------------------------------------------------


def test_pull_request_labels() -> None:
    event = g.matrix_event
    assert event(g.PULL_REQUEST, [], g.BRANCH) == g.PULL_REQUEST
    assert event(g.PULL_REQUEST, ["Bug"], g.BRANCH) == g.PULL_REQUEST
    assert event(g.PULL_REQUEST, [g.READY_TO_MERGE_LABEL], g.BRANCH) == g.READY_TO_MERGE
    assert event(g.PULL_REQUEST, [g.FULL_BUILD_LABEL], g.BRANCH) == g.FULL_BUILD
    both = [g.READY_TO_MERGE_LABEL, g.FULL_BUILD_LABEL]
    assert event(g.PULL_REQUEST, both, g.BRANCH) == g.FULL_BUILD
    assert event(g.PULL_REQUEST, both[::-1], g.BRANCH) == g.FULL_BUILD


def test_other_events_ignore_labels() -> None:
    for event in g.GITHUB_EVENTS:
        if event != g.PULL_REQUEST:
            assert g.matrix_event(event, [g.FULL_BUILD_LABEL], g.BRANCH) == event


def test_push_of_a_tag() -> None:
    assert g.matrix_event(g.PUSH, [], g.TAG) == g.TAG
    assert g.matrix_event(g.PUSH, [], g.BRANCH) == g.PUSH


def test_events_list_gets_full_build() -> None:
    assert linux_config(events=[g.PUSH]).events == [g.PUSH, g.FULL_BUILD]
    assert g.PlatformConfig(build_type=["Release"], events=[g.TAG]).events == [
        g.TAG,
        g.FULL_BUILD,
    ]


def test_events_list_is_checked() -> None:
    assert rejected(events=[])
    assert rejected(events=["bogus"])
    assert rejected(events=[g.FULL_BUILD])
    assert rejected(events=[g.PUSH, g.FULL_BUILD])
    assert not rejected(events=list(g.MATRIX_EVENTS))


def test_packaged_config_must_list_tag() -> None:
    package = {"type": "deb", "image": "debian:trixie"}
    assert rejected(package=package, events=[g.PUSH])
    assert not rejected(package=package, events=[g.PUSH, g.TAG])
    assert rejected(package={**package, "type": "msi"}, events=[g.TAG])


# --- invariants over the JSON files -------------------------------------------


def test_config_names_are_unique() -> None:
    for event in EVENTS:
        names = build_names(event)
        assert len(names) == len(set(names)), event


def test_full_build_is_every_config() -> None:
    full = set(build_names(g.FULL_BUILD))
    union: set[str] = set()
    for event in g.MATRIX_EVENTS:
        names = set(build_names(event))
        assert names <= full, event
        union |= names
    assert union == full


def test_packaging_consumes_what_the_same_event_builds() -> None:
    for event in EVENTS:
        built = set(build_names(event))
        for entry in g.expand_linux_packaging(LINUX, event):
            assert entry.xrpld_artifact_name.removeprefix("xrpld-") in built, event
            assert (
                entry.validator_keys_artifact_name.removeprefix("validator-keys-")
                in built
            ), event
            assert entry.package_type in g.PACKAGE_TYPES


def test_tag_packages_every_packaged_config() -> None:
    tagged = g.expand_linux_packaging(LINUX, g.TAG)
    assert tagged
    assert len(tagged) == len(g.expand_linux_packaging(LINUX, g.FULL_BUILD))


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

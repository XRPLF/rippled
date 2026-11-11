#!/usr/bin/env python3
"""
Tests for generate.py: the event rules, and invariants over the JSON files.

Run directly (no test framework needed):
    ./.github/scripts/strategy-matrix/test_generate.py
or under pytest:
    pytest .github/scripts/strategy-matrix/test_generate.py
"""

import re
import sys
from typing import Any

import generate as g

LINUX = g.LinuxFile.load(g.config_file(g.LINUX))
PLATFORMS = [g.PlatformFile.load(g.config_file(p)) for p in (g.MACOS, g.WINDOWS)]
EVENTS = tuple(g.Event)
CONFIGS = [c for cfgs in LINUX.configs.values() for c in cfgs] + [
    c for pf in PLATFORMS for c in pf.configs
]
PACKAGE_WORKFLOW = g.THIS_DIR.parents[1] / "workflows" / "reusable-package.yml"
STAGE_TWO = (g.Event.READY_TO_MERGE, g.Event.MERGE_GROUP, g.Event.PUSH)


def build_names(event: g.Event) -> list[str]:
    entries = g.expand_linux_matrix(LINUX, event)
    for pf in PLATFORMS:
        entries += g.expand_platform_matrix(pf, event)
    return [e.config_name for e in entries]


def linux_config(**overrides: Any) -> g.LinuxConfig:
    fields: dict[str, Any] = {
        "compiler": ["gcc"],
        "build_type": ["Debug"],
        "arch": ["amd64"],
        "events": [g.Event.PUSH],
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
    assert event(g.Event.PULL_REQUEST, [], g.RefType.BRANCH) == g.Event.PULL_REQUEST
    assert (
        event(g.Event.PULL_REQUEST, ["Bug"], g.RefType.BRANCH) == g.Event.PULL_REQUEST
    )
    assert (
        event(g.Event.PULL_REQUEST, [g.READY_TO_MERGE_LABEL], g.RefType.BRANCH)
        == g.Event.READY_TO_MERGE
    )
    assert (
        event(g.Event.PULL_REQUEST, [g.MERGE_GROUP_LABEL], g.RefType.BRANCH)
        == g.Event.MERGE_GROUP
    )
    both = [g.READY_TO_MERGE_LABEL, g.MERGE_GROUP_LABEL]
    assert event(g.Event.PULL_REQUEST, both, g.RefType.BRANCH) == g.Event.MERGE_GROUP
    assert (
        event(g.Event.PULL_REQUEST, both[::-1], g.RefType.BRANCH) == g.Event.MERGE_GROUP
    )


def test_other_events_ignore_labels() -> None:
    for event in g.GITHUB_EVENTS:
        if event != g.Event.PULL_REQUEST:
            assert (
                g.matrix_event(event, [g.MERGE_GROUP_LABEL], g.RefType.BRANCH) == event
            )


def test_push_of_a_tag() -> None:
    assert g.matrix_event(g.Event.PUSH, [], g.RefType.TAG) == g.Event.TAG
    assert g.matrix_event(g.Event.PUSH, [], g.RefType.BRANCH) == g.Event.PUSH


def test_events_list_is_read_as_events() -> None:
    # A StrEnum member equals its value, so compare the types, not the lists.
    platform: dict[str, Any] = {"build_type": ["Release"], "events": ["tag"]}
    for cfg in (linux_config(events=["push"]), g.PlatformConfig(**platform)):
        assert [type(e) for e in cfg.events] == [g.Event], cfg


def test_events_list_is_checked() -> None:
    assert rejected(events=[])
    assert rejected(events=["bogus"])
    assert rejected(events=[g.Event.PUSH, "full_build"])
    assert not rejected(events=list(g.Event))


def test_packaged_config_must_list_tag() -> None:
    package = {"type": "deb", "image": "debian:trixie"}
    assert rejected(package=package, events=[g.Event.PUSH])
    assert not rejected(package=package, events=[g.Event.PUSH, g.Event.TAG])
    assert rejected(package={**package, "type": "msi"}, events=[g.Event.TAG])


def test_disabled_config_is_built_by_no_event() -> None:
    disabled = linux_config(events=list(g.Event), disabled="a reason")
    linux = g.LinuxFile(image_tag="tag", configs={"ubuntu": [disabled]})
    platform = g.PlatformFile(
        platform="macos/arm64",
        runner=[],
        configs=[g.PlatformConfig(build_type=["Release"], events=list(g.Event))],
    )
    platform.configs[0].disabled = "a reason"
    for event in g.Event:
        assert not g.expand_linux_matrix(linux, event), event
        assert not g.expand_platform_matrix(platform, event), event


def test_disabled_config_is_still_checked() -> None:
    assert rejected(events=[], disabled="a reason")
    assert rejected(disabled=True)
    assert rejected(disabled="")
    assert rejected(disabled="  ")
    assert not rejected(disabled=None)


def test_disabled_config_is_named_by_its_entries() -> None:
    linux = linux_config(compiler=["gcc", "clang"], disabled="a reason")
    assert g.entry_names("ubuntu", linux) == [
        "ubuntu-gcc-debug-amd64",
        "ubuntu-clang-debug-amd64",
    ]
    platform = g.PlatformConfig(
        build_type=["Release"], events=[g.Event.PUSH], toolchain="nix"
    )
    platform.disabled = "a reason"
    assert g.entry_names("macos/arm64", platform) == ["macos-arm64-release-nix"]


def test_packaged_config_has_one_sanitizer_at_most() -> None:
    package = {"type": "deb", "image": "debian:trixie"}
    assert not rejected(package=package, events=[g.Event.TAG], sanitizers=["address"])
    assert rejected(
        package=package, events=[g.Event.TAG], sanitizers=["address", "thread"]
    )


# --- invariants over the JSON files -------------------------------------------


def test_config_names_are_unique() -> None:
    for event in EVENTS:
        names = build_names(event)
        assert len(names) == len(set(names)), event


def test_schedule_and_dispatch_build_every_config() -> None:
    for cfg in (c for c in CONFIGS if not c.disabled):
        assert g.Event.SCHEDULE in cfg.events, cfg
        assert g.Event.WORKFLOW_DISPATCH in cfg.events, cfg


def test_pull_request_builds_part_of_stage_two() -> None:
    assert set(build_names(g.Event.PULL_REQUEST)) <= set(
        build_names(g.Event.MERGE_GROUP)
    )


def test_stage_two_events_build_the_same_matrix() -> None:
    for event in STAGE_TWO:
        assert set(build_names(event)) == set(build_names(g.Event.MERGE_GROUP)), event
        assert g.expand_linux_packaging(LINUX, event) == g.expand_linux_packaging(
            LINUX, g.Event.MERGE_GROUP
        ), event


def test_tag_builds_only_what_it_publishes() -> None:
    # Besides each packaged config's own artifacts, the packaging workflow
    # downloads some by a literal name, such as the voidstar binary.
    literal = set(
        re.findall(
            r"^\s*name: xrpld-([a-z0-9-]+)\s*$",
            PACKAGE_WORKFLOW.read_text(),
            re.MULTILINE,
        )
    )
    assert literal
    packaged = {
        e.xrpld_artifact_name.removeprefix("xrpld-")
        for e in g.expand_linux_packaging(LINUX, g.Event.TAG)
    }
    assert set(build_names(g.Event.TAG)) == packaged | literal


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
    tagged = g.expand_linux_packaging(LINUX, g.Event.TAG)
    assert tagged
    assert len(tagged) == len(
        g.expand_linux_packaging(LINUX, g.Event.WORKFLOW_DISPATCH)
    )


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

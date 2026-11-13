#!/usr/bin/env python3
import argparse
import dataclasses
import itertools
import json
import sys
from collections.abc import Sequence
from enum import StrEnum
from pathlib import Path

THIS_DIR = Path(__file__).parent.resolve()

# The platforms a matrix is generated for, each read from its own JSON file.
LINUX = "linux"
MACOS = "macos"
WINDOWS = "windows"
PLATFORMS = (LINUX, MACOS, WINDOWS)

# The architectures a Linux config can list.
AMD64 = "amd64"
ARM64 = "arm64"


def config_file(platform: str) -> Path:
    """Locate the JSON file a platform's configs are read from."""
    return THIS_DIR / f"{platform}.json"


_BASE_CMAKE_ARGS = [
    "-Dtests=ON",
    "-Dwerr=ON",
    "-Dxrpld=ON",
    "-Dwextra=ON",
    "-Drust=ON",
]

# The package formats a config can be packaged as, each with its own
# install-test job in reusable-package.yml.
PACKAGE_DEB = "deb"
PACKAGE_RPM = "rpm"
PACKAGE_TYPES = (PACKAGE_DEB, PACKAGE_RPM)

# The package name a variant suffixes, as build_pkg.py's BASE_NAME spells it:
# the two have to agree, or the artifact globs miss what was built.
BASE_NAME = "xrpld"

# Maps sanitizer names (as used in cmake) to short config-name suffixes.
_SANITIZER_SUFFIX: dict[str, str] = {
    "address": "asan",
    "undefinedbehavior": "ubsan",
    "thread": "tsan",
}


def config_name(
    distro: str,
    compiler: str,
    build_type: str,
    arch: str,
    suffix: str = "",
    sanitizer: str = "",
) -> str:
    """Name a config. Its artifacts are named after it, so packaging reuses this."""
    parts = [s for s in [suffix, _SANITIZER_SUFFIX.get(sanitizer, "")] if s]
    return "-".join([f"{distro}-{compiler}-{build_type.lower()}-{arch}", *parts])


def get_cmake_args(build_type: str, extra_args: str) -> str:
    """Get the full list of CMake arguments for a config."""
    args = _BASE_CMAKE_ARGS.copy()
    if extra_args:
        args.extend(extra_args.split())
    return " ".join(args)


class Event(StrEnum):
    """The matrix events."""

    # The GitHub events that reach reusable-strategy-matrix.yml.
    PULL_REQUEST = "pull_request"
    MERGE_GROUP = "merge_group"
    PUSH = "push"
    SCHEDULE = "schedule"
    WORKFLOW_DISPATCH = "workflow_dispatch"
    # The events derived from context: a labeled pull request and the push of a
    # tag.
    READY_TO_MERGE = "ready_to_merge"
    TAG = "tag"


class RefType(StrEnum):
    """What github.ref_type can be."""

    BRANCH = "branch"
    TAG = "tag"


GITHUB_EVENTS = (
    Event.PULL_REQUEST,
    Event.MERGE_GROUP,
    Event.PUSH,
    Event.SCHEDULE,
    Event.WORKFLOW_DISPATCH,
)

# The labels that select READY_TO_MERGE and MERGE_GROUP on a pull request.
READY_TO_MERGE_LABEL = "Ready to merge"
MERGE_GROUP_LABEL = "Merge CI"


def matrix_event(event: Event, labels: list[str], ref_type: RefType) -> Event:
    """Resolve a GitHub event, the pull request's labels and the ref type to a
    matrix event.

    If a pull request has both labels, "Merge CI" wins.
    """
    if event == Event.PULL_REQUEST:
        if MERGE_GROUP_LABEL in labels:
            return Event.MERGE_GROUP
        if READY_TO_MERGE_LABEL in labels:
            return Event.READY_TO_MERGE
    if event == Event.PUSH and ref_type == RefType.TAG:
        return Event.TAG
    return event


def _read_events(events: Sequence[str]) -> list[Event]:
    """Check a config's 'events' list and convert it to Event members."""
    assert events and all(
        e in Event for e in events
    ), f"unsupported events {events!r}: list one or more of {', '.join(Event)}."
    return [Event(e) for e in events]


def _check_disabled(disabled: object) -> None:
    """Check that a config's 'disabled' field, when present, is a reason."""
    assert disabled is None or (
        isinstance(disabled, str) and disabled.strip()
    ), f"'disabled' takes the reason the config is disabled, not {disabled!r}."


# ---------------------------------------------------------------------------
# Input types — shapes of the JSON config files
# ---------------------------------------------------------------------------


# Every config lists in 'events' the matrix events that build it, drawn from
# Event. No event implies another. Every config lists 'schedule' and
# 'workflow_dispatch', so the nightly run and a manual run build every config.
#
# Configs may also opt into 'benchmark' to smoke-run the benchmarks, or carry a
# 'package' map to be packaged as well. Note that either applies to every entry
# a config expands into, so only set them on configs that expand to a single
# combination.
#
# A config with a 'disabled' reason is built by no event until the field is
# removed. The reason says why, since JSON has no comments. Its other fields
# are still checked, so it can be enabled again by deleting that one line.


@dataclasses.dataclass
class PackageConfig:
    """The 'package' map of a config whose binaries are also packaged."""

    type: str  # Has to match what the image provides.
    # The packaging container image: a vanilla distro image, not the nix image
    # the config itself builds in.
    image: str
    # A flavour of the package, named xrpld-<variant>, for a config whose
    # binaries are not the plain release build. A variant needs no counterpart
    # in the other format.
    variant: str = ""

    def __post_init__(self) -> None:
        assert self.type in PACKAGE_TYPES, (
            f"unsupported package type {self.type!r}: "
            f"use one of {', '.join(PACKAGE_TYPES)}."
        )


@dataclasses.dataclass
class LinuxConfig:
    """One entry in a linux.json 'configs' array."""

    compiler: list[str]
    build_type: list[str]
    arch: list[str]
    events: list[Event]
    benchmark: bool = False  # If true, smoke-run the benchmarks after testing.
    sanitizers: list[str] = dataclasses.field(default_factory=list)
    suffix: str = ""
    extra_cmake_args: str = ""
    package: PackageConfig | None = None  # Set to also package this config.
    disabled: str | None = None  # Reason why no event builds this config.

    def sanitizer_entries(self) -> list[str]:
        """The sanitizer of each entry the config expands into: one per listed
        sanitizer, or a single entry without one, which the empty string marks.
        """
        return self.sanitizers or [""]

    def __post_init__(self) -> None:
        _check_disabled(self.disabled)
        if isinstance(self.package, dict):
            self.package = PackageConfig(**self.package)
        assert self.package is None or Event.TAG in self.events, (
            f"a config with a 'package' map must list the '{Event.TAG}' event; "
            f"got {self.events}."
        )
        self.events = _read_events(self.events)


@dataclasses.dataclass
class LinuxFile:
    """Shape of linux.json."""

    image_tag: str
    configs: dict[str, list[LinuxConfig]]  # Maps each distro to its configs.

    @classmethod
    def load(cls, path: Path) -> "LinuxFile":
        data = json.loads(path.read_text())
        return cls(
            image_tag=data["image_tag"],
            configs={
                distro: [LinuxConfig(**c) for c in cfgs]
                for distro, cfgs in data["configs"].items()
            },
        )


@dataclasses.dataclass
class PlatformConfig:
    """One entry in macos.json's or windows.json's 'configs' array."""

    build_type: list[str]
    events: list[Event]
    build_only: bool = False  # If true, skip tests (e.g. macOS/Windows Debug).
    benchmark: bool = False  # If true, smoke-run the benchmarks after testing.
    extra_cmake_args: str = ""
    # "" is the runner's system compiler, "nix" the flake's CI environment.
    # macOS only: Linux always builds in a Nix image, Windows has no Nix.
    toolchain: str = ""
    disabled: str | None = None  # Reason why no event builds this config.

    def __post_init__(self) -> None:
        _check_disabled(self.disabled)
        if isinstance(self.build_type, str):
            self.build_type = [self.build_type]
        self.events = _read_events(self.events)


@dataclasses.dataclass
class PlatformFile:
    """Shape of macos.json and windows.json."""

    platform: str  # For example "macos/arm64" or "windows/amd64".
    runner: list[str]  # GitHub Actions runner labels.
    configs: list[PlatformConfig]

    @classmethod
    def load(cls, path: Path) -> "PlatformFile":
        data = json.loads(path.read_text())
        return cls(
            platform=data["platform"],
            runner=data["runner"],
            configs=[PlatformConfig(**c) for c in data["configs"]],
        )


# ---------------------------------------------------------------------------
# Output types — shapes of the generated GitHub Actions matrix entries
# ---------------------------------------------------------------------------


@dataclasses.dataclass
class Architecture:
    platform: str
    runner: list[str]


@dataclasses.dataclass
class MatrixEntry:
    """One entry in the generated build/test strategy matrix."""

    config_name: str
    cmake_args: str
    cmake_target: str
    build_only: bool
    benchmark: bool
    build_type: str
    architecture: Architecture
    sanitizers: str
    image: str = ""  # Container image; empty for macOS/Windows (runs natively).
    compiler: str = ""  # Compiler name ("gcc" or "clang"); empty for macOS/Windows.
    toolchain: str = ""  # "nix" for the flake's CI environment; see PlatformConfig.


@dataclasses.dataclass
class PackagingEntry:
    """One entry in the generated packaging strategy matrix."""

    xrpld_artifact_name: str
    validator_keys_artifact_name: str
    image: str
    package_type: str  # One of PACKAGE_TYPES; drives the format-specific steps.
    package_variant: str  # Passed to build_pkg.py --variant; empty for xrpld.
    package_name: str  # The name it builds under, which the artifact globs use.


# ---------------------------------------------------------------------------
# Matrix expansion
# ---------------------------------------------------------------------------

_ARCHS: dict[str, Architecture] = {
    AMD64: Architecture(
        platform="linux/amd64", runner=["self-hosted", "Linux", "X64", "heavy"]
    ),
    ARM64: Architecture(
        platform="linux/arm64",
        runner=["self-hosted", "Linux", "ARM64", "heavy-arm64"],
    ),
}


def expand_linux_matrix(linux: LinuxFile, event: Event) -> list[MatrixEntry]:
    """Expand a LinuxFile into the flat matrix 'event' builds.

    Each config entry is expanded over the cross-product of its compiler,
    build_type, sanitizers, and architecture lists. A config is included when
    its 'events' list contains 'event'.
    """
    entries: list[MatrixEntry] = []

    for distro, configs in linux.configs.items():
        for cfg in configs:
            if cfg.disabled or event not in cfg.events:
                continue
            effective_archs = {arch: _ARCHS[arch] for arch in cfg.arch}

            for compiler, build_type, sanitizer, (arch, arch_info) in itertools.product(
                cfg.compiler,
                cfg.build_type,
                cfg.sanitizer_entries(),
                effective_archs.items(),
            ):
                name = config_name(
                    distro, compiler, build_type, arch, cfg.suffix, sanitizer
                )
                entries.append(
                    MatrixEntry(
                        config_name=name,
                        image=f"ghcr.io/xrplf/xrpld/nix-{distro}:{linux.image_tag}",
                        cmake_args=get_cmake_args(build_type, cfg.extra_cmake_args),
                        cmake_target="all",
                        build_only=False,
                        benchmark=cfg.benchmark,
                        build_type=build_type,
                        architecture=arch_info,
                        sanitizers=sanitizer,
                        compiler=compiler,
                    )
                )

    return entries


def expand_linux_packaging(linux: LinuxFile, event: Event) -> list[PackagingEntry]:
    """Generate the packaging matrix from the configs that carry a 'package' map
    and list 'event'.

    Packaging consumes the binaries that config's build job uploaded, so the
    artifact names come from the same config name, and a packaged config is one
    that passes -Dvalidator_keys=ON.

    Packaging itself runs in vanilla distro images (debian:trixie, almalinux:10)
    instead of the nix-based build images, because deb/rpm tooling (debhelper,
    rpm-build) is taken from the distro's archive rather than from nixpkgs.
    """
    entries = []
    for distro, configs in linux.configs.items():
        for cfg in configs:
            if cfg.disabled or cfg.package is None or event not in cfg.events:
                continue
            for compiler, build_type, arch in itertools.product(
                cfg.compiler, cfg.build_type, cfg.arch
            ):
                # The packaging workflow hardcodes an amd64 runner.
                assert arch == AMD64, f"cannot package {distro} on {arch}"
                name = config_name(distro, compiler, build_type, arch, cfg.suffix)
                entries.append(
                    PackagingEntry(
                        xrpld_artifact_name=f"xrpld-{name}",
                        validator_keys_artifact_name=f"validator-keys-{name}",
                        image=cfg.package.image,
                        package_type=cfg.package.type,
                        package_variant=cfg.package.variant,
                        package_name=(
                            f"{BASE_NAME}-{cfg.package.variant}"
                            if cfg.package.variant
                            else BASE_NAME
                        ),
                    )
                )

    return entries


def package_names_by_type(entries: list[PackagingEntry]) -> dict[str, list[str]]:
    """The names of the packages in 'entries', keyed by format.

    Derived from the packaging matrix rather than listed again, so the packages
    the install-test jobs look for are the packages that were built.
    """
    return {
        package_type: sorted(
            {e.package_name for e in entries if e.package_type == package_type}
        )
        for package_type in PACKAGE_TYPES
    }


def expand_platform_matrix(pf: PlatformFile, event: Event) -> list[MatrixEntry]:
    """Expand a PlatformFile (macOS or Windows) into the matrix 'event' builds.

    A config is included when its 'events' list contains 'event'.
    """
    platform_name, arch = pf.platform.split("/")
    is_windows = platform_name == WINDOWS

    entries: list[MatrixEntry] = []
    for cfg in pf.configs:
        if cfg.disabled or event not in cfg.events:
            continue
        for build_type in cfg.build_type:
            name = f"{platform_name}-{arch}-{build_type.lower()}"
            if cfg.toolchain:
                name += f"-{cfg.toolchain}"
            entries.append(
                MatrixEntry(
                    config_name=name,
                    cmake_args=get_cmake_args(build_type, cfg.extra_cmake_args),
                    cmake_target="install" if is_windows else "all",
                    build_only=cfg.build_only,
                    benchmark=cfg.benchmark,
                    build_type=build_type,
                    architecture=Architecture(platform=pf.platform, runner=pf.runner),
                    sanitizers="",
                    toolchain=cfg.toolchain,
                )
            )
    return entries


def entry_names(where: str, cfg: LinuxConfig | PlatformConfig) -> list[str]:
    """Name the matrix entries a config expands into, whether or not it is
    disabled, so a disabled one is reported under the job names CI shows. The
    config is found under 'where': the distro of a Linux config, or the platform
    of another one.
    """
    enabled = dataclasses.replace(cfg, disabled=None)
    entries: list[MatrixEntry]
    if isinstance(enabled, LinuxConfig):
        linux = LinuxFile(image_tag="", configs={where: [enabled]})
        entries = expand_linux_matrix(linux, enabled.events[0])
    else:
        pf = PlatformFile(platform=where, runner=[], configs=[enabled])
        entries = expand_platform_matrix(pf, enabled.events[0])
    return [e.config_name for e in entries]


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Generate a CI strategy matrix for all platforms or a specific one."
    )
    parser.add_argument(
        "-c",
        "--config",
        help="Platform to generate for ('linux', 'macos', or 'windows'). Defaults to all platforms.",
        choices=PLATFORMS,
        default=None,
    )
    parser.add_argument(
        "-p",
        "--packaging",
        help="Emit the Linux packaging matrix instead of the build/test matrix.",
        action="store_true",
    )
    parser.add_argument(
        "--event",
        help="The GitHub event that triggered the run (github.event_name).",
        type=Event,
        choices=GITHUB_EVENTS,
        required=True,
    )
    parser.add_argument(
        "--labels",
        help="The pull request's label names as a JSON array "
        "(github.event.pull_request.labels.*.name). Empty means none.",
        default="",
    )
    parser.add_argument(
        "--ref-type",
        help="The type of the ref the run is for (github.ref_type).",
        type=RefType,
        choices=list(RefType),
        required=True,
    )
    args = parser.parse_args()

    labels = json.loads(args.labels) if args.labels else []
    event = matrix_event(args.event, labels, args.ref_type)

    entries: list[MatrixEntry] | list[PackagingEntry]
    if args.packaging:
        entries = expand_linux_packaging(LinuxFile.load(config_file(LINUX)), event)
        # One list per format, so each install-test job installs the packages its
        # own format produced.
        for package_type, names in package_names_by_type(entries).items():
            print(f"{package_type}_package_names={json.dumps(names)}")
        # An unlabeled pull request packages nothing, and GitHub Actions fails a
        # job whose strategy matrix has no entries, so the package job reads this
        # count and skips on zero.
        print(f"count={len(entries)}")
    else:
        matrix: list[MatrixEntry] = []
        configs: list[tuple[str, LinuxConfig | PlatformConfig]] = []
        if args.config in (LINUX, None):
            linux = LinuxFile.load(config_file(LINUX))
            matrix += expand_linux_matrix(linux, event)
            configs += [(d, c) for d, cfgs in linux.configs.items() for c in cfgs]
        for platform in (MACOS, WINDOWS):
            if args.config in (platform, None):
                pf = PlatformFile.load(config_file(platform))
                matrix += expand_platform_matrix(pf, event)
                configs += [(pf.platform, c) for c in pf.configs]
        # Every event builds something, and GitHub Actions fails a job whose
        # strategy matrix has no entries.
        where = f"{args.config} config" if args.config else "config"
        assert matrix, f"no {where} builds on '{event}'."
        entries = matrix
        # Standard output goes to ${GITHUB_OUTPUT}, so the annotations go to
        # standard error, where the runner reads workflow commands as well.
        for where, cfg in configs:
            if cfg.disabled:
                print(
                    "::warning title=Disabled CI config::"
                    f"{', '.join(entry_names(where, cfg))}: {cfg.disabled}",
                    file=sys.stderr,
                )

    print(f"matrix={json.dumps({'include': [dataclasses.asdict(e) for e in entries]})}")

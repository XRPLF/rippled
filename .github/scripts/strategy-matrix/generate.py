#!/usr/bin/env python3
import argparse
import dataclasses
import itertools
import json
from pathlib import Path

THIS_DIR = Path(__file__).parent.resolve()

_BASE_CMAKE_ARGS = ["-Dtests=ON", "-Dwerr=ON", "-Dxrpld=ON", "-Dwextra=ON"]

# Maps sanitizer names (as used in cmake) to short config-name suffixes.
_SANITIZER_SUFFIX: dict[str, str] = {
    "address": "asan",
    "undefinedbehavior": "ubsan",
    "thread": "tsan",
}


def get_cmake_args(build_type: str, extra_args: str) -> str:
    """Get the full list of CMake arguments for a config."""
    args = _BASE_CMAKE_ARGS.copy()
    if extra_args:
        args.extend(extra_args.split())
    return " ".join(args)


def runs_on_event(exclude_event_types: list[str], event: str | None) -> bool:
    """Whether a config should run for the current event.

    'exclude_event_types' is a list of GitHub event names (e.g.
    ["pull_request"]) on which the config should NOT run; an empty list means
    the config runs on every event. When no event is given (event is None), no
    filtering is applied.
    """
    if event is None:
        return True
    return event not in exclude_event_types


# ---------------------------------------------------------------------------
# Input types — shapes of the JSON config files
# ---------------------------------------------------------------------------


@dataclasses.dataclass
class LinuxConfig:
    """One entry in linux.json's 'configs' or 'package_configs' arrays."""

    compiler: list[str]
    build_type: list[str]
    arch: list[str]
    sanitizers: list[str] = dataclasses.field(default_factory=list)
    suffix: str = ""
    extra_cmake_args: str = ""
    image: str = ""  # only used by package_configs entries
    # List of GitHub event names (e.g. "pull_request") on which this config
    # should NOT run. Empty means it runs on every event.
    exclude_event_types: list[str] = dataclasses.field(default_factory=list)


@dataclasses.dataclass
class LinuxFile:
    """Shape of linux.json."""

    image_tag: str
    configs: dict[str, list[LinuxConfig]]  # distro → configs
    package_configs: dict[str, list[LinuxConfig]]  # distro → packaging configs

    @classmethod
    def load(cls, path: Path) -> "LinuxFile":
        data = json.loads(path.read_text())

        def parse(section: dict) -> dict[str, list[LinuxConfig]]:
            return {
                distro: [LinuxConfig(**c) for c in cfgs]
                for distro, cfgs in section.items()
            }

        return cls(
            image_tag=data["image_tag"],
            configs=parse(data["configs"]),
            package_configs=parse(data.get("package_configs", {})),
        )


@dataclasses.dataclass
class PlatformConfig:
    """One entry in macos.json's or windows.json's 'configs' array."""

    build_type: list[str]
    build_only: bool = False  # if true, skip tests (e.g. macos/Windows Debug)
    extra_cmake_args: str = ""
    # List of GitHub event names (e.g. "pull_request") on which this config
    # should NOT run. Empty means it runs on every event.
    exclude_event_types: list[str] = dataclasses.field(default_factory=list)

    def __post_init__(self) -> None:
        if isinstance(self.build_type, str):
            self.build_type = [self.build_type]


@dataclasses.dataclass
class PlatformFile:
    """Shape of macos.json and windows.json."""

    platform: str  # e.g. "macos/arm64" or "windows/amd64"
    runner: list[str]  # GitHub Actions runner labels
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
    build_type: str
    architecture: Architecture
    sanitizers: str
    image: str = ""  # container image; empty for macOS/Windows (runs natively)
    compiler: str = ""  # compiler name ("gcc" or "clang"); empty for macOS/Windows


@dataclasses.dataclass
class PackagingEntry:
    """One entry in the generated packaging strategy matrix."""

    artifact_name: str
    image: str
    distro: str  # e.g. "debian" or "rhel"; drives package-format-specific steps


# ---------------------------------------------------------------------------
# Matrix expansion
# ---------------------------------------------------------------------------

_ARCHS: dict[str, Architecture] = {
    "amd64": Architecture(
        platform="linux/amd64", runner=["self-hosted", "Linux", "X64", "heavy"]
    ),
    "arm64": Architecture(
        platform="linux/arm64",
        runner=["self-hosted", "Linux", "ARM64", "heavy-arm64"],
    ),
}


def expand_linux_matrix(
    linux: LinuxFile, event: str | None = None
) -> list[MatrixEntry]:
    """Expand a LinuxFile into a flat list of matrix entries.

    Each config entry is expanded over the cross-product of its
    compiler, build_type, sanitizers, and architecture lists. Configs that
    exclude the current event are skipped.
    """
    entries: list[MatrixEntry] = []

    for distro, configs in linux.configs.items():
        for cfg in configs:
            if not runs_on_event(cfg.exclude_event_types, event):
                continue
            # An empty sanitizers list means "one entry with no sanitizer".
            effective_sanitizers = cfg.sanitizers or [""]
            effective_archs = {arch: _ARCHS[arch] for arch in cfg.arch}

            for compiler, build_type, sanitizer, (arch, arch_info) in itertools.product(
                cfg.compiler,
                cfg.build_type,
                effective_sanitizers,
                effective_archs.items(),
            ):
                name = f"{distro}-{compiler}-{build_type.lower()}-{arch}"
                suffix_parts = [
                    s for s in [cfg.suffix, _SANITIZER_SUFFIX.get(sanitizer, "")] if s
                ]
                if suffix_parts:
                    name += "-" + "-".join(suffix_parts)

                entries.append(
                    MatrixEntry(
                        config_name=name,
                        image=f"ghcr.io/xrplf/xrpld/nix-{distro}:{linux.image_tag}",
                        cmake_args=get_cmake_args(build_type, cfg.extra_cmake_args),
                        cmake_target="all",
                        build_only=False,
                        build_type=build_type,
                        architecture=arch_info,
                        sanitizers=sanitizer,
                        compiler=compiler,
                    )
                )

    return entries


def expand_linux_packaging(linux: LinuxFile) -> list[PackagingEntry]:
    """Generate the packaging matrix from a LinuxFile's package_configs section.

    Packaging uses vanilla distro images (debian:bookworm, ubi9, …) instead of
    the nix-based build images, because deb/rpm tooling (debhelper, rpm-build)
    is taken from the distro's archive rather than from nixpkgs. Each config
    entry carries its own 'image'.
    """
    entries = []
    for distro, configs in linux.package_configs.items():
        for cfg in configs:
            for compiler, build_type in itertools.product(cfg.compiler, cfg.build_type):
                entries.append(
                    PackagingEntry(
                        artifact_name=f"xrpld-{distro}-{compiler}-{build_type.lower()}-amd64",
                        image=cfg.image,
                        distro=distro,
                    )
                )

    return entries


def expand_platform_matrix(
    pf: PlatformFile, event: str | None = None
) -> list[MatrixEntry]:
    """Expand a PlatformFile (macOS or Windows) into matrix entries.

    Configs that exclude the current event are skipped.
    """
    platform_name, arch = pf.platform.split("/")
    is_windows = platform_name == "windows"

    entries: list[MatrixEntry] = []
    for cfg in pf.configs:
        if not runs_on_event(cfg.exclude_event_types, event):
            continue
        for build_type in cfg.build_type:
            entries.append(
                MatrixEntry(
                    config_name=f"{platform_name}-{arch}-{build_type.lower()}",
                    cmake_args=get_cmake_args(build_type, cfg.extra_cmake_args),
                    cmake_target="install" if is_windows else "all",
                    build_only=cfg.build_only,
                    build_type=build_type,
                    architecture=Architecture(platform=pf.platform, runner=pf.runner),
                    sanitizers="",
                )
            )
    return entries


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
        choices=["linux", "macos", "windows"],
        default=None,
    )
    parser.add_argument(
        "-p",
        "--packaging",
        help="Emit the Linux packaging matrix instead of the build/test matrix.",
        action="store_true",
    )
    parser.add_argument(
        "-e",
        "--event",
        help="The GitHub event name that triggered the workflow (e.g. 'push', "
        "'pull_request'). Configs are filtered by their 'event_type'. If "
        "omitted, no filtering is applied.",
        default=None,
    )
    args = parser.parse_args()

    matrix: list[MatrixEntry] | list[PackagingEntry] = []

    if args.packaging:
        matrix = expand_linux_packaging(LinuxFile.load(THIS_DIR / "linux.json"))
    else:
        if args.config in ("linux", None):
            matrix += expand_linux_matrix(
                LinuxFile.load(THIS_DIR / "linux.json"), args.event
            )
        if args.config in ("macos", None):
            matrix += expand_platform_matrix(
                PlatformFile.load(THIS_DIR / "macos.json"), args.event
            )
        if args.config in ("windows", None):
            matrix += expand_platform_matrix(
                PlatformFile.load(THIS_DIR / "windows.json"), args.event
            )

    print(f"matrix={json.dumps({'include': [dataclasses.asdict(e) for e in matrix]})}")

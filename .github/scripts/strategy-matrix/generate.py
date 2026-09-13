#!/usr/bin/env python3
import argparse
import dataclasses
import itertools
import json
from pathlib import Path

THIS_DIR = Path(__file__).parent.resolve()

_BASE_CMAKE_ARGS = [
    "-Dtests=ON",
    "-Dwerr=ON",
    "-Dxrpld=ON",
    "-Dwextra=ON",
    "-Drust=ON",
]

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


# ---------------------------------------------------------------------------
# Input types — shapes of the JSON config files
# ---------------------------------------------------------------------------


# Every config must declare 'minimal'. Minimal configs form the reduced matrix
# built for pull requests by default; the full matrix adds the rest.
#
# Configs may also opt into 'benchmark' to smoke-run the benchmarks, or carry a
# 'package' map to be packaged as well. Note that either applies to every entry
# a config expands into, so only set them on configs that expand to a single
# combination.


@dataclasses.dataclass
class PackageConfig:
    """The 'package' map of a config whose binaries are also packaged."""

    type: str  # "deb" or "rpm"; has to match what the image provides
    # The packaging container image: a vanilla distro image, not the nix image
    # the config itself builds in.
    image: str


@dataclasses.dataclass
class LinuxConfig:
    """One entry in a linux.json 'configs' array."""

    compiler: list[str]
    build_type: list[str]
    arch: list[str]
    minimal: bool
    benchmark: bool = False  # if true, smoke-run the benchmarks after testing
    sanitizers: list[str] = dataclasses.field(default_factory=list)
    suffix: str = ""
    extra_cmake_args: str = ""
    package: PackageConfig | None = None  # set to also package this config

    def __post_init__(self) -> None:
        if isinstance(self.package, dict):
            self.package = PackageConfig(**self.package)


@dataclasses.dataclass
class LinuxFile:
    """Shape of linux.json."""

    image_tag: str
    configs: dict[str, list[LinuxConfig]]  # distro → configs

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
    minimal: bool
    build_only: bool = False  # if true, skip tests (e.g. macos/Windows Debug)
    benchmark: bool = False  # if true, smoke-run the benchmarks after testing
    extra_cmake_args: str = ""
    # "" is the runner's system compiler, "nix" the flake's CI environment.
    # macOS only: Linux always builds in a Nix image, Windows has no Nix.
    toolchain: str = ""

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
    benchmark: bool
    build_type: str
    architecture: Architecture
    sanitizers: str
    image: str = ""  # container image; empty for macOS/Windows (runs natively)
    compiler: str = ""  # compiler name ("gcc" or "clang"); empty for macOS/Windows
    toolchain: str = ""  # "nix" for the flake's CI environment; see PlatformConfig


@dataclasses.dataclass
class PackagingEntry:
    """One entry in the generated packaging strategy matrix."""

    xrpld_artifact_name: str
    validator_keys_artifact_name: str
    image: str
    package_type: str  # "deb" or "rpm"; drives the format-specific steps


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


def expand_linux_matrix(linux: LinuxFile, minimal: bool) -> list[MatrixEntry]:
    """Expand a LinuxFile into a flat list of matrix entries.

    Each config entry is expanded over the cross-product of its
    compiler, build_type, sanitizers, and architecture lists. When 'minimal' is
    true, only configs flagged as minimal are included.
    """
    entries: list[MatrixEntry] = []

    for distro, configs in linux.configs.items():
        for cfg in configs:
            if minimal and not cfg.minimal:
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


def expand_linux_packaging(linux: LinuxFile) -> list[PackagingEntry]:
    """Generate the packaging matrix from the configs that carry a 'package' map.

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
            if cfg.package is None:
                continue
            for compiler, build_type, arch in itertools.product(
                cfg.compiler, cfg.build_type, cfg.arch
            ):
                # The packaging workflow hardcodes an amd64 runner.
                assert arch == "amd64", f"cannot package {distro} on {arch}"
                name = config_name(distro, compiler, build_type, arch, cfg.suffix)
                entries.append(
                    PackagingEntry(
                        xrpld_artifact_name=f"xrpld-{name}",
                        validator_keys_artifact_name=f"validator-keys-{name}",
                        image=cfg.package.image,
                        package_type=cfg.package.type,
                    )
                )

    return entries


def expand_platform_matrix(pf: PlatformFile, minimal: bool) -> list[MatrixEntry]:
    """Expand a PlatformFile (macOS or Windows) into matrix entries.

    When 'minimal' is true, only configs flagged as minimal are included.
    """
    platform_name, arch = pf.platform.split("/")
    is_windows = platform_name == "windows"

    entries: list[MatrixEntry] = []
    for cfg in pf.configs:
        if minimal and not cfg.minimal:
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
        "-m",
        "--minimal",
        help="Emit only the minimal matrix (the configs flagged 'minimal'), "
        "used for pull requests by default. If omitted, the full matrix is "
        "emitted.",
        action="store_true",
    )
    args = parser.parse_args()

    matrix: list[MatrixEntry] | list[PackagingEntry] = []

    if args.packaging:
        matrix = expand_linux_packaging(LinuxFile.load(THIS_DIR / "linux.json"))
    else:
        if args.config in ("linux", None):
            matrix += expand_linux_matrix(
                LinuxFile.load(THIS_DIR / "linux.json"), args.minimal
            )
        if args.config in ("macos", None):
            matrix += expand_platform_matrix(
                PlatformFile.load(THIS_DIR / "macos.json"), args.minimal
            )
        if args.config in ("windows", None):
            matrix += expand_platform_matrix(
                PlatformFile.load(THIS_DIR / "windows.json"), args.minimal
            )

    print(f"matrix={json.dumps({'include': [dataclasses.asdict(e) for e in matrix]})}")

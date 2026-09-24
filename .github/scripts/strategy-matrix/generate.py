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

# The package formats a config can be packaged as, each with its own
# install-test job in reusable-package.yml.
PACKAGE_TYPES = ("deb", "rpm")

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


# ---------------------------------------------------------------------------
# Input types — shapes of the JSON config files
# ---------------------------------------------------------------------------


# Every config must declare 'minimal'. Minimal configs form the reduced matrix
# built for pull requests by default; the full matrix adds the rest.
#
# A Linux config may instead declare 'extended'. Neither the minimal nor the full
# matrix includes such a config; only the extended matrix does, which the nightly
# schedule and a manual run ask for. Use it for a config too expensive to run per
# pull request. It is Linux only because nothing else needs it yet.
#
# Configs may also opt into 'benchmark' to smoke-run the benchmarks, or carry a
# 'package' map to be packaged as well. Note that either applies to every entry
# a config expands into, so only set them on configs that expand to a single
# combination.


@dataclasses.dataclass
class PackageConfig:
    """The 'package' map of a config whose binaries are also packaged."""

    type: str  # has to match what the image provides
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
    minimal: bool
    extended: bool = False
    benchmark: bool = False  # if true, smoke-run the benchmarks after testing
    sanitizers: list[str] = dataclasses.field(default_factory=list)
    suffix: str = ""
    extra_cmake_args: str = ""
    package: PackageConfig | None = None  # set to also package this config

    def __post_init__(self) -> None:
        if isinstance(self.package, dict):
            self.package = PackageConfig(**self.package)
        # The two flags pull in opposite directions: 'minimal' asks for the
        # smallest matrix, 'extended' for the largest. The filtering would drop
        # such a config from the minimal matrix, which reads as the opposite of
        # what it declared, so reject the pair instead.
        assert not (
            self.minimal and self.extended
        ), "a config cannot be both 'minimal' and 'extended'."


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
    package_variant: str  # passed to build_pkg.py --variant; empty for xrpld
    package_name: str  # the name it builds under, which the artifact globs use


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


def expand_linux_config(
    distro: str, cfg: LinuxConfig, image_tag: str
) -> list[MatrixEntry]:
    """Expand one Linux config over the cross-product of its lists.

    Kept apart from the size filtering in expand_linux_matrix so that
    validate_linux_matrices can ask what a single config expands to without
    repeating the cross-product.

    @param distro The distro key the config is listed under in linux.json.
    @param cfg The config to expand.
    @param image_tag The tag of the nix image the entries build in.
    @return One entry per (compiler, build type, sanitizer, architecture)
        combination the config's lists produce.
    """
    # An empty sanitizers list means "one entry with no sanitizer".
    effective_sanitizers = cfg.sanitizers or [""]
    effective_archs = {arch: _ARCHS[arch] for arch in cfg.arch}

    return [
        MatrixEntry(
            config_name=config_name(
                distro, compiler, build_type, arch, cfg.suffix, sanitizer
            ),
            image=f"ghcr.io/xrplf/xrpld/nix-{distro}:{image_tag}",
            cmake_args=get_cmake_args(build_type, cfg.extra_cmake_args),
            cmake_target="all",
            build_only=False,
            benchmark=cfg.benchmark,
            build_type=build_type,
            architecture=arch_info,
            sanitizers=sanitizer,
            compiler=compiler,
        )
        for compiler, build_type, sanitizer, (arch, arch_info) in itertools.product(
            cfg.compiler,
            cfg.build_type,
            effective_sanitizers,
            effective_archs.items(),
        )
    ]


def expand_linux_matrix(
    linux: LinuxFile, minimal: bool, extended: bool = False
) -> list[MatrixEntry]:
    """Expand a LinuxFile into a flat list of matrix entries.

    Each config entry is expanded over the cross-product of its
    compiler, build_type, sanitizers, and architecture lists. When 'minimal' is
    true, only configs flagged as minimal are included.

    @param linux The parsed linux.json.
    @param minimal Emit only the configs flagged 'minimal'.
    @param extended Emit the configs flagged 'extended'. Both the minimal and
        the full matrix leave those out, so this is the only way to get them.
    @return One entry per combination the surviving configs expand into.
    @note Do not set both 'minimal' and 'extended'. The command line rejects the
        pair, but a direct caller gets the minimal matrix rather than an error.
    """
    entries: list[MatrixEntry] = []

    for distro, configs in linux.configs.items():
        for cfg in configs:
            if minimal and not cfg.minimal:
                continue
            if not extended and cfg.extended:
                continue
            entries += expand_linux_config(distro, cfg, linux.image_tag)

    return entries


def validate_linux_matrices(linux: LinuxFile) -> None:
    """Check that the three matrix sizes nest, and hold the configs they should.

    CI runs only the jobs this script emits, so a config that falls out of the
    size it belongs to takes its coverage with it and fails nothing. These checks
    run on every invocation, so the drop fails matrix generation instead.

    The checks name no config, only the flags, so adding or removing a config
    needs no edit here.

    @param linux The parsed linux.json.
    @raise AssertionError If two configs expand to the same name, if the sizes do
        not nest, or if a config reaches a size it does not belong to.
    """
    minimal_names = {e.config_name for e in expand_linux_matrix(linux, minimal=True)}
    full_names = {e.config_name for e in expand_linux_matrix(linux, minimal=False)}
    extended_names = {
        e.config_name for e in expand_linux_matrix(linux, minimal=False, extended=True)
    }

    # The names each config expands to, paired with the config, so that the
    # checks below expand every config once.
    per_config = [
        (
            distro,
            cfg,
            {e.config_name for e in expand_linux_config(distro, cfg, linux.image_tag)},
        )
        for distro, configs in linux.configs.items()
        for cfg in configs
    ]

    # A config name is also the name of the artifacts the job uploads, so two
    # configs that expand to the same name overwrite each other. The per-config
    # checks below also need a name to belong to one config only.
    all_names = [n for _, _, names in per_config for n in names]
    duplicates = sorted({n for n in all_names if all_names.count(n) > 1})
    assert not duplicates, f"configs expand to duplicate names: {duplicates}."

    # The sizes nest, so a larger one only ever adds. Were 'extended' to replace
    # the full matrix rather than widen it, the nightly would test less than a
    # labeled pull request does, which is the opposite of the intent.
    assert minimal_names <= full_names, (
        "the minimal matrix is not part of the full one, missing: "
        f"{sorted(minimal_names - full_names)}."
    )
    assert full_names <= extended_names, (
        "the full matrix is not part of the extended one, missing: "
        f"{sorted(full_names - extended_names)}."
    )

    # Every config reaches the sizes its flags ask for, and no others.
    for distro, cfg, names in per_config:
        if cfg.extended:
            assert names <= extended_names, (
                f"{distro} config flagged 'extended' is missing from the "
                f"extended matrix: {sorted(names - extended_names)}."
            )
            assert not names & full_names, (
                f"{distro} config flagged 'extended' also reaches the full "
                f"matrix: {sorted(names & full_names)}."
            )
        else:
            assert names <= full_names, (
                f"{distro} config is missing from the full matrix: "
                f"{sorted(names - full_names)}."
            )
        if cfg.minimal:
            assert names <= minimal_names, (
                f"{distro} config flagged 'minimal' is missing from the "
                f"minimal matrix: {sorted(names - minimal_names)}."
            )

    # The 'extended' tier costs a flag here, a condition in
    # reusable-strategy-matrix.yml, and the check after it. An empty tier leaves
    # all three as dead weight that still reads as working, so require a holder.
    # Checked last, because the checks above name the config that went missing.
    assert extended_names > full_names, (
        "no config is flagged 'extended', so the extended matrix is the full one. "
        "Either flag the config that needs the tier, or remove the tier."
    )


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
    # Each flag picks a matrix size, and the sizes nest: minimal is a subset of
    # the full matrix, which is a subset of extended. So one flag narrows and the
    # other widens the same default, and asking for both has no answer. Argparse
    # rejects the pair rather than letting the filters intersect into a surprise.
    size = parser.add_mutually_exclusive_group()
    size.add_argument(
        "-m",
        "--minimal",
        help="Emit only the minimal matrix (the configs flagged 'minimal'), "
        "used for pull requests by default. If omitted, the full matrix is "
        "emitted.",
        action="store_true",
    )
    size.add_argument(
        "-x",
        "--extended",
        help="Emit the extended matrix: the full one plus the configs flagged "
        "'extended', which no other matrix includes. Used for the nightly "
        "schedule and for a manual run.",
        action="store_true",
    )
    args = parser.parse_args()

    matrix: list[MatrixEntry] | list[PackagingEntry] = []

    # Checked on every invocation, including the ones that emit another platform
    # or the packaging matrix, so that no call can pass a broken linux.json.
    linux = LinuxFile.load(THIS_DIR / "linux.json")
    validate_linux_matrices(linux)

    if args.packaging:
        matrix = expand_linux_packaging(linux)
        # One list per format, so each install-test job installs the packages its
        # own format produced.
        for package_type, names in package_names_by_type(matrix).items():
            print(f"{package_type}_package_names={json.dumps(names)}")
    else:
        if args.config in ("linux", None):
            matrix += expand_linux_matrix(linux, args.minimal, args.extended)
        if args.config in ("macos", None):
            matrix += expand_platform_matrix(
                PlatformFile.load(THIS_DIR / "macos.json"), args.minimal
            )
        if args.config in ("windows", None):
            matrix += expand_platform_matrix(
                PlatformFile.load(THIS_DIR / "windows.json"), args.minimal
            )

    print(f"matrix={json.dumps({'include': [dataclasses.asdict(e) for e in matrix]})}")

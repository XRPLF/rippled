#!/usr/bin/env python3
"""Build an RPM or Debian package from the pre-built xrpld and validator-keys binaries.

The build tool for the chosen format has to be on PATH, so this runs in the
vanilla distro image that matches it.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import textwrap
from datetime import datetime, timezone
from pathlib import Path

# This script lives in the repository it packages.
SRC_DIR = Path(__file__).resolve().parents[1]

PRE_RELEASE = re.compile(r"^(b|rc)(0|[1-9][0-9]*)(\+.*)?$")

# The package name a variant suffixes, and the name every variant keeps for its
# on-disk paths (/usr/bin/xrpld, /etc/xrpld, xrpld.service).
BASE_NAME = "xrpld"

# The flavours that can be built, '' being the plain xrpld package. A variant
# needs a config in linux.json to be built by CI; see package/README.md.
VARIANTS = ("", "assert")

# Files both packaging systems consume, staged under the same names.
STAGED_FROM_BUILD = ("xrpld", "validator-keys", "validator-keys-LICENSE")
STAGED_FROM_SRC = {
    "cfg/xrpld-example.cfg": "xrpld.cfg",
    "cfg/validators-example.txt": "validators.txt",
    "LICENSE.md": "LICENSE.md",
    "README.md": "README.md",
}
STAGED_UNITS = ("xrpld.service", "xrpld.sysusers", "xrpld.tmpfiles", "xrpld.logrotate")

# debian/ files debhelper keys by package name, staged as '<package>.<name>'.
DEBIAN_PKG_FILES = ("docs", "links")

# Debian control files have no conditionals, so what makes a variant replace the
# plain package is rendered into control.in rather than written there.
DEB_VARIANT_FIELDS = """\
Conflicts: xrpld
Replaces: xrpld
Provides: xrpld (= ${binary:Version})"""

TOKEN = re.compile(r"@[A-Z_]+@")


def run(*command: object, cwd: Path | None = None) -> None:
    """Echo a command and run it."""
    argv = [str(part) for part in command]
    print("+ " + " ".join(argv), flush=True)
    subprocess.run(argv, check=True, cwd=cwd)


def capture(*command: object) -> str:
    """Run a command and return its stdout, stripped."""
    argv = [str(part) for part in command]
    # stderr is left alone so a failing command explains itself.
    return subprocess.run(
        argv, stdout=subprocess.PIPE, text=True, check=True
    ).stdout.strip()


def package_version(reported: str) -> str:
    """Normalise a reported version into one the package formats accept.

    A pre-release switches to '~' (3.2.0-b1 -> 3.2.0~b1), which also sorts before
    the final 3.2.0; a no-op for a final release.
    """
    base, _, pre_release = reported.partition("-")
    version = f"{base}~{pre_release}" if pre_release else base

    # BuildInfo already SemVer-validates the version. Packaging adds one narrower
    # constraint: after normalisation the version must not contain '-', because
    # RPM forbids it in Version and Debian reads it as the revision separator.
    assert "-" not in version, (
        f"unsupported version {reported!r}: {version!r} cannot contain '-'. "
        "Use a single-token pre-release like 3.2.0-b1 or 3.2.0-rc2."
    )
    assert pre_release or "+" not in reported, (
        f"unsupported version {reported!r}: "
        "build metadata is only supported on bN/rcN pre-releases."
    )
    assert not pre_release or PRE_RELEASE.match(pre_release), (
        f"unsupported pre-release {pre_release!r}: use bN or rcN, "
        "e.g. 3.2.0-b1 or 3.2.0-rc2."
    )
    return version


def render(template: Path, dest: Path, values: dict[str, str]) -> None:
    """Write template to dest with its @TOKEN@ placeholders substituted.

    A token left without a value fails the build rather than reaching dpkg.
    """
    text = template.read_text()
    for token, value in values.items():
        text = text.replace(f"@{token}@", value)

    missing = sorted(set(TOKEN.findall(text)))
    assert not missing, f"{template}: no value for {', '.join(missing)}"

    # An empty value at the end of a stanza would otherwise leave a blank line,
    # which is what ends a stanza.
    dest.write_text(text.rstrip("\n") + "\n")


def package_name(variant: str) -> str:
    """The binary package name for a variant: '' -> xrpld, 'assert' -> xrpld-assert."""
    return f"{BASE_NAME}-{variant}" if variant else BASE_NAME


def read_version(xrpld: Path) -> str:
    """Read the version from the binary that is about to be packaged."""
    fields = capture(xrpld, "--version").partition("\n")[0].split()
    assert len(fields) >= 3, f"cannot read a version from {xrpld} --version"
    return fields[2]


def check_binaries(build_dir: Path) -> None:
    """Fail unless the binaries and their notices are present and runnable."""
    missing = [
        name
        for name in ("xrpld", "validator-keys")
        if not os.access(build_dir / name, os.X_OK)
    ]
    assert not missing, (
        f"missing or not executable in {build_dir}: {' '.join(missing)}. "
        "Both binaries come from a single CMake build directory configured with "
        "-Dxrpld=ON -Dvalidator_keys=ON."
    )

    # No package goes out without the attribution.
    notice = build_dir / "validator-keys-LICENSE"
    assert notice.is_file(), (
        f"missing {notice}. cmake/XrplValidatorKeys.cmake copies it out of the "
        "fetched validator-keys-tool source, so reconfigure with -Dvalidator_keys=ON."
    )

    # Catches a binary still pointing at the Nix store's ELF loader, since
    # packaging runs in a vanilla distro container.
    capture(build_dir / "validator-keys", "--version")


def source_date_epoch() -> int:
    """The last commit's timestamp."""
    # git refuses to read a checkout owned by another user, which is what a CI
    # container or a bind mount hands it.
    return int(
        capture(
            "git",
            "-c",
            f"safe.directory={SRC_DIR}",
            "-C",
            SRC_DIR,
            "log",
            "-1",
            "--format=%ct",
        )
    )


def stage_common(build_dir: Path, dest: Path) -> None:
    """Copy everything both packaging systems consume into dest."""
    dest.mkdir(parents=True, exist_ok=True)

    for name in STAGED_FROM_BUILD:
        shutil.copy2(build_dir / name, dest / name)
    for source, name in STAGED_FROM_SRC.items():
        shutil.copy2(SRC_DIR / source, dest / name)


def stage_units(dest: Path, *, prefix: str = "") -> None:
    """Copy the systemd, sysusers, tmpfiles and logrotate files into dest.

    Each format wants them somewhere else: rpmbuild reads them from SOURCES by
    path, debhelper from debian/ by package name -- hence 'prefix', which makes
    the copies 'xrpld-assert.xrpld.service' and so on.
    """
    for name in STAGED_UNITS:
        shutil.copy2(SRC_DIR / "package" / "shared" / name, dest / f"{prefix}{name}")


def build_rpm(build_dir: Path, *, version: str, pkg_release: str, variant: str) -> None:
    """Stage the spec and its sources, then build the binary RPMs."""
    topdir = build_dir / "rpmbuild"
    for name in ("BUILD", "BUILDROOT", "RPMS", "SOURCES", "SPECS", "SRPMS"):
        (topdir / name).mkdir(parents=True, exist_ok=True)

    spec = topdir / "SPECS" / "xrpld.spec"
    shutil.copy2(SRC_DIR / "package" / "rpm" / "xrpld.spec", spec)
    stage_common(build_dir, topdir / "SOURCES")
    stage_units(topdir / "SOURCES")

    # The spec defaults it to nothing, so a plain build is unchanged.
    variant_defines = ["--define", f"pkg_variant {variant}"] if variant else []

    run(
        "rpmbuild",
        "-bb",
        "--define",
        f"_topdir {topdir}",
        "--define",
        f"pkg_version {version}",
        "--define",
        f"pkg_release {pkg_release}",
        # The image tracks the newest distro, but the packages target el9.
        "--define",
        "dist .el9",
        *variant_defines,
        spec,
    )


def stage_debian(dest: Path, name: str) -> None:
    """Stage the debian directory for the package name being built."""
    source = SRC_DIR / "package" / "debian"
    shutil.copytree(
        source, dest, ignore=shutil.ignore_patterns("*.in", *DEBIAN_PKG_FILES)
    )

    values = {
        "PKG": name,
        "VARIANT_FIELDS": "" if name == BASE_NAME else DEB_VARIANT_FIELDS,
    }
    render(source / "control.in", dest / "control", values)
    render(source / "lintian-overrides.in", dest / f"{name}.lintian-overrides", values)

    for suffix in DEBIAN_PKG_FILES:
        shutil.copy2(source / suffix, dest / f"{name}.{suffix}")


def build_deb(
    build_dir: Path,
    *,
    version: str,
    reported: str,
    pkg_release: str,
    channel: str,
    epoch: int,
    name: str,
) -> None:
    """Stage the debian directory and its sources, then build the binary DEBs."""
    staging = build_dir / "debbuild" / "source"
    stage_common(build_dir, staging)
    stage_debian(staging / "debian", name)

    # Prefixed whether it is a variant's name or not: debian/rules names them
    # explicitly either way.
    stage_units(staging / "debian", prefix=f"{name}.")

    date = datetime.fromtimestamp(epoch, timezone.utc).strftime(
        "%a, %d %b %Y %H:%M:%S %z"
    )
    # The leading spaces are significant to dpkg.
    changelog = textwrap.dedent(f"""\
        {name} ({version}-{pkg_release}) {channel}; urgency=medium
          * Release {reported}.

         -- XRPL Foundation <contact@xrplf.org>  {date}
        """)
    (staging / "debian" / "changelog").write_text(changelog)

    run("dpkg-buildpackage", "-b", "--no-sign", "-d", cwd=staging)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--package-type",
        required=True,
        choices=("deb", "rpm"),
        help="the package format to build",
    )
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=Path("build"),
        help="directory holding the xrpld and validator-keys binaries (default: %(default)s)",
    )
    parser.add_argument(
        "--pkg-release",
        default="1",
        help="package release iteration (default: %(default)s)",
    )
    parser.add_argument(
        "--variant",
        default="",
        choices=VARIANTS,
        help="the flavour of the package to build: 'assert' produces "
        "xrpld-assert, which ships the same paths as xrpld and replaces it "
        "(default: the plain xrpld package)",
    )
    parser.add_argument(
        "--channel",
        required=True,
        choices=("stable", "rc", "beta", "develop", "private", "UNRELEASED"),
        help="release channel, written to debian/changelog",
    )
    args = parser.parse_args()
    package_type: str = args.package_type
    build_dir: Path = args.build_dir.resolve()
    pkg_release: str = args.pkg_release
    channel: str = args.channel
    variant: str = args.variant
    name = package_name(variant)

    assert build_dir.is_dir(), (
        f"build directory not found: {build_dir}. Build the binaries before "
        "packaging, or point --build-dir at the directory holding them."
    )

    check_binaries(build_dir)
    reported = read_version(build_dir / "xrpld")
    version = package_version(reported)
    epoch = source_date_epoch()

    # rpmbuild and dpkg-buildpackage both honour this for file timestamps.
    os.environ["SOURCE_DATE_EPOCH"] = str(epoch)

    # Remove both build trees, because a package left from an earlier build would
    # otherwise be picked up and published alongside this one.
    for tree in ("debbuild", "rpmbuild"):
        shutil.rmtree(build_dir / tree, ignore_errors=True)

    print(f"Building {package_type} {name} {version}-{pkg_release}", flush=True)

    if package_type == "deb":
        build_deb(
            build_dir,
            version=version,
            reported=reported,
            pkg_release=pkg_release,
            channel=channel,
            epoch=epoch,
            name=name,
        )
    else:
        build_rpm(build_dir, version=version, pkg_release=pkg_release, variant=variant)


if __name__ == "__main__":
    main()

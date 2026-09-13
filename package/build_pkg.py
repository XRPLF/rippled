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

# Files both packaging systems consume, staged under the same names.
STAGED_FROM_BUILD = ("xrpld", "validator-keys", "validator-keys-LICENSE")
STAGED_FROM_SRC = {
    "cfg/xrpld-example.cfg": "xrpld.cfg",
    "cfg/validators-example.txt": "validators.txt",
    "LICENSE.md": "LICENSE.md",
    "README.md": "README.md",
}
STAGED_UNITS = ("xrpld.service", "xrpld.sysusers", "xrpld.tmpfiles", "xrpld.logrotate")


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


def stage_units(dest: Path) -> None:
    """Copy the systemd, sysusers, tmpfiles and logrotate files into dest.

    Each format wants them somewhere else: rpmbuild reads them from SOURCES,
    debhelper from debian/.
    """
    for name in STAGED_UNITS:
        shutil.copy2(SRC_DIR / "package" / "shared" / name, dest / name)


def build_rpm(build_dir: Path, *, version: str, pkg_release: str) -> None:
    """Stage the spec and its sources, then build the binary RPMs."""
    topdir = build_dir / "rpmbuild"
    for name in ("BUILD", "BUILDROOT", "RPMS", "SOURCES", "SPECS", "SRPMS"):
        (topdir / name).mkdir(parents=True, exist_ok=True)

    spec = topdir / "SPECS" / "xrpld.spec"
    shutil.copy2(SRC_DIR / "package" / "rpm" / "xrpld.spec", spec)
    stage_common(build_dir, topdir / "SOURCES")
    stage_units(topdir / "SOURCES")

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
        spec,
    )


def build_deb(
    build_dir: Path,
    *,
    version: str,
    reported: str,
    pkg_release: str,
    channel: str,
    epoch: int,
) -> None:
    """Stage the debian directory and its sources, then build the binary DEBs."""
    staging = build_dir / "debbuild" / "source"
    stage_common(build_dir, staging)
    shutil.copytree(SRC_DIR / "package" / "debian", staging / "debian")

    # debhelper picks these up from debian/ automatically.
    stage_units(staging / "debian")

    date = datetime.fromtimestamp(epoch, timezone.utc).strftime(
        "%a, %d %b %Y %H:%M:%S %z"
    )
    # The leading spaces are significant to dpkg.
    changelog = textwrap.dedent(f"""\
        xrpld ({version}-{pkg_release}) {channel}; urgency=medium
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

    if package_type == "deb":
        build_deb(
            build_dir,
            version=version,
            reported=reported,
            pkg_release=pkg_release,
            channel=channel,
            epoch=epoch,
        )
    else:
        build_rpm(build_dir, version=version, pkg_release=pkg_release)


if __name__ == "__main__":
    main()

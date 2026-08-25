#!/usr/bin/env python3
"""Publish the packages built by build_pkg.py to the XRPLF repositories on Nexus.

RPMs are uploaded to the hosted repository, but yum clients install from the
'rpm-<channel>' group repository in front of it, which serves signed metadata.

NEXUS_USERNAME and NEXUS_PASSWORD are read from the environment, so the
credentials never reach the process list.
"""

import argparse
import base64
import os
import time
import urllib.error
import urllib.request
from pathlib import Path

SUFFIXES = (".deb", ".ddeb", ".rpm")

# No progress for this long ends an attempt. urlopen applies the timeout per
# socket operation, so a stalled transfer fails while a merely slow one carries
# on -- the debuginfo package is large enough for that distinction to matter.
STALL_TIMEOUT = 300

ATTEMPTS = 4
RETRY_DELAY = 5


def build_opener() -> urllib.request.OpenerDirector:
    """An opener with no redirect handler, so a 3xx raises instead of being followed.

    A redirected upload is silently downgraded to a GET, turning it into a no-op
    that still answers 200.
    """
    opener = urllib.request.OpenerDirector()
    opener.add_handler(urllib.request.HTTPHandler())
    opener.add_handler(urllib.request.HTTPSHandler())
    opener.add_handler(urllib.request.HTTPErrorProcessor())
    opener.add_handler(urllib.request.HTTPDefaultErrorHandler())
    return opener


def upload(url: str, method: str, headers: dict[str, str], package: Path) -> None:
    """Send one package, retrying only what is worth retrying.

    A 4xx is a deterministic rejection, so it is reported at once rather than
    re-sending the whole body three more times. Nexus explains what it rejected
    in the response body, so that body is always surfaced.
    """
    opener = build_opener()

    for attempt in range(1, ATTEMPTS + 1):
        try:
            with package.open("rb") as body:
                request = urllib.request.Request(
                    url,
                    data=body,
                    method=method,
                    headers={**headers, "Content-Length": str(package.stat().st_size)},
                )
                opener.open(request, timeout=STALL_TIMEOUT)
            return
        except urllib.error.HTTPError as error:
            detail = error.read().decode(errors="replace").strip()
            reason = f"HTTP {error.code}: {detail}"
            retryable = error.code >= 500
        except (urllib.error.URLError, OSError) as error:
            reason = str(error)
            retryable = True

        assert (
            retryable and attempt < ATTEMPTS
        ), f"upload of {package.name} failed: {reason}"
        print(f"    attempt {attempt} failed ({reason}), retrying")
        time.sleep(RETRY_DELAY)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--channel",
        required=True,
        help="release channel, selecting the deb-<channel> and rpm-<channel>-hosted repositories",
    )
    parser.add_argument(
        "--package-dir",
        type=Path,
        default=Path("build"),
        help=f"searched recursively for {', '.join(SUFFIXES)} (default: %(default)s)",
    )
    parser.add_argument(
        "--nexus-url",
        default="https://packages.xrplf.org",
        help="the Nexus instance to publish to (default: %(default)s)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="list the uploads without performing them",
    )
    args = parser.parse_args()
    channel: str = args.channel
    package_dir: Path = args.package_dir
    nexus_url: str = args.nexus_url
    dry_run: bool = args.dry_run

    nexus = nexus_url.rstrip("/")
    deb_repo = f"deb-{channel}"
    rpm_repo = f"rpm-{channel}-hosted"

    auth: dict[str, str] = {}
    if not dry_run:
        username = os.environ.get("NEXUS_USERNAME")
        password = os.environ.get("NEXUS_PASSWORD")
        assert username and password, "NEXUS_USERNAME and NEXUS_PASSWORD are required"
        token = base64.b64encode(f"{username}:{password}".encode()).decode()
        auth = {"Authorization": f"Basic {token}"}

    packages = sorted(
        path
        for path in package_dir.rglob("*")
        if path.is_file() and path.suffix in SUFFIXES
    )
    # Uploading nothing would otherwise look like a successful publish.
    assert packages, f"no packages found in {package_dir}"

    print(f"Publishing {package_dir} to {deb_repo} and {rpm_repo} on {nexus}:")
    for package in packages:
        if package.suffix == ".rpm":
            # yum repositories are addressed by path, and the arch comes from
            # the name, e.g. xrpld-3.4.0-1.el9.x86_64.rpm.
            destination = f"{rpm_repo}/{package.stem.rsplit('.', 1)[-1]}"
            url = f"{nexus}/repository/{destination}/{package.name}"
            method, content_type = "PUT", "application/octet-stream"
        else:
            # A raw body with a multipart Content-Type, POSTed to the repository
            # root, is the documented upload for a hosted apt repository:
            # https://help.sonatype.com/en/apt-repositories.html#deploying-packages-to-hosted-apt-repositories
            destination = deb_repo
            url = f"{nexus}/repository/{destination}/"
            method, content_type = "POST", "multipart/form-data"

        print(f"  {package.name} -> {destination}")
        if not dry_run:
            upload(url, method, {"Content-Type": content_type, **auth}, package)

    verb = "would be published" if dry_run else "published"
    print(f"{len(packages)} package(s) {verb}.")


if __name__ == "__main__":
    main()

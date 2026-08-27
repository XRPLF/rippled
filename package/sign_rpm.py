#!/usr/bin/env python3
"""Sign the RPMs built by build_pkg.py.

Nexus signs the yum repository metadata (via the 'rpm-<channel>' group
repository), but never the packages themselves, so they carry their own
signature. Clients verify the packages with gpgcheck=1 and the metadata with
repo_gpgcheck=1.

The DEBs are deliberately not signed: embedded DEB signatures exist (debsigs),
but apt does not verify them by default and trusts the repository metadata,
which Nexus signs, instead.

PKG_SIGNING_KEY is read from the environment, so the key never reaches the
process list.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import tempfile
from pathlib import Path

# An RSA signature lands in the RSAHEADER tag, a DSA or EdDSA one in DSAHEADER,
# so both are queried; checking only the first would reject a signed package.
SIGNATURE_QUERY = "%{RSAHEADER:pgpsig}%{DSAHEADER:pgpsig}"
UNSIGNED = "(none)(none)"


def gpg(gnupghome: Path, *args: str, stdin: str | None = None) -> str:
    """Run gpg against a throwaway keyring and return its stdout."""
    return subprocess.run(
        ["gpg", "--batch", "--quiet", *args],
        input=stdin,
        # stderr is left alone so a failing gpg explains itself.
        stdout=subprocess.PIPE,
        text=True,
        check=True,
        env={**os.environ, "GNUPGHOME": str(gnupghome)},
    ).stdout


def import_key(gnupghome: Path, key: str) -> str:
    """Import the armoured private key and return its fingerprint."""
    gpg(gnupghome, "--import", stdin=key)

    records = [
        line.split(":")
        for line in gpg(gnupghome, "--list-secret-keys", "--with-colons").splitlines()
    ]
    # Exactly one, so the fingerprint picked below is not a guess.
    secrets = [record for record in records if record[0] == "sec"]
    assert (
        len(secrets) == 1
    ), f"PKG_SIGNING_KEY must hold exactly one secret key, found {len(secrets)}"

    # The first fingerprint belongs to the primary key; subkeys follow.
    fingerprints = [record[9] for record in records if record[0] == "fpr"]
    assert fingerprints, "PKG_SIGNING_KEY holds a secret key with no fingerprint"
    return fingerprints[0]


def sign(gnupghome: Path, rpms: list[Path], fingerprint: str) -> None:
    """Attach a signature to every RPM in one rpmsign invocation."""
    subprocess.run(
        [
            "rpmsign",
            "--define",
            f"_gpg_name {fingerprint}",
            # Loopback pinentry: the key is unattended, so there is no tty to
            # prompt on.
            "--define",
            "_gpg_sign_cmd_extra_args --pinentry-mode loopback --batch --yes",
            "--addsign",
            *(str(rpm) for rpm in rpms),
        ],
        check=True,
        env={**os.environ, "GNUPGHOME": str(gnupghome)},
    )


def verify(rpms: list[Path]) -> None:
    """Fail unless every RPM now carries a signature.

    rpmsign can exit 0 having attached nothing, and an unsigned package is only
    rejected later, on the installing machine.
    """
    for rpm in rpms:
        signature = subprocess.run(
            ["rpm", "--query", "--queryformat", SIGNATURE_QUERY, "--package", str(rpm)],
            stdout=subprocess.PIPE,
            text=True,
            check=True,
        ).stdout.strip()
        assert signature != UNSIGNED, f"{rpm} is unsigned after rpmsign"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--package-dir",
        type=Path,
        default=Path("build"),
        help="searched recursively for *.rpm (default: %(default)s)",
    )
    args = parser.parse_args()
    package_dir: Path = args.package_dir

    rpms = sorted(path for path in package_dir.rglob("*.rpm") if path.is_file())
    # Signing nothing would otherwise look like a successful signing.
    assert rpms, f"no RPMs found in {package_dir}"

    key = os.environ.get("PKG_SIGNING_KEY")
    assert key, "PKG_SIGNING_KEY is required"

    # The keyring holds an unencrypted private key, so it goes even if signing
    # fails.
    with tempfile.TemporaryDirectory() as tmp:
        gnupghome = Path(tmp)
        fingerprint = import_key(gnupghome, key)
        print(f"Signing {len(rpms)} RPM(s) with {fingerprint}.")
        sign(gnupghome, rpms, fingerprint)
        verify(rpms)


if __name__ == "__main__":
    main()

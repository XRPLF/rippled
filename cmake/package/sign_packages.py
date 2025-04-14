#!/usr/bin/env python3
import argparse
import base64
import os
import subprocess
import sys
from pathlib import Path

import gnupg

"""
Call this module to sign rpm and deb packages.
GPG_KEY_B64 and GPG_KEY_PASS_B64 must be defined as environment variables
Pass the directory containing packages as param to the script.
sign_rpm() needs the rpm-sign pkg
    available as rpm in debian bullseye and rpmsign in bookworm
sign_deb() needs dpkg-sig gnupg to run in container with python3 with python-gnupg installed
    apt-get update && apt-get install dpkg-sig gnupg && pip install python-gnupg
    dpgk-sig no longer in debian 12 bookwork so something else will need to be figured out
"""

## If tty, gpg needs it
# tty = subprocess.check_output(["tty"], text=True)
# os.environ["GPG_TTY"] = tty

# TODO: Error if these aren't found somehow
GPG_KEY_B64 = os.environ['GPG_KEY_B64']
GPG_KEY_PASS_B64 = os.environ['GPG_KEY_PASS_B64']
gpg_passphrase = base64.b64decode(GPG_KEY_PASS_B64).decode('utf-8').strip()
GPG_KEY = base64.b64decode(GPG_KEY_B64 + "==").decode('utf-8').strip()

gnupghome = "/root/gnupghome"
os.environ["GNUPGHOME"] = gnupghome
Path(gnupghome).mkdir(parents=True, exist_ok=True, mode=0o0700)
gpg = gnupg.GPG(gnupghome=gnupghome, secret_keyring=f'{gnupghome}/secring.gpg', keyring=f'{gnupghome}/pubring.gpg')
import_result = gpg.import_keys(GPG_KEY)
assert import_result.count == 1
gpg_keyid = gpg.list_keys()[0]["keyid"]

rpm_sign_cmd = [
    'setsid', 'rpm',
    '--define', f'_gpg_name {gpg_keyid}',
    '--define', '_signature gpg',
    '--define', '__gpg_check_password_cmd /bin/true',
    '--define', "__gpg_sign_cmd %{__gpg} gpg --batch --no-armor --digest-algo 'sha512' --passphrase " +
        gpg_passphrase +
        " --no-secmem-warning -u '%{_gpg_name}' --sign --detach-sign --output %{__signature_filename} %{__plaintext_filename}",
    '--addsign'
]

deb_sign_cmd = [
    'dpkg-sig',
    '--gpg-options',
    f"--no-tty --digest-algo 'sha512' --passphrase '{gpg_passphrase}' --pinentry-mode=loopback",
    '-k', gpg_keyid,
    '--sign', 'builder',
]

def sign_package(package):
    try:
        _, file_extension = os.path.splitext(package)
        if file_extension == '.rpm':
            return sign_rpm(package)
        elif file_extension in [".deb", ".ddeb"]:  # debian debug has ext
            return sign_deb(package)
        else:
            print(f"couldn't determine file type of {package}")
    except Exception as e:
        print(f"Something went wrong! {e}")


def sign_rpm(package):
    rpm_sign_cmd.append(package)
    cmd = rpm_sign_cmd
    result = subprocess.run(cmd, capture_output=True, text=True, input="y")
    return result


def sign_deb(package):
    deb_sign_cmd.append(package)
    cmd = deb_sign_cmd
    result = subprocess.run(cmd)
    stream = open(package, "rb")
    verified = gpg.verify_file(stream)
    """
    verify deb
    dpkg-sig --verify rippled_2.0.0~b2-1_amd64.deb
    Processing rippled_2.0.0~b2-1_amd64.deb...
    GOODSIG _gpgbuilder C0010EC205B35A3310DC90DE395F97FFCCAFD9A2 1696898690
    """
    if verified.valid:
        print(f"{verified.status} {verified.fingerprint} for {verified.username}")
    else:
        exit(1)
    return result

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
                        prog='package_signer',
                        description='signs packages')
    parser.add_argument('package_file')
    args = parser.parse_args()
    sign_package(args.package_file)

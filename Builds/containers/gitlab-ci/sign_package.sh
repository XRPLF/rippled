#!/usr/bin/env bash

set -eo pipefail

sign_dpkg() {
  if [ -n "${GPG_KEYID}" ]; then
    dpkg-sig \
      -g "--no-tty --digest-algo 'sha512' --passphrase '${GPG_PASSPHRASE}' --pinentry-mode=loopback" \
			-k "${GPG_KEYID}" \
			--sign builder \
			"out/deb/${PACKAGE_NAME}_${PACKAGE_ARCH}.deb"
	fi
}

sign_rpm() {
  if [ -n "${GPG_KEYID}" ] ; then
	echo "yes" | setsid rpm \
			--define "_gpg_name ${GPG_KEYID}" \
			--define "_signature gpg" \
			--define "__gpg_check_password_cmd /bin/true" \
			--define "__gpg_sign_cmd %{__gpg} gpg --batch --no-armor --digest-algo 'sha512' --passphrase '${GPG_PASSPHRASE}' --pinentry-mode=loopback --no-secmem-warning -u '%{_gpg_name}' --sign --detach-sign --output %{__signature_filename} %{__plaintext_filename}" \
			--addsign "out/rpm/${PACKAGE_NAME}_${PACKAGE_ARCH}.rpm"
	fi
}

case "${1}" in
    dpkg)
        sign_dpkg
        ;;
    rpm)
        sign_rpm
        ;;
    *)
        echo "Usage: ${0} (dpkg|rpm)"
        ;;
esac


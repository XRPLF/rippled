#!/usr/bin/env bash

set -eo pipefail

sign_dpkg() {
  if [ -n "${GPG_KEYID}" ]; then
    dpkg-sig \
      -g "--no-tty --digest-algo 'sha512' --passphrase '${GPG_PASSPHRASE}' --pinentry-mode=loopback" \
			-k "${GPG_KEYID}" \
			--sign builder \
			"build/dpkg/packages/*.deb"
	fi
}

sign_rpm() {
  if [ -n "${GPG_KEYID}" ] ; then
    find build/rpm/packages -name "*.rpm" -exec bash -c '
	echo "yes" | setsid rpm \
			--define "_gpg_name ${GPG_KEYID}" \
			--define "_signature gpg" \
			--define "__gpg_check_password_cmd /bin/true" \
			--define "__gpg_sign_cmd %{__gpg} gpg --batch --no-armor --digest-algo 'sha512' --passphrase '${GPG_PASSPHRASE}' --no-secmem-warning -u '%{_gpg_name}' --sign --detach-sign --output %{__signature_filename} %{__plaintext_filename}" \
			--addsign '{} \;
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


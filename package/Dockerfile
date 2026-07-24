ARG BASE_IMAGE=debian:bookworm

FROM ${BASE_IMAGE}

# Packaging runs in a vanilla distro image, so the tooling has to come
# from the distro's archive: debhelper for deb, rpm-build (and the
# systemd / find-debuginfo macros it depends on) for rpm.
# The container also uses git (real history) for
# build_pkg.sh's SOURCE_DATE_EPOCH; otherwise it falls back to a tarball
# download and the timestamp comes from wall-clock time.

COPY package/install-packaging-tools.sh /tmp/install-packaging-tools.sh

RUN /tmp/install-packaging-tools.sh

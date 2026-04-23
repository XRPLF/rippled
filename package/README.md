# Linux Packaging

This directory contains all files needed to build RPM and Debian packages for `xrpld`.

## Directory layout

```
package/
  build_pkg.sh      Staging and build script (called by CMake targets and CI)
  rpm/
    xrpld.spec      RPM spec (xrpld_version/pkg_release passed via rpmbuild --define)
  deb/
    debian/         Debian control files (control, rules, install, links, conffiles, ...)
  shared/
    xrpld.service       systemd unit file (used by both RPM and DEB)
    xrpld.sysusers      sysusers.d config (used by both RPM and DEB)
    xrpld.tmpfiles      tmpfiles.d config (used by both RPM and DEB)
    xrpld.logrotate     logrotate config (installed to /opt/xrpld/bin/, user activates)
    update-xrpld.sh     auto-update script (installed to /opt/xrpld/bin/)
    update-xrpld-cron   cron entry for auto-update (installed to /opt/xrpld/bin/)
  test/
    smoketest.sh            Package install smoke test
    check_install_paths.sh  Verify install paths and compat symlinks
```

## Prerequisites

Packaging targets and their container images are declared in
[`.github/scripts/strategy-matrix/linux.json`](../.github/scripts/strategy-matrix/linux.json)
via a `"packaging"` field on specific os entries. The image tag is composed
as `ghcr.io/xrplf/ci/{distro}-{version}:{compiler}-{cver}-sha-{image_sha}` —
the same scheme used by `reusable-build-test.yml`. Bump `image_sha` in
`linux.json` and both CI and local builds pick up the new image with no
workflow edits.

| Package type | Image (derived from `linux.json`)                    | Tool required                                                   |
| ------------ | ---------------------------------------------------- | --------------------------------------------------------------- |
| RPM          | `ghcr.io/xrplf/ci/rhel-9:gcc-12-sha-<git_sha>`       | `rpmbuild`                                                      |
| DEB          | `ghcr.io/xrplf/ci/ubuntu-jammy:gcc-12-sha-<git_sha>` | `dpkg-buildpackage`, `debhelper (>= 13)`, `dh-sequence-systemd` |

To print the exact image tags for the current `linux.json`:

```bash
./.github/scripts/strategy-matrix/generate.py --packaging --config=.github/scripts/strategy-matrix/linux.json
```

## Building packages

### Via CI

Caller workflows (`on-pr.yml`, `on-tag.yml`, `on-trigger.yml`) call
`reusable-strategy-matrix.yml` with `mode: packaging` to generate the matrix of
`{pkg_type, artifact_name, container_image}` entries, then fan out to
`reusable-package.yml` per entry. That workflow downloads the pre-built `xrpld`
binary artifact and calls `build_pkg.sh` directly — no CMake configure or
build step is needed inside the packaging job.

### Locally (mirrors CI)

With an `xrpld` binary already built at `build/xrpld`, run the packaging step
inside the same container CI uses. The image tag is derived from `linux.json`
so you don't need to hardcode a SHA.

```bash
# From the repo root.
PKG_TYPE=deb                # or rpm
VERSION=2.4.0-local
PKG_RELEASE=1

# Derive the correct image for this package type from linux.json.
IMAGE=$(jq -r --arg pkg "$PKG_TYPE" '
  .os[] | select((.packaging // []) | index($pkg)) |
  "ghcr.io/xrplf/ci/\(.distro_name)-\(.distro_version):\(.compiler_name)-\(.compiler_version)-sha-\(.image_sha)"
' .github/scripts/strategy-matrix/linux.json)

# Run the packaging in the container.
docker run --rm \
  -v "$(pwd):/src" \
  -w /src \
  "$IMAGE" \
  ./package/build_pkg.sh "$PKG_TYPE" . build "$VERSION" "$PKG_RELEASE"

# Output:
#   build/debbuild/*.deb         (DEB + dbgsym .ddeb)
#   build/rpmbuild/RPMS/x86_64/*.rpm
```

### Via CMake (host-side target)

If you run CMake configure on a host that has `rpmbuild` or `dpkg-buildpackage`
installed natively, you can use the CMake targets directly — no container
needed, but the host toolchain replaces the pinned CI image:

```bash
cmake \
  -DCMAKE_INSTALL_PREFIX=/opt/xrpld \
  -Dxrpld=ON \
  -Dxrpld_version=2.4.0-local \
  -Dtests=OFF \
  ..

cmake --build . --target package-rpm    # requires rpmbuild
cmake --build . --target package-deb    # requires dpkg-buildpackage
```

The `cmake/XrplPackaging.cmake` module gates each target on whether the
required tool is present at configure time, so configuring on a host that
lacks one simply omits the corresponding target. `CMAKE_INSTALL_PREFIX` must
be `/opt/xrpld`; if it is not, both targets are skipped with a `STATUS`
message.

## How `build_pkg.sh` works

`build_pkg.sh <pkg_type> <src_dir> <build_dir> [version] [pkg_release]` stages
all files and invokes the platform build tool. It resolves `src_dir` and
`build_dir` to absolute paths, then calls `stage_common()` to copy the binary,
config files, and shared support files into the staging area.

### RPM

1. Creates the standard `rpmbuild/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}` tree inside the build directory.
2. Copies `xrpld.spec` and all source files (binary, configs, service files) into `SOURCES/`.
3. Runs `rpmbuild -bb --define "xrpld_version ..." --define "pkg_release ..."`. The spec uses manual `install` commands to place files.
4. Output: `rpmbuild/RPMS/x86_64/xrpld-*.rpm`

### DEB

1. Creates a staging source tree at `debbuild/source/` inside the build directory.
2. Stages the binary, configs, `README.md`, and `LICENSE.md`.
3. Copies `package/deb/debian/` control files into `debbuild/source/debian/`.
4. Copies shared service/sysusers/tmpfiles into `debian/` where `dh_installsystemd`, `dh_installsysusers`, and `dh_installtmpfiles` pick them up automatically.
5. Generates a minimal `debian/changelog` (pre-release versions use `~` instead of `-`).
6. Runs `dpkg-buildpackage -b --no-sign`. `debian/rules` uses manual `install` commands.
7. Output: `debbuild/*.deb` and `debbuild/*.ddeb` (dbgsym package)

## Post-build verification

```bash
# DEB
dpkg-deb -c debbuild/*.deb | grep -E 'systemd|sysusers|tmpfiles'
lintian -I debbuild/*.deb

# RPM
rpm -qlp rpmbuild/RPMS/x86_64/*.rpm
```

## Reproducibility

The following environment variables improve build reproducibility. They are not
set automatically by `build_pkg.sh`; set them manually if needed:

```bash
export SOURCE_DATE_EPOCH=$(git log -1 --pretty=%ct)
export TZ=UTC
export LC_ALL=C.UTF-8
export GZIP=-n
export DEB_BUILD_OPTIONS="noautodbgsym reproducible=+fixfilepath"
```

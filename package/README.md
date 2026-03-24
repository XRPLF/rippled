# Linux Packaging

This directory contains all files needed to build RPM and Debian packages for `xrpld`.

## Directory layout

```
package/
  build_pkg.sh      Staging and build script (called by CMake targets and CI)
  rpm/
    xrpld.spec.in   RPM spec template (substitutes @xrpld_version@, @pkg_release@)
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

| Package type | Container                              | Tool required                                                   |
| ------------ | -------------------------------------- | --------------------------------------------------------------- |
| RPM          | `ghcr.io/xrplf/ci/rhel-9:gcc-12`       | `rpmbuild`                                                      |
| DEB          | `ghcr.io/xrplf/ci/ubuntu-jammy:gcc-12` | `dpkg-buildpackage`, `debhelper (>= 13)`, `dh-sequence-systemd` |

## Building packages

### Via CI (recommended)

The `reusable-package.yml` workflow downloads a pre-built `xrpld` binary artifact
and calls `build_pkg.sh` directly. No CMake configure or build step is needed in
the packaging job.

### Via CMake (local development)

Configure with the required install prefix, then invoke the target:

```bash
cmake \
  -DCMAKE_INSTALL_PREFIX=/opt/xrpld \
  -Dxrpld=ON \
  -Dtests=OFF \
  ..

# RPM (in RHEL container):
cmake --build . --target package-rpm

# DEB (in Debian/Ubuntu container):
cmake --build . --target package-deb
```

The `cmake/XrplPackaging.cmake` module gates each target on whether the required
tool (`rpmbuild` / `dpkg-buildpackage`) is present at configure time, so
configuring on a host that lacks one simply omits the corresponding target.

`CMAKE_INSTALL_PREFIX` must be `/opt/xrpld`; if it is not, both targets are
skipped with a `STATUS` message.

## How `build_pkg.sh` works

`build_pkg.sh <pkg_type> <src_dir> <build_dir> [version] [pkg_release]` stages
all files and invokes the platform build tool. It resolves `src_dir` and
`build_dir` to absolute paths, then calls `stage_common()` to copy the binary,
config files, and shared support files into the staging area.

### RPM

1. Creates the standard `rpmbuild/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}` tree inside the build directory.
2. Copies the generated `xrpld.spec` and all source files (binary, configs, service files) into `SOURCES/`.
3. Runs `rpmbuild -bb`. The spec uses manual `install` commands to place files.
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

## TODO

- Port debsigs signing instructions and integrate into CI.
- Port RPM GPG signing setup (key import + `%{?_gpg_sign}` in spec).
- Introduce a virtual package for key rotation.

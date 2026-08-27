# Linux Packaging

This directory contains all files needed to build RPM and Debian packages for
`xrpld`. The packages also ship the `validator-keys` tool, so packaging requires
a build configured with `-Dvalidator_keys=ON`.

## Directory layout

```
package/
  build_pkg.py        Staging and build script (called by the CMake `package` target and CI)
  sign_rpm.py         Signs the built RPMs (called by CI when publishing)
  docker/
    Dockerfile          Packaging image, built by `build-packaging-images.yml`; installs its tooling with `bin/install-packaging-tools.sh`
    publish_pkg.py      Uploads built packages to the XRPLF Nexus repositories (called by CI, and shipped in that image)
  rpm/
    xrpld.spec      RPM spec
  debian/           Debian control files (control, rules, copyright, xrpld.docs, xrpld.links, source/format)
  shared/
    xrpld.service       systemd unit file (used by both RPM and DEB)
    xrpld.sysusers      sysusers.d config (used by both RPM and DEB)
    xrpld.tmpfiles      tmpfiles.d config (used by both RPM and DEB)
    xrpld.logrotate     logrotate config (installed to /etc/logrotate.d/xrpld)
```

## Prerequisites

Packaging is declared on the build configs themselves, in
[`.github/scripts/strategy-matrix/linux.json`](../.github/scripts/strategy-matrix/linux.json):
a config that is also packaged carries a `package` map, so its binaries and its
packaging job cannot drift apart. Today only `linux/amd64` is emitted. The map
pins the full container image in `image` — edit that field to move to a new
image and both CI and local builds pick it up — and names the format that image
builds in `type`, which CI passes to `build_pkg.py` as `--package-type`; the two
have to stay in step.

| Package type | Image (`configs.<distro>[].package.image` in `linux.json`) | Tools required                                      |
| ------------ | ---------------------------------------------------------- | --------------------------------------------------- |
| RPM          | `ghcr.io/xrplf/xrpld/packaging-rhel:sha-<sha>`             | `rpmbuild`, `rpmsign`                               |
| DEB          | `ghcr.io/xrplf/xrpld/packaging-debian:sha-<sha>`           | `dpkg-buildpackage`, debhelper with compat level 13 |

To print the full packaging matrix (artifact names and images) for the current
`linux.json`:

```bash
./.github/scripts/strategy-matrix/generate.py --packaging
```

## Building packages

### Via CI

Caller workflows (`on-pr.yml`, `on-tag.yml`, `on-trigger.yml`) call
`reusable-package.yml`. That workflow generates its own packaging matrix from
the configs that carry a `package` map (via `generate.py --packaging`) and fans
out one job per distro. Each job downloads the pre-built `xrpld` and
`validator-keys` binary artifacts and runs in that distro's container, building
the format `package.type` declares. The packaging script derives the package
version from the downloaded binary's `xrpld --version` output; no CMake
configure or build step is needed inside the packaging job.

The binaries come from the `debian` and `rhel` build configs themselves — the
ones carrying the `package` map — which pass `-Dvalidator_keys=ON` so that the
build job produces `validator-keys` next to `xrpld` and uploads it as the
`validator-keys-<config name>` artifact. The packaging matrix names both
artifacts (`xrpld_artifact_name` and `validator_keys_artifact_name`) after that
same config, so a packaged config must keep `-Dvalidator_keys=ON`. Those configs
are not `minimal`, so `on-pr.yml` only packages once a PR runs the full matrix.

`validator-keys` is fetched from an exact commit pinned in
[`cmake/XrplValidatorKeys.cmake`](../cmake/XrplValidatorKeys.cmake), so a given
`xrpld` version always packages the same tool; bump that commit deliberately.

### Locally (mirrors CI)

With `xrpld` and `validator-keys` binaries already built at `build/xrpld` and
`build/validator-keys`, run the packaging step inside the same container CI uses.
The image tag is derived from `linux.json` so you don't need to hardcode a SHA.

```bash
# From the repo root. Each distro's container image is the `package.image` field
# of its config in linux.json. Example for the rpm-producing image (use
# .configs.debian[0].package.image and --package-type deb for the other one):
IMAGE=$(jq -r '.configs.rhel[0].package.image' .github/scripts/strategy-matrix/linux.json)

PKG_RELEASE=1

docker run --rm \
    -v "$(pwd):/src" \
    -w /src \
    "${IMAGE}" \
    ./package/build_pkg.py \
    --package-type rpm \
    --pkg-release "${PKG_RELEASE}" \
    --channel UNRELEASED

# Output:
#   build/debbuild/*.deb         (DEB + dbgsym; Debian names both .deb)
#   build/rpmbuild/RPMS/x86_64/*.rpm
```

### Via CMake (host-side target)

If you run CMake configure on a host that has `rpmbuild` or `dpkg-buildpackage`
installed natively, you can use the CMake target directly — no container
needed, but the host toolchain replaces the pinned CI image:

```bash
cmake \
    -Dxrpld=ON \
    -Dvalidator_keys=ON \
    -Dpkg_release=1 \
    -Dtests=OFF \
    ..

cmake --build . --target package # deb on Debian/Ubuntu, rpm on RHEL
```

The `cmake/XrplPackaging.cmake` module defines the `package` target only if at
least one of `rpmbuild` / `dpkg-buildpackage` is present and both the `xrpld` and
`validator-keys` targets exist (`-Dxrpld=ON -Dvalidator_keys=ON`); the target
builds both binaries before packaging, passing `--package-type deb` when
`dpkg-buildpackage` is present and `rpm` otherwise, and `--channel UNRELEASED`.
The packaging script installs to FHS-standard paths (`/usr/bin`, `/etc/xrpld`,
etc.) regardless of `CMAKE_INSTALL_PREFIX`.

The package version is not a CMake input on this path: `build_pkg.py` derives it
from the just-built `xrpld` binary's `xrpld --version` output. The package
release defaults to 1 and is overridable with `-Dpkg_release=N`.

## Publishing packages

Packages are published to the XRPLF repositories on Sonatype Nexus at
`https://packages.xrplf.org`. The `release-info` action decides the channel from
the event, and `publish_pkg.py` maps that channel to its repositories:

| Event                    | Version           | Channel   | DEB repository | RPM upload repository |
| ------------------------ | ----------------- | --------- | -------------- | --------------------- |
| tag                      | `X.Y.Z`           | `stable`  | `deb-stable`   | `rpm-stable-hosted`   |
| tag                      | `X.Y.Z-rcN`       | `rc`      | `deb-rc`       | `rpm-rc-hosted`       |
| tag                      | `X.Y.Z-bN`        | `beta`    | `deb-beta`     | `rpm-beta-hosted`     |
| push to `develop`        | `xrpld --version` | `develop` | `deb-develop`  | `rpm-develop-hosted`  |
| tag, non-public codebase | _any_             | `private` | `deb-private`  | `rpm-private-hosted`  |

Only a tag names a channel — do not extend that to `develop`, where
`BuildInfo.cpp`'s `versionString` moves through `-bN`, `-rcN` and even the final
version during a release cycle, which would send develop builds into `stable`.
Versions sort in row order, so moving to a more mature channel never downgrades.

The action decides the package release number on the same split: a tag's version
is unique, so its packages are release 1, while develop repeats the same version
and takes `<run number>.<commit date>git<commit hash>`, e.g.
`857.20260827gitb6a8995` — the leading run number keeps each push superseding
the last, and the date and hash say which commit a package on
`packages.xrplf.org` came from. Both reach the packaging scripts as arguments,
so neither script derives anything itself.

Publishing is the last step of each packaging job, uploading from the container
that built the packages with the `publish_pkg.py` shipped in the image — the
same copy other repositories run. The step itself always runs: without
`publish: true` it is a `--dry-run` that lists the uploads without needing
credentials, so every PR checks the repository routing. Uploads happen when the
caller passes `publish: true`: `on-trigger.yml` for develop pushes in
`XRPLF/rippled`, `on-tag.yml` for tags in any `XRPLF` repository, `on-pr.yml`
never. Both authenticate with the `NEXUS_REMOTE_USERNAME` /
`NEXUS_REMOTE_PASSWORD` secrets already used for the Conan remote.

Nexus owns the repository metadata; nothing here indexes anything. Worth knowing:

- Each apt-hosted repository needs a distribution (ours use `any`) and a PGP
  signing keypair configured in Nexus, which rejects one created without a
  keypair. Nexus signs the apt metadata with it, never the packages.
- Hosted yum repositories cannot be signed by Nexus, so each `rpm-<channel>-hosted`
  repository sits behind a `rpm-<channel>` yum group repository whose metadata
  Nexus signs. Uploads go to the hosted repository; clients point at the group
  and verify the metadata with `repo_gpgcheck=1`. Nexus never signs the RPMs
  themselves, so `sign_rpm.py` signs them before they are uploaded, and clients
  verify them with `gpgcheck=1`.
- yum metadata is rebuilt asynchronously, so a successful publish is not
  immediately installable.
- Each job uploads only what it built, and uploads are not transactional, so a
  failure can leave one format published alone. Re-running is safe: both the apt
  POST and the yum PUT replace an existing asset.
- The `develop` repositories gain a package per push, so they need a cleanup
  policy to stay bounded; tagged channels publish each version once.

### Publishing from other repositories

`publish_pkg.py` knows nothing about `xrpld`, so the packaging image
installs it at `/usr/local/bin/publish_pkg.py` for other XRPLF repositories that
build their packages elsewhere.

## How `build_pkg.py` works

`build_pkg.py` derives the `xrpld` software version from
`${BUILD_DIR}/xrpld --version` in both package formats.

The binary's version is already SemVer-validated by `BuildInfo`.
`build_pkg.py` converts pre-release versions such as `3.2.0-b1` or
`3.2.0-rc1` from `-` to `~` for package metadata so pre-releases sort before
the final release. If that normalized package version still contains `-`,
packaging fails because RPM forbids `-` in `Version`, and Debian uses `-` as
the upstream/revision separator.

`pkg_version` is the normalized package metadata version derived inside
`build_pkg.py` from the binary-reported `xrpld` version (`-` pre-release
separator converted to `~`). It is not a separate user input.

`PKG_RELEASE` is a different value: the package release iteration for that
`xrpld` version. RPM receives the normalized `pkg_version` and `PKG_RELEASE` as
the `pkg_version` and `pkg_release` macros for its `Version` and `Release`
values; DEB writes them as `${pkg_version}-${PKG_RELEASE}` in
`debian/changelog`.

With `PKG_RELEASE=1`, the package metadata becomes:

| Input version      | RPM version/release          | Debian version       |
| ------------------ | ---------------------------- | -------------------- |
| `3.2.0`            | `3.2.0-1%{?dist}`            | `3.2.0-1`            |
| `3.2.0-b0+abc1234` | `3.2.0~b0+abc1234-1%{?dist}` | `3.2.0~b0+abc1234-1` |
| `3.2.0-b1`         | `3.2.0~b1-1%{?dist}`         | `3.2.0~b1-1`         |
| `3.2.0-rc1`        | `3.2.0~rc1-1%{?dist}`        | `3.2.0~rc1-1`        |

`build_pkg.py` defines `dist` as `.el9` rather than letting rpmbuild take it
from the build host, so the RHEL image can track a newer release without
changing what the packages claim to target.

The Debian changelog entry carries the channel passed as `--channel`, which
only accepts the channels in the table above plus `UNRELEASED`, the Debian
convention for a build that targets no channel at all — what local and CMake
builds pass, since nothing publishes them. An unsupported pre-release, and
build metadata on a final release such as `3.2.0+abc123`, are both rejected.

The RPM path intentionally uses `~` in `Version`, matching the Debian
pre-release ordering convention, so RPM filenames/NVRs begin with forms like
`xrpld-3.2.0~b1-...` and `xrpld-3.2.0~rc1-...` instead of encoding
pre-releases with an older `0.<release>.<suffix>` RPM `Release` value.

The package format is `--package-type`, either `deb` or `rpm`. It is required,
so a job never silently builds the wrong format for the image it runs in; the
matching build tool still has to be on PATH.

Every input is a named argument, and every argument but `--build-dir` and
`--pkg-release` is required. The repository root is not an argument
at all: the script reads it from its own location. Only secrets stay in the
environment, so they never reach the process list -- `PKG_SIGNING_KEY` for
`sign_rpm.py`, and `NEXUS_USERNAME` / `NEXUS_PASSWORD` for `publish_pkg.py`.

Signing is not part of this script. `sign_rpm.py` does it in a separate CI step
that only runs when publishing, so a published RPM is always signed and a local
build never needs a key.

It resolves the build directory to an absolute path, then calls
`stage_common()` to copy the `xrpld` and `validator-keys` binaries, config files,
and shared support files into the staging area, and invokes the platform build
tool. Both binaries must be present in the build directory and must run in the
packaging environment; a missing or non-runnable one fails early. That runtime
check is what catches a binary still linked against the Nix store's ELF loader (see
`patch_nix_binary` in `cmake/PatchNixBinary.cmake`).

### RPM

1. Creates the standard `rpmbuild/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}` tree inside the build directory.
2. Copies `xrpld.spec` and all shared source files (binaries, configs, service files) into `SOURCES/`.
3. Runs `rpmbuild -bb`, passing the normalized package metadata version as the
   `pkg_version` RPM macro and `PKG_RELEASE` as the `pkg_release` RPM macro.
   The spec uses manual `install` commands to place files, disables `dwz`, and
   generates debuginfo packages.
4. Output: `rpmbuild/RPMS/x86_64/xrpld-*.rpm`

RPM upgrades intentionally do not restart a running `xrpld` service. The spec
uses `%systemd_postun`, matching Debian's `dh_installsystemd
--no-stop-on-upgrade` behavior; operators pick up the new binary on the next
service restart.

### DEB

1. Creates a staging source tree at `debbuild/source/` inside the build directory.
2. Stages the binaries, configs, `README.md`, `LICENSE.md`, and
   `validator-keys-LICENSE`.
3. Copies `package/debian/` control files into `debbuild/source/debian/`.
4. Copies shared service/sysusers/tmpfiles into `debian/` where `dh_installsystemd`, `dh_installsysusers`, and `dh_installtmpfiles` pick them up automatically.
5. Generates a minimal `debian/changelog` using `${pkg_version}-${PKG_RELEASE}`,
   where `pkg_version` is derived from the binary-reported `xrpld` version.
6. Runs `dpkg-buildpackage -b --no-sign -d` (`-d` skips the build-dependency check, since the binary is already built). `debian/rules` uses manual `install` commands.
7. Output: `debbuild/*.deb`, the binary package and the `-dbgsym` package.
   Debian gives dbgsym packages a `.deb` extension; only Ubuntu uses `.ddeb`.

## Post-build verification

```bash
# DEB
dpkg-deb -c debbuild/*.deb | grep -E 'systemd|sysusers|tmpfiles'

# RPM
rpm -qlp rpmbuild/RPMS/x86_64/*.rpm

# Optional, and not in the packaging image: apt-get install -y lintian
lintian -I debbuild/*.deb
```

## Reproducibility

`build_pkg.py` sets `SOURCE_DATE_EPOCH` from the latest git commit time and
exports it; the RPM spec clamps file modification times to it via
`%build_mtime_policy`. The remaining variables
below further improve reproducibility but are _not_ set by the script — export
them yourself if needed:

```bash
export TZ=UTC
export LC_ALL=C.UTF-8
export GZIP=-n
export DEB_BUILD_OPTIONS="noautodbgsym reproducible=+fixfilepath"
```

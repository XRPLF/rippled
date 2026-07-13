# Nix CI Docker images

This directory builds the Docker images used by xrpld's Linux CI. Each image
bundles the **exact same toolchain that the Nix development shell provides**
(see [`docs/build/nix.md`](../../docs/build/nix.md)), so what runs in CI matches
what developers get locally from `nix develop`.

The toolchain (CMake, Ninja, Conan, GCC, Clang, clang-tidy, the
sanitizer/coverage tools, …) is defined in [`nix/packages.nix`](../packages.nix)
and assembled for CI by [`nix/ci-env.nix`](../ci-env.nix). The Docker build
turns that Nix environment into an ordinary container image layered on top of a
conventional base image (Ubuntu, Debian, RHEL, or `nixos/nix`).

## Images

The images are built by the [`build-nix-images.yml`](../../.github/workflows/build-nix-images.yml)
workflow and pushed to `ghcr.io/xrplf/xrpld/nix-<distro>`. The `<distro>` is
selected through the `BASE_IMAGE` build argument; the base images are the
**oldest supported version** of each distribution we target:

| Image        | `BASE_IMAGE`                                 | Notes                                              |
| ------------ | -------------------------------------------- | -------------------------------------------------- |
| `nix-nixos`  | `nixos/nix:latest`                           | Build/lint only; binaries are not run (see below). |
| `nix-ubuntu` | `ubuntu:20.04`                               | Oldest supported Ubuntu (glibc 2.31).              |
| `nix-debian` | `debian:bookworm`                            |                                                    |
| `nix-rhel`   | `registry.access.redhat.com/ubi9/ubi:latest` |                                                    |

All images carry the full toolchain on `PATH` (via `/nix/ci-env/bin`) plus the
CA bundle shipped in the Nix environment, so HTTPS clients (git, curl, Conan)
work without `ca-certificates` being installed in the base image.

## Build stages

[`Dockerfile`](./Dockerfile) is a multi-stage build:

1. **`builder`** — On a `nixos/nix` builder, evaluate the flake and build the
   CI environment (`nix/ci-env.nix`). The resulting Nix store closure (the
   complete set of store paths the toolchain depends on) is copied into a
   staging directory.
2. **`final`** — Start from `BASE_IMAGE`, copy in the Nix store closure and the
   `ci-env` symlink tree, and wire up `PATH` and the CA bundle. It then:
   - installs the dynamic linker if the base image lacks one (see
     [How libc is handled](#how-libc-is-handled)),
   - runs [`bin/check-tools.sh`](../../bin/check-tools.sh) to verify every
     expected tool is present and runnable, and
   - compiles the C++ test programs in
     [`test_files/`](./test_files) with both `g++` and `clang++`, and sanitizers.
3. **`tester`** — Start again from a clean `BASE_IMAGE` (no Nix toolchain),
   install only the sanitizer runtime libraries
   ([`install-sanitizer-libs.sh`](./install-sanitizer-libs.sh)), and run the
   binaries compiled in `final`. This proves the binaries built with the Nix
   toolchain actually run on a vanilla base image. On `nixos/nix` this step is
   skipped (the binaries are patched for a conventional FHS loader).
4. **Output** — The final image is gated on the tester succeeding: it copies a
   sentinel file out of `tester`, so a failed test run fails the whole build.

## How libc is handled

The goal is for binaries built in these images to run on the **oldest supported
base image** (Ubuntu 20.04, glibc 2.31) and newer — without the developer's Nix
toolchain being present at runtime. Two pieces make that work:

- **Compilers linked against an old glibc.** The Nix CI environment does not use
  nixpkgs' current glibc. Instead it pins a 2020 nixpkgs snapshot whose primary
  glibc is **2.31** (matching Ubuntu 20.04), via the `nixpkgs-custom-glibc`
  flake input. GCC, Clang, binutils and compiler-rt are all rebuilt/wrapped
  against this custom glibc (see [`nix/ci-env.nix`](../ci-env.nix)). As a result
  the libraries they emit (`libstdc++`, `libgcc_s`, the sanitizer runtimes)
  reference only symbols available in glibc 2.31.

- **An expected dynamic linker in the image.**
  Binaries built in Nix environments reference a dynamic linker from Nix store paths, which won't be present in the base image. However,
  [`loader-path.sh`](./loader-path.sh) reports the expected loader path for the
  current architecture, so we can patch the binaries to use the correct loader.

The build then verifies all of this end to end: the test programs in
`test_files/` (a regular binary plus ASan/TSan/UBSan variants) are compiled in
`final`, their `PT_INTERP` is patched to the target loader, and they are run in
the clean `tester` stage to confirm each emits the expected sanitizer
diagnostic on a stock base image.

## Files

| File                                                                    | Purpose                                                                       |
| ----------------------------------------------------------------------- | ----------------------------------------------------------------------------- |
| [`./Dockerfile`](./Dockerfile)                                          | Multi-stage build described above.                                            |
| [`./loader-path.sh`](./loader-path.sh)                                  | Print the dynamic-linker (`PT_INTERP`) path for the current architecture.     |
| [`./test_files/`](./test_files)                                         | C++ sources and scripts to compile and run the sanitizer smoke tests.         |
| [`/bin/check-tools.sh`](../../bin/check-tools.sh)                       | Verify every expected tools are present and runnable.                         |
| [`/bin/install-sanitizer-libs.sh`](../../bin/install-sanitizer-libs.sh) | Install `libasan`/`libtsan`/`libubsan` runtimes on the supported base images. |

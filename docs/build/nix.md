# Using Nix Development Shell for xrpld Development

This guide explains how to use Nix to set up a reproducible development environment for xrpld. Using Nix eliminates the need to manually install utilities and ensures consistent tooling across different machines.

**The Nix development shell is the recommended way to develop xrpld.** It unifies the development environment for everyone and synchronizes updates: the same tooling and compiler versions are used both here and in CI. Any custom environment (Homebrew packages or anything else) will continue to work, but then it is up to you to keep it in sync with the environment used in CI.

## Benefits of Using Nix

- **Reproducible environment**: Everyone gets the same versions of tools and compilers
- **Matches CI**: The Linux CI runs in Docker images built from this exact Nix environment, and CI builds some macOS configurations in it as well
- **No system pollution**: Dependencies are isolated and don't affect your system packages
- **Consistent compilers**: The GCC and Clang shells use the same versions as CI
- **Quick setup**: Get started with a single command
- **Works on Linux and macOS**: Consistent experience across platforms

## Install Nix

Please follow [the official installation instructions of nix package manager](https://nixos.org/download/) for your system.

## Entering the Development Shell

### Basic Usage

From the root of the xrpld repository, enter the default development shell:

```bash
nix --experimental-features 'nix-command flakes' develop
```

This will:

- Download and set up all required development tools (CMake, Ninja, Conan, etc.)
- Configure the appropriate compiler for your platform:
  - **Linux**: GCC (provided by Nix)
  - **macOS**: Clang (provided by Nix)

The first time you run this command, it will take a few minutes to download and build the environment. Subsequent runs will be much faster.

### Platform notes

- **Linux**: `nix develop` gives you a shell with all the tooling necessary to develop xrpld
  and with the same GCC/glibc toolchain that Nix builds for CI.
  See [Choosing a different compiler](#choosing-a-different-compiler)
  for the custom-vs-plain toolchain trade-off.
- **macOS**: `nix develop` gives you a full environment too, with Clang (and
  every other tool, including Conan) provided by Nix. To use your system-wide
  Apple Clang instead, enter `nix develop .#apple-clang`. Conan has no binary in
  the Nix cache for macOS, so it is built from source the first time you enter
  the shell, which makes the initial setup slower (this is handled
  automatically; see [`nix/devshell.nix`](../../nix/devshell.nix)).

> [!TIP]
> To avoid typing `--experimental-features 'nix-command flakes'` every time, you can permanently enable flakes by creating `~/.config/nix/nix.conf`:
>
> ```bash
> mkdir -p ~/.config/nix
> echo "experimental-features = nix-command flakes" >> ~/.config/nix/nix.conf
> ```
>
> After this, you can simply use `nix develop` instead.

> [!NOTE]
> The examples below assume you've enabled flakes in your config. If you haven't, add `--experimental-features 'nix-command flakes'` after each `nix` command.

### Choosing a different compiler

A compiler can be chosen by providing its name with the `.#` prefix, e.g. `nix develop .#clang`.

On Linux, `.#gcc` and `.#clang` provide the exact toolchain CI uses:
the compiler (pinned in [`nix/packages.nix`](../../nix/packages.nix))
rebuilt against the pinned custom glibc (see [`nix/linux.nix`](../../nix/linux.nix)).
Building that toolchain the first time is slow unless it is fetched from a Nix binary cache.
If you don't need the custom glibc, the Linux-only `.#gcc-plain` and `.#clang-plain`
give you the stock nixpkgs compilers of the same versions.
On macOS there is no custom glibc, so `.#gcc` and `.#clang` are already the plain nixpkgs toolchain,
and the `-plain` variants do not exist.

Use `nix flake show` to see all the available development shells.

Use `nix develop .#no-compiler` to use the compiler from your system.

### Example Usage

```bash
# Use GCC — same toolchain as CI (custom glibc on Linux)
nix develop .#gcc

# Use Clang — same toolchain as CI (custom glibc on Linux)
nix develop .#clang

# Use default for your platform
nix develop

# Stock nixpkgs GCC/Clang, Linux only — skips the custom-glibc build, but does not match CI
nix develop .#gcc-plain
nix develop .#clang-plain
```

### Using a different shell

`nix develop` opens bash by default. To use another shell, pass it with the `-c` flag — this works with any shell, e.g. `zsh` or `fish`:

```bash
# Use zsh
nix develop -c zsh

# Use fish
nix develop -c fish

# Use your login shell
nix develop -c "$SHELL"
```

> [!WARNING]
> Your shell's interactive startup files (e.g. `config.fish`, `.zshrc`) may prepend other directories — most commonly Homebrew — to `$PATH`, which can shadow the tools provided by the Nix shell. After entering, verify that tools resolve into the Nix store:
>
> ```bash
> command -v cmake   # should print a /nix/store/... path
> ```
>
> If it doesn't, either adjust your shell configuration so it doesn't override `$PATH`, or use [direnv](#automatic-activation-with-direnv) (below), which loads the environment _after_ your shell config and so takes precedence regardless of the shell you use.

## Building xrpld in the Nix shell

Once inside the Nix development shell, follow the standard [build instructions](../../BUILD.md#steps). The Nix shell provides all necessary tools (CMake, Ninja, Conan, etc.).

Coverage builds (`-Dcoverage=ON`) work in the `gcc` shell (and `gcc-plain` on Linux):
each ships a `gcov` matching its compiler, since Nix's cc-wrapper does not expose one.
The `clang` shells do not include `llvm-cov`, so use a `gcc` shell for coverage.

## Conan configuration

The shell runs [`conan/init.sh`](../../conan/init.sh) on entry, so
[Set Up Conan](../../BUILD.md#set-up-conan) is already done for you. It installs
into the shell's own Conan home: `CONAN_HOME=~/.conan2-nix`.

### Prebuilt packages

On **Linux**, the binaries on the `xrplf` remote are built in this same Nix
environment — CI runs in Docker images that bundle the dev shell's toolchain (see
[`nix/docker`](../../nix/docker)) — so `.#gcc` and `.#clang` can reuse them. The
`-plain` shells do not match that toolchain's glibc, so binaries from the remote
are not a reliable match there.

On **macOS**, CI also builds in this Nix environment, in Debug and Release (the
`macos-arm64-*-nix` configurations — Debug because the profile defaults to it).
The Nix build resolves to `compiler=clang`, so it gets its own package IDs,
separate from the Apple Clang ones. The
[dependency upload](../../.github/workflows/upload-conan-deps.yml) publishes them
on pushes to `develop` and on manual runs — its nightly run rebuilds everything
from source but uploads nothing — so once a set has been published `nix develop`
can reuse it instead of compiling every dependency locally. These configurations
run outside the reduced pull-request matrix, so label a PR `Full CI build` when it
touches `flake.lock` or `nix/`.

To compile everything from source, add `--build '*'` to the `conan install`
command.

### Why the nixpkgs revision is not part of the package ID

A Conan package ID records the compiler and its major version, but nothing about
the nixpkgs revision the toolchain came from — and `flake.lock` moves far more
often than the toolchain meaningfully changes, so folding it in would rebuild
every dependency on every bump for nothing.

That is safe as long as no cached artifact resolves a `/nix/store` path at run
time, because store paths change on every update and the old ones disappear with
`nix-collect-garbage`. With the `clang` toolchain macOS CI and the dev shell use,
they do not: it links against `/usr/lib/libc++` and `/usr/lib/libSystem`, and
store paths reach the `.a` files only through debug info, which nothing resolves
at link or run time.

> [!WARNING]
> This does not hold for `nix develop .#gcc` on macOS. There is no system
> libstdc++, so GCC links its own from the store and every binary keeps a
> `/nix/store` reference. That shell is fine for tooling, but it is not a build
> configuration CI covers, and no dependency binaries are published for it.

This is checked rather than assumed.
[`bin/check-nix-store-refs.sh`](../../bin/check-nix-store-refs.sh) takes one file
or directory and fails if a binary under it resolves a store path at run time.
CI runs it over the build output and the Conan cache, and again in the upload job
before anything is published. You can run it yourself:

```bash
bin/check-nix-store-refs.sh build
bin/check-nix-store-refs.sh ~/.conan2-nix
```

It works on Linux too, but asserts something narrower there: the toolchain always
writes the store into `PT_INTERP` and `RUNPATH`, and CI builds inside an image
whose store is fixed for its lifetime, so that is fine. Only the binaries
[`PatchNixBinary.cmake`](../../cmake/PatchNixBinary.cmake) retargets to the
system loader have to be clean, and those are what CI checks:

```bash
bin/check-nix-store-refs.sh build/xrpld
```

### The libresolv stub

This is not hypothetical: `xrpld` used to be caught by it. The c-ares package
tells the linker to pass `-lresolv`, and nixpkgs keeps `libresolv` out of the
macOS SDK and ships it as an ordinary store dylib — so every Nix-built `xrpld`
recorded a `/nix/store/…-libresolv-93/lib/libresolv.9.dylib` load command and
stopped running once that path was collected. Nothing in the link uses a single
symbol from it.

Both environments now put a stub on the linker search path
(`libresolvSystemStub` in [`nix/darwin.nix`](../../nix/darwin.nix)): the
same library with its install name set to `/usr/lib/libresolv.9.dylib`, which is
exactly the load command the Apple Clang build records.

Package IDs did not change, so Conan keeps serving anything built before the
stub landed. If a binary fails to start with `Library not loaded: /nix/store/…`,
see [that entry](./nix_troubleshooting.md#library-not-loaded-nixstore-from-a-binary-that-used-to-work)
in the troubleshooting guide.

## Automatic Activation with direnv

[direnv](https://direnv.net/) or [nix-direnv](https://github.com/nix-community/nix-direnv) can automatically activate the Nix development shell when you enter the repository directory.

This is also the most robust way to use the environment from **any shell** (bash, zsh, fish, …): direnv stays in your current shell and loads the environment _after_ your shell's startup files have run, so the Nix-provided tools take precedence over anything your shell configuration adds to `$PATH`.

The repository already ships an `.envrc` at its root that activates the Nix flake development shell, so you don't need to create one. To use it:

1. [Install direnv](https://direnv.net/docs/installation.html) and [hook it into your shell](https://direnv.net/docs/hook.html) (bash, zsh, fish, …). Installing [nix-direnv](https://github.com/nix-community/nix-direnv) as well is recommended: it caches the shell so that activation is near-instant after the first run.
2. Run `direnv allow` once in the repository root. direnv will then load (and reload) the Nix development shell automatically whenever you enter the directory.

> [!NOTE]
> direnv only caches the `.direnv` directory (already listed in `.gitignore`); no other repository files are affected.

## Updating `flake.lock` file

To update `flake.lock` to the latest revision use `nix flake update` command.

## Tooling snapshots

The tool versions in each Nix environment are recorded in
[`nix/check-tools/`](../../nix/check-tools) and verified by CI. If you change the
environment (bump the CI image tag, update `flake.lock`, or edit the tool list in
`bin/check-tools.sh`), CI fails until you regenerate and commit the affected
snapshot — see [`nix/check-tools/README.md`](../../nix/check-tools/README.md).

## Troubleshooting

See [Troubleshooting Nix problems](./nix_troubleshooting.md) for common issues,
such as `nix develop` failing inside Git worktrees.

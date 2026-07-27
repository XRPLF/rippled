# Using Nix Development Shell for xrpld Development

This guide explains how to use Nix to set up a reproducible development environment for xrpld. Using Nix eliminates the need to manually install utilities and ensures consistent tooling across different machines.

**The Nix development shell is the recommended way to develop xrpld.** It unifies the development environment for everyone and synchronizes updates: the same tooling and compiler versions are used both here and in CI. Any custom environment (Homebrew packages or anything else) will continue to work, but then it is up to you to keep it in sync with the environment used in CI.

## Benefits of Using Nix

- **Reproducible environment**: Everyone gets the same versions of tools and compilers
- **Matches CI**: The Linux CI runs in Docker images built from this exact Nix environment
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
rebuilt against the pinned custom glibc (see [`nix/compilers.nix`](../../nix/compilers.nix)).
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

## Building xrpld with Nix

Once inside the Nix development shell, follow the standard [build instructions](../../BUILD.md#steps). The Nix shell provides all necessary tools (CMake, Ninja, Conan, etc.).

Coverage builds (`-Dcoverage=ON`) work in the `gcc` shell (and `gcc-plain` on Linux):
each ships a `gcov` matching its compiler, since Nix's cc-wrapper does not expose one.
The `clang` shells do not include `llvm-cov`, so use a `gcc` shell for coverage.

## Automatic Activation with direnv

[direnv](https://direnv.net/) or [nix-direnv](https://github.com/nix-community/nix-direnv) can automatically activate the Nix development shell when you enter the repository directory.

This is also the most robust way to use the environment from **any shell** (bash, zsh, fish, …): direnv stays in your current shell and loads the environment _after_ your shell's startup files have run, so the Nix-provided tools take precedence over anything your shell configuration adds to `$PATH`.

The repository already ships an `.envrc` at its root that activates the Nix flake development shell, so you don't need to create one. To use it:

1. [Install direnv](https://direnv.net/docs/installation.html) and [hook it into your shell](https://direnv.net/docs/hook.html) (bash, zsh, fish, …). Installing [nix-direnv](https://github.com/nix-community/nix-direnv) as well is recommended: it caches the shell so that activation is near-instant after the first run.
2. Run `direnv allow` once in the repository root. direnv will then load (and reload) the Nix development shell automatically whenever you enter the directory.

> [!NOTE]
> direnv only caches the `.direnv` directory (already listed in `.gitignore`); no other repository files are affected.

## Conan and Prebuilt Packages

Please note that there is no guarantee that binaries from conan cache will work when using nix. If you encounter any errors, please use `--build '*'` to force conan to compile everything from source:

```bash
conan install .. --output-folder . --build '*' --settings build_type=Release
```

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

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

- **Linux**: `nix develop` gives you a shell with all the tooling necessary to
  develop xrpld and with GCC 15.2 (also provided by Nix). There are no caveats.
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
The `.#gcc` and `.#clang` shells provide the same GCC and Clang versions used in CI
(pinned in [`nix/packages.nix`](../../nix/packages.nix)).
Use `nix flake show` to see all the available development shells.

Use `nix develop .#no-compiler` to use the compiler from your system.

### Example Usage

```bash
# Use GCC (same version as CI)
nix develop .#gcc

# Use Clang (same version as CI)
nix develop .#clang

# Use default for your platform
nix develop
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

## Troubleshooting

See [Troubleshooting Nix problems](./nix_troubleshooting.md) for common issues,
such as `nix develop` failing inside Git worktrees.

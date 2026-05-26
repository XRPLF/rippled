# Using Nix Development Shell for xrpld Development

This guide explains how to use Nix to set up a reproducible development environment for xrpld. Using Nix eliminates the need to manually install utilities and ensures consistent tooling across different machines.

## Benefits of Using Nix

- **Reproducible environment**: Everyone gets the same versions of tools and compilers
- **No system pollution**: Dependencies are isolated and don't affect your system packages
- **Multiple compiler versions**: Easily switch between different GCC and Clang versions
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
  - **macOS**: Apple Clang (default system compiler)
  - **Linux**: GCC 15

The first time you run this command, it will take a few minutes to download and build the environment. Subsequent runs will be much faster.

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

A compiler can be chosen by providing its name with the `.#` prefix, e.g. `nix develop .#gcc15`.
Use `nix flake show` to see all the available development shells.

Use `nix develop .#no_compiler` to use the compiler from your system.

### Example Usage

```bash
# Use GCC 14
nix develop .#gcc14

# Use Clang 19
nix develop .#clang19

# Use default for your platform
nix develop
```

### Using a different shell

`nix develop` opens bash by default. If you want to use another shell this could be done by adding `-c` flag. For example:

```bash
nix develop -c zsh
```

## Building xrpld with Nix

Once inside the Nix development shell, follow the standard [build instructions](../../BUILD.md#steps). The Nix shell provides all necessary tools (CMake, Ninja, Conan, etc.).

## Automatic Activation with direnv

[direnv](https://direnv.net/) or [nix-direnv](https://github.com/nix-community/nix-direnv) can automatically activate the Nix development shell when you enter the repository directory.

## Conan and Prebuilt Packages

Please note that there is no guarantee that binaries from conan cache will work when using nix. If you encounter any errors, please use `--build '*'` to force conan to compile everything from source:

```bash
conan install .. --output-folder . --build '*' --settings build_type=Release
```

## Updating `flake.lock` file

To update `flake.lock` to the latest revision use `nix flake update` command.

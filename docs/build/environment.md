Our [build instructions][BUILD.md] assume you have a C++ development
environment complete with Git, Python, Conan, CMake, and a C++ compiler.
This document explains how to set one up.

[BUILD.md]: ../../BUILD.md

## Tested compiler versions

`xrpld` is built in the **C++23** dialect by default.
Make sure your toolchain is recent enough — the compiler versions currently tested in CI are:

| Compiler    | Version |
| ----------- | ------- |
| GCC         | 15.2    |
| Clang       | 22      |
| Apple Clang | 17      |
| MSVC        | 19.44   |

LLVM tools (`clang-tidy` and `clang-format`) are also pinned to version 22.

Older compilers may fail to build the latest `develop` code: the codebase now
relies on C++23 features and has been adjusted for `clang-tidy`.
If the latest code doesn't build for you, update your build toolchain first.

## Linux and macOS

The **recommended way** to get a development environment on Linux and macOS is
the Nix development shell. It provides the exact tooling used in CI — `git`,
`python`, `conan`, `cmake`, `clang-tidy`, `clang-format`, and everything else —
with a single command and without installing anything system-wide:

```bash
nix --experimental-features 'nix-command flakes' develop
```

On **Linux**, Nix also provides the compiler (GCC). On **macOS**, the shell uses
your **system-wide Apple Clang** as the compiler, so you still need to manage
its version (see below).

See [Using the Nix development shell](./nix.md) for installation and usage
details, including how to select a different compiler.

> [!NOTE]
> Using Nix is not mandatory. Any custom environment (Homebrew packages or
> anything else) will continue to work, but then it is up to you to keep it in
> sync with the environment used in CI. Nix unifies the development environment
> for everyone and synchronizes updates, which is why we recommend it.

### macOS: managing the Apple Clang version

Because the Nix shell uses the system-wide Apple Clang on macOS, the compiler
version is whatever your installed Xcode (or Command Line Tools) provides. The
following command should return a version greater than or equal to the
[minimum required](#tested-compiler-versions):

```bash
clang --version
```

If you develop other applications using Xcode, you might be consistently
updating to the newest version of Apple Clang, which will likely cause issues
building xrpld. You may want to install and pin a specific version of Xcode:

1. **Download Xcode**
   - Visit [Apple Developer Downloads](https://developer.apple.com/download/more/)
   - Sign in with your Apple Developer account
   - Search for an Xcode version that includes the expected Apple Clang version
   - Download the `.xip` file

2. **Install and configure Xcode**

   ```bash
   # Extract the .xip file and rename for version management
   # Example: Xcode_16.2.app

   # Move to Applications directory
   sudo mv Xcode_16.2.app /Applications/

   # Set as default toolchain (persistent)
   sudo xcode-select -s /Applications/Xcode_16.2.app/Contents/Developer

   # Set as environment variable (temporary)
   export DEVELOPER_DIR=/Applications/Xcode_16.2.app/Contents/Developer
   ```

## Windows

Nix is not available on Windows, so the required tools have to be installed
manually:

- [Visual Studio 2022](https://visualstudio.microsoft.com/) with the
  **"Desktop development with C++"** workload — this provides MSVC and the
  "x64 Native Tools Command Prompt".
- [Git for Windows](https://git-scm.com/download/win)
- [Python 3.11](https://www.python.org/downloads/), or higher
- [Conan 2.17](https://conan.io/downloads.html), or higher
- [CMake 3.22](https://cmake.org/download/), or higher

> [!NOTE]
> Windows is used for development only and is not recommended for production.

## Clang-tidy

`clang-tidy` is required to run static analysis checks locally (see
[CONTRIBUTING.md](../../CONTRIBUTING.md)). It is not required to build the
project. This project currently uses `clang-tidy` version 22.

On Linux and macOS, the [Nix development shell](./nix.md) provides `clang-tidy`
22 out of the box — run it via `run-clang-tidy`. No separate installation is
needed.

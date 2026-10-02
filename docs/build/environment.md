Our [build instructions][BUILD.md] assume you have a C++ development
environment complete with Git, Python, Conan, CMake, and a C++ compiler.
This document explains how to set one up.

[BUILD.md]: ../../BUILD.md

## Tested compiler versions

`xrpld` is built in the **C++23** dialect by default, so your toolchain has to
support it — see [compiler support for C++23][cpp23-support].
The versions currently tested in CI are:

| Compiler    | Version            |
| ----------- | ------------------ |
| GCC         | 15.2               |
| Clang       | 22                 |
| Apple Clang | 21                 |
| MSVC        | Visual Studio 2026 |

LLVM tools (`clang-tidy` and `clang-format`) are also pinned to version 22.

### Older compilers

Older compilers may fail to build the latest `develop` code: the codebase now
relies on C++23 features and has been adjusted for `clang-tidy`.
If the latest code doesn't build for you, update your build toolchain first.

If updating isn't an option for you, we do accept pull requests that fix builds
on older compilers, as long as the change is small and doesn't make the code
harder to read. What we can't promise is that older compilers will keep working:
only the versions in the table above are tested in CI, and we won't hold back
the use of C++23 features or add invasive workarounds to keep an untested
compiler building. Treat support for anything outside the table as best-effort.

## Required tools

Besides a compiler, building `xrpld` requires:

| Tool                                        | Minimum version |
| ------------------------------------------- | --------------- |
| [Git](https://git-scm.com/downloads)        | any recent      |
| [Python](https://www.python.org/downloads/) | 3.11            |
| [Conan](https://conan.io/downloads.html)    | 2.17            |
| [CMake](https://cmake.org/download/)        | 3.16            |

On Linux and macOS, the [Nix development shell](./nix.md) provides all of them
(see below). On Windows they have to be installed manually.

Building with `-Drust=ON` additionally requires a Rust toolchain, see
[Rust](#rust). A default build does not, so it is not in the table above.

Once they are in place, verify that everything is installed and runnable with:

```bash
./bin/check-tools.sh
```

## Linux and macOS

The **recommended way** to get a development environment on Linux and macOS is
the Nix development shell. It provides the exact tooling used in CI — `git`,
`python`, `conan`, `cmake`, `clang-tidy`, `clang-format`, and everything else —
with a single command and without installing anything system-wide:

```bash
nix --experimental-features 'nix-command flakes' develop
```

On **Linux**, Nix also provides the compiler (GCC); on **macOS**, it provides
Clang. If you instead opt to use your system-wide Apple Clang (via
`nix develop .#apple-clang`), you need to manage its version yourself (see
below).

See [Using the Nix development shell](./nix.md) for installation and usage
details, including how to select a different compiler and why we recommend Nix
over a hand-maintained environment.

### macOS: managing the Apple Clang version

If you use your system-wide Apple Clang on macOS (via `nix develop .#apple-clang`),
the compiler version is whatever your installed Xcode (or Command Line Tools)
provides. The following command should return a version greater than or equal to
the [tested one](#tested-compiler-versions):

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

- [Visual Studio 2026](https://visualstudio.microsoft.com/) with the
  **"Desktop development with C++"** workload — this provides MSVC and the
  "x64 Native Tools Command Prompt". CI configures CMake with the
  `Visual Studio 18 2026` generator.
- [Git for Windows](https://git-scm.com/download/win)
- Python, Conan, and CMake, at the versions listed in
  [Required tools](#required-tools).
- a [Rust toolchain](https://rustup.rs) — only needed to build with
  `-Drust=ON`, see [Rust](#rust)

## Rust

The repository contains a Rust workspace in [`crates/`](../../crates), whose
crates are exposed to C++ through [cxx](https://cxx.rs) bindings. It is **not**
part of a default build: the CMake `rust` option is OFF by default, and with it
off no Rust toolchain is needed. It is only required when configuring with
`-Drust=ON` (which is what CI does), see [Options](../../BUILD.md#options).

The toolchain (`cargo`, `rustc`) is pinned to the channel in
[`rust-toolchain.toml`](../../rust-toolchain.toml) at the repository root. If
you install Rust with [rustup](https://rustup.rs), that file is picked up
automatically, and `cargo`/`rustc` in the repository will use the pinned
version.

Everything else the Rust build needs on the CMake side comes from Conan along
with the rest of the dependencies, so there is nothing further to install.

## Clang-tidy

`clang-tidy` is required to run static analysis checks locally (see
[CONTRIBUTING.md](../../CONTRIBUTING.md)). It is not required to build the
project. The version this project uses is listed in
[Tested compiler versions](#tested-compiler-versions).

On Linux and macOS, the [Nix development shell](./nix.md) provides that exact
version out of the box — run it via `run-clang-tidy`. No separate installation
is needed.

[cpp23-support]: https://en.cppreference.com/w/cpp/compiler_support/23

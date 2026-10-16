Our [build instructions][BUILD.md] assume you have a C++ development
environment complete with Git, Python, Conan, CMake, and a C++ compiler.
This document explains how to set one up.

[BUILD.md]: ../../BUILD.md

## Supported development platforms

- **Linux**: the [Nix development shell](./nix.md),
  which provides the same toolchain CI uses on any distribution.
- **macOS** 15 (Sequoia) or newer: the [Nix development shell](./nix.md),
  or the system [Apple Clang](#macos-apple-clang).
  CI runs on macOS 26 (Tahoe), but the build defaults `CMAKE_OSX_DEPLOYMENT_TARGET` to 15.
- **Windows**: best effort, see [Windows](#windows).

For the platforms supported for running `xrpld`,
see [Requirements](../install.md#requirements).

## Tested compiler versions

`xrpld` is built in the **C++23** dialect by default, so your toolchain has to
support it — see [compiler support for C++23][cpp23-support].
The versions currently tested in CI are:

| Compiler    | Version                         |
| ----------- | ------------------------------- |
| GCC         | `gccVersion` in [packages.nix]  |
| Clang       | `llvmVersion` in [packages.nix] |
| Apple Clang | 21                              |
| MSVC        | Visual Studio 2026              |

LLVM tools (`clang-tidy` and `clang-format`)
come from the same LLVM release as Clang.

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

Besides a compiler, building `xrpld` requires
[Git](https://git-scm.com/downloads),
[Python](https://www.python.org/downloads/),
[Conan](https://conan.io/downloads.html),
and [CMake](https://cmake.org/download/).
Building with `-Drust=ON` additionally requires a Rust toolchain,
see [Rust](#rust).

On Linux and macOS, the [Nix development shell](./nix.md) provides all of them
at the versions CI uses, recorded in [`nix/check-tools/`](../../nix/check-tools).

Once they are in place, verify that everything is installed and runnable with:

```bash
./bin/check-tools.sh
```

## Linux and macOS

Use the [Nix development shell](./nix.md):
it provides the compiler and every tool CI uses,
without installing anything system-wide.

### macOS: Apple Clang

You can also build with the system Apple Clang,
installing the other [required tools](#required-tools) any way you like
(or getting them from `nix develop .#apple-clang`).
Its version is whatever your installed Xcode (or Command Line Tools) provides,
so check that `clang --version` is at least the [tested one](#tested-compiler-versions).
If several Xcode versions are installed,
select one with `sudo xcode-select -s <path>` or the `DEVELOPER_DIR` environment variable.

## Windows

Nix is not available on Windows, so the required tools have to be installed
manually:

- [Visual Studio 2026](https://visualstudio.microsoft.com/) with the
  **"Desktop development with C++"** workload — this provides MSVC and the
  "x64 Native Tools Command Prompt". CI configures CMake with the
  `Visual Studio 18 2026` generator.
- [Git for Windows](https://git-scm.com/download/win)
- recent releases of Python, Conan, and CMake
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

[cpp23-support]: https://en.cppreference.com/w/cpp/compiler_support/23
[packages.nix]: ../../nix/packages.nix

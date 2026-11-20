# Formal verification (Lean 4)

This folder holds a [Lean 4](https://lean-lang.org) model of parts of `xrpld`
and machine-checked theorems about that model. The goal is to state properties
of the protocol code (for example of `Number` arithmetic) precisely and have
Lean check their proofs.

Nothing here is part of a normal build: it is only used when the
`formal_verification` option is on (default off).

## Layout

```
formal_verification/
├── lean-toolchain        Lean version (leanprover/lean4:vX.Y.Z)
├── lakefile.toml         lake project: everything, with its mathlib dependency
├── lakefile-model.toml   lake project: only Model/ and FFI/, no dependency
├── lake-manifest.json    exact revisions of mathlib and its dependencies, for lake in this folder
├── XRPL.lean             root module, imports everything below
└── XRPL/
    ├── Model/            Lean definitions mirroring the C++ types and functions
    ├── Properties/       theorems about the model, with their proofs
    └── FFI/              the model functions exported to C++
```

`Model/` and `FFI/` use core Lean only, so what is linked into `xrpld` does not
depend on mathlib. Only `Properties/` imports it.

The three folders share the same sub-folders (e.g. `Protocol/`), so a
theorem or an export is found under the name of what it is about. A definition
in `Model/` should stay close to the C++ it mirrors; anything that only exists
to make a proof work belongs in `Properties/`.

## How the model is tied to the C++

A theorem only says something about `xrpld` if the model behaves like the C++.
That is checked by tests: the functions in `FFI/` are compiled and linked into
`xrpld`, and the suites in `src/test/formal_verification/` call both the Lean
function and the C++ one on the same inputs and compare the results.

The current content is one small example of the whole chain:

| Step    | Where                                                 | What                                               |
| ------- | ----------------------------------------------------- | -------------------------------------------------- |
| C++     | `include/xrpl/protocol/XRPAmount.h`                   | `XRPAmount::signum`                                |
| Model   | `XRPL/Model/Protocol/IntAmount.lean`                  | `IntAmount.signum`                                 |
| Theorem | `XRPL/Properties/Protocol/IntAmount.lean`             | `signum` is the sign of the amount's value         |
| Export  | `XRPL/FFI/Protocol/IntAmountFFI.lean`                 | `lean_int_amount_signum`                           |
| Test    | `src/test/formal_verification/LeanIntAmount_test.cpp` | the export and `XRPAmount::signum` return the same |

`IntAmount` models an amount that the C++ stores as one `std::int64_t`, such as
`XRPAmount` (a count of drops) or `MPTAmount`.

Run the tests with `./xrpld --unittest=formal_verification`.

The suites use the legacy Beast framework in `src/test/` rather than gtest: the
cross-validation suites that follow drive real vault and lending transactions
through the jtx test DSL, which only exists in `src/test/`.

## How it is built

Both the Lean toolchain and a prebuilt
[mathlib](https://github.com/leanprover-community/mathlib4), which takes hours
to compile, come from the `xrplf` Conan remote as the `lean4` and `mathlib4`
packages. See [BUILD.md](../BUILD.md#formal-verification) for the commands.

`xrpld` links only the model library, built from `lakefile-model.toml`, and the
Lean runtime from `lean4`. `mathlib4` is only used to check the proofs.

CMake builds in its own lake workspace in the build directory, never in this
folder, and always with the `lean4` package's `lake`. That workspace uses the
`lake-manifest.json` shipped in the `mathlib4` package, so it always matches
the prebuilt mathlib. The copy committed here is not read by CMake: it is for
an editor or `lake` run in this folder (see below).

## Working on the model and proofs

The proof check is the `lean4_proofs` target. It runs `lake build --wfail` on
the whole project against the `mathlib4` package, and fails on any error,
warning, or `sorry` (a `sorry` in a proof means to accept the proof as true,
even if it does not hold):

```bash
cmake --build . --target lean4_proofs
```

For an editor or `lake` in this folder, `nix develop .#formal-verification`
provides `lean` and `lake`. lake needs mathlib's prebuilt files under
`.lake/packages`, or it compiles the part of mathlib the proofs import (about
100 CPU-minutes).

Take them from the `mathlib4` Conan package, which a configured build
directory (here `.build`) has already downloaded:

```bash
mkdir -p .lake
ln -s "$(sed -n 's/^MATHLIB4_PACKAGES_DIR:STRING=//p' ../.build/CMakeCache.txt)" .lake/packages
lake build --wfail
```

`lake exe cache get`, the usual Lean way to download them from mathlib's
servers, does not work in this shell: it first builds a helper program, which
fails to link against the shell's older glibc.

With the link, `.lake/packages` is the Conan cache. lake only reads it as long
as `lake-manifest.json` pins the same revisions as the `mathlib4` package.
Nothing checks that, so keep the two in step when updating (see below), and do
not run `lake update` or `lake exe cache get` with the link in place: lake
would then fetch or rebuild mathlib inside the Conan cache.

For editor support, use the
[Lean 4 VS Code extension](https://marketplace.visualstudio.com/items?itemName=leanprover.lean4)
or any editor with Lean LSP support, opened on this folder.

## Updating Lean or mathlib

1. Publish `lean4` and `mathlib4` for the new version from
   [XRPLF/conan-center-index](https://github.com/XRPLF/conan-center-index).
2. Update the `lean4` and `mathlib4` versions in `conanfile.py`, then
   [regenerate `conan.lock`](../conan/lockfile/README.md).
3. Update `lean-toolchain`, the mathlib `rev` in `lakefile.toml`, and
   `lake-manifest.json` (copy the one the `mathlib4` package ships, keeping the
   root `name`).
4. Add the new release's githash to `leanReleaseGithashes` in
   `nix/packages.nix`.

The `lean4` and `mathlib4` binaries are specific to a compiler version, and
only XRPLF/conan-center-index builds them. Before a compiler bump in CI,
publish them for the new compiler from there, or a CI job that enables the
option builds mathlib from source.

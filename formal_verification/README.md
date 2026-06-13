# XRPL formal verification (Lean 4)

The Lean 4 model of XRPL, the theorems about it, and the FFI exports that the
C++ cross-validation tests call. For how the model, proofs, FFI, and build all
fit together, see
[docs/formal-verification/README.md](../docs/formal-verification/README.md).

## Layout

- `XRPL/Model/`: the Lean model of the C++ types (e.g. `Number`).
- `XRPL/Properties/`: theorems about the model.
- `XRPL/FFI/`: the `@[export]` wrappers the C++ tests call.

## Building

Nothing here is built by hand for normal use. The C++ cross-validation tests
link this model into `xrpld` through three Conan packages (`lean4`,
`xrpl-lean4-deps`, `xrpl-lean4`), gated by the `formal_verification_tests`
option. That path runs `lake` inside the Conan cache, so it needs no Lean or
elan install. See
[docs/formal-verification/README.md](../docs/formal-verification/README.md) for
the commands.

## Working on the proofs directly (optional)

Only needed when editing the Lean model or proofs by hand. This is the one path
that requires [elan](https://github.com/leanprover/elan) (it provides the Lean
version pinned in `lean-toolchain`):

```bash
lake exe cache get # fetch prebuilt mathlib artifacts, so mathlib isn't compiled
lake build         # build everything and check the proofs
```

`lake build` is the proof check: it passes only if every file elaborates with no
`sorry` and standard axioms.

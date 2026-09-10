# Rust crates

This directory holds the WebAssembly engine that runs Smart Escrow contracts,
bridged into C++ via `cxxbridge`/the `cxx` crate.

The workspace is built unconditionally — `add_subdirectory(crates)` in the
top-level `CMakeLists.txt` is not behind an option, and
`xrpl_wasm_vm_ffi_cxxbridge` is a `PUBLIC` dependency of
`xrpl.libxrpl.ledger` (see `cmake/XrplCore.cmake`). The Rust toolchain pinned in
[`rust-toolchain.toml`](../rust-toolchain.toml) is therefore required to build
`libxrpl` at all; the Nix devshell provides it automatically.

## The crates

Dependencies run in one direction: the ABI crate at the bottom, the engine on
top of it, and the two bridges at the edge.

### `xrpl-host-functions`

The wasm host ABI, declared exactly once. A `host_functions!` block at the
bottom of `src/lib.rs` generates the `HostFunctions` trait a host implements and
the `HostFunctionSpec` table a wasm engine registers from. Only the vocabulary
the declarations are written in — `HostError`, `HostResult`, `TraceDataType`,
`HASH_LEN` — is hand-written.

**Add or change a host function here**, never in the engine or the bridge: the
expansion names nothing this file does not, so neither side of the FFI boundary
gets to restate a signature.

`no_std`, because this crate is also what a guest contract links against.

### `xrpl-host-functions-macros`

The proc macro behind that block, plus the `wasmi_glue!` marshalling it
generates. An implementation detail of the crate above — nothing else should
depend on it.

The dev-dependency back on `xrpl-host-functions` is a deliberate cycle: the
doctests declare host functions returning `HostResult`, which the facade crate
hand-writes. Cargo allows it because dev-dependencies sit outside the library
build graph.

### `xrpl-wasm-vm`

The engine itself, on `wasmi`: preflight validation (`preflight/`), gas
metering and execution (`vm.rs`), and host-call dispatch (`abi.rs`, `args.rs`,
`register.rs`).

Two lint decisions are load-bearing, both because this is a consensus path:

- `forbid(unsafe_code)`, so "every guest access reaches linear memory only
  through wasmi's bounds-checked slice operations" is a property rather than a
  claim.
- The truncating, wrapping and sign-losing cast lints are `deny` and each
  remaining cast is argued for at its site — a bad cast here changes what a
  contract is charged or told.

It pins `wasmi` with `default-features = false` deliberately. wasmi's `wat`
feature is on by default and makes `Module::new` accept text as readily as
binary, which would turn a transaction's validity into a build flag.

Not bridged to C++ directly; it reaches `xrpld` through `xrpl-wasm-vm-ffi`.

### `xrpl-wasm-vm-ffi`

The cxx bridge into `xrpld`. Three crossings:

- **In:** C++ calls `run_escrow`, once per escrow finish.
- **Back out:** that run's host calls leave through the C++ `HostContext`, which
  `CxxHost` presents to the engine as an ordinary `HostFunctions` implementor.
- **In only:** C++ screens a module with `check_escrow`. Screening needs no
  host, so nothing comes back out.

The C++ side is `src/libxrpl/tx/wasm/WasmVM.cpp` and
`src/libxrpl/tx/wasm/HostContext.cpp`.

**Neither language may unwind into the other**, and the two halves are not
symmetric:

- A **Rust panic** is caught here, by `guarded`. Letting one reach C++ is
  undefined behaviour, and `[profile.release]` enables overflow checks, so this
  is a live path rather than a formality.
- A **C++ exception** is stopped on the C++ side: every `HostContext` method is
  `noexcept` and catches its own. That is what makes `guarded` sufficient.

Everything hand-written here is private, so `cargo doc` needs
`--document-private-items` to show any of it. That is also why this crate,
unlike `xrpl-wasm-vm`, does not `deny(unreachable_pub)` — cxx's expansion is
`pub` throughout by necessity.

### `xrpl-wasm-testkit`

**Test-only.** Assembles WebAssembly text for the C++ test suite, and exposes
the gas price of each host function by its guest import name for the C++ gas
benchmarks (read through the bridge rather than transcribed into C++, so the
numbers cannot drift silently).

A crate of its own rather than an entry on `xrpl-wasm-vm-ffi`, and the
separation is the point: putting `compile_wat` on the production bridge would
link `wat` into `xrpld` even if nothing called it. Linked only into
`xrpl_tests`, never into `libxrpl` or `xrpld`, so "no text assembler in the
shipped node" holds by the link graph rather than by a flag someone can flip.

## Testing

```bash
cargo test --manifest-path crates/Cargo.toml --workspace
```

CI uses `cargo nextest`. This is independent of the CMake build.

One gap that command does not cover: it never compiles `xrpl-host-functions`
with its `wasmi_glue` feature **off**, because `xrpl-wasm-vm` enables the
feature and Cargo unifies features across a workspace build. The feature-off
configuration is the one a guest contract sees, so after touching that crate
also run:

```bash
cargo check -p xrpl-host-functions --manifest-path crates/Cargo.toml
```

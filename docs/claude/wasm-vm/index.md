# Rust WASM VM — working docs

The Rust wasmi engine for programmable escrows on branch `Wasm-vm-redesign`, and the cxx
bridge that connects it to xrpld.

| Read | When |
|---|---|
| [bridge.md](bridge.md) | changing anything that crosses between C++ and Rust, or the TER map |
| [engine.md](engine.md) | changing `vm.rs`, `preflight.rs`, `abi.rs`, `region.rs` — the invariants, and the wasmi facts that decided them |
| [abi.md](abi.md) | adding or changing a host function |
| [testing.md](testing.md) | running the loop, or adding a test on either side |
| [conventions.md](conventions.md) | before writing code or comments in the crate |
| [open-questions.md](open-questions.md) | the undecided ABI questions, and the two performance items gated on a benchmark |
| [history.md](history.md) | recovering deleted C++ behaviour, or checking a review finding's outcome |

## What this branch is doing

`Wasm-vm-redesign`: replacing the C++ wasmi **C-API** integration with a Rust wasmi
wrapper, written as production code built on the PoC's ideas — not a cleanup pass over the
PoC.

`Rust_wasm_PoC` (and `Rust_wasm_PoC_benchmark`) are read-only reference branches. Their
crates are named differently — `host_functions`, `host_functions_macros`, `wasm_vm` (with
`imports.rs` where we have `register.rs`, plus `ffi.rs`), `stdlib`, `example_contract` —
read with `git show Rust_wasm_PoC:crates/<path>`.

**One PoC difference explains a lot of this crate.** The PoC's `host_abi!` inserted
`&self`, wrapped returns in `HostResult<_>`, and for a `Vec<u8>`/`[u8; N]` return
**appended `out: &mut [u8]` and changed the return to `HostResult<usize>`**. Here the
declaration *is* the signature — nothing is appended behind the reader's back. That is what
"no magic" means throughout these docs, and it is why byte outputs are written as explicit
out-params.

The C-API path is gone: `b7059deb9f` ("Remove wasmi dependency") deleted
`WasmVM.{h,cpp}`, `WasmiVM.h`, `HostFuncWrapper.cpp` and dropped the conan `wasmi`
package. Old semantics are recoverable with `git show b7059deb9f^:<path>` — do that rather
than guessing. See [history.md](history.md) for what is worth recovering.

## Where the code lives

- `crates/` — cargo workspace (edition 2024, resolver 3), built into the C++ build via
  corrosion (`crates/CMakeLists.txt`, which registers the cxxbridge targets).
  - `xrpl-host-functions/` — `no_std` ABI declaration: `host_functions! { … }` generates the
    `HostFunctions` trait and the `HostFunctionSpec` enum (import name + gas per function).
    Also `HostError`. **The single source of truth for the ABI** — see [abi.md](abi.md).
  - `xrpl-host-functions-macros/` — the proc macro. An implementation detail of the crate
    above, deliberately not re-exported: the ABI has one declaration site.
  - `xrpl-wasm-vm/` — the wasmi wrapper. `vm.rs` (engine, store, `run`), `preflight.rs`
    (`check` — compile, imports, entry point, with no host, store or gas), `abi.rs` (gas,
    transfer budget, guest-memory marshaling), `region.rs` (the `(ptr, len)` type),
    `register.rs` (one `func_wrap` per host function). See [engine.md](engine.md).
  - `xrpl-wasm-vm-ffi/` — the cxx bridge, both crossings. `RunStatus`/`RunResult`,
    `run_escrow`, `CxxHost`, the panic guard. See [bridge.md](bridge.md).
  - `xrpl-wasm-testkit/` — **test-only**: `compile_wat`, so the C++ tests write their modules
    as WebAssembly text. A crate of its own so `wat` cannot reach the shipped node; see
    [testing.md](testing.md).
- `include/xrpl/tx/wasm/`, `src/libxrpl/tx/wasm/` — C++ side: `HostFunc.h` (the ~60-method
  `HostFunctions` interface), `HostFuncImpl*.cpp` (its implementations, over
  `ApplyContext&`), `WasmCommon.h` (`HostFunctionError`, `Wmem`, `WasmTER`, `FieldLocator`).
  The bridge's C++ half is `HostContext.{h,cpp}` (the ABI-shaped view of `HostFunctions`)
  and `WasmVM.{h,cpp}` (`runEscrowWasm`, gas validation, the TER map).
- `src/tests/libxrpl/tx/wasm/` — the C++ tests, in the `xrpl_tests` gtest binary.
- `include/xrpl/tx/wasm/README.md` is **stale**: it uses the long name `get_ledger_sqn`
  where the code registers `ldgr_index`, and references `detail/WasmVM.cpp`,
  `detail/HostFuncWrapper.cpp`, `HostFuncWrapper.h` and `ParamsHelper.h`, none of which
  exist.

## Current state (2026-08-04)

**The whole workspace is green**: `cargo test --workspace`, `clippy --workspace
--all-targets`, `fmt`, and `cargo doc -p xrpl-wasm-vm --no-deps`. **154 tests** — 33 macro,
12 facade, 1 doctest, **96 in `xrpl-wasm-vm`** (11 unit; 85 integration — 13 `budgets`,
12 `host_calls`, 23 `memory_policy`, 16 `preflight`, 21 `vm_limits`), 10 in
`xrpl-wasm-vm-ffi`, 2 in `xrpl-wasm-testkit`. On the C++ side, **27 tests over the whole
loop** in six fixtures: `./xrpl_tests --gtest_filter='WasmVMTest.*:*Call.*'`.

**Both crossings are wired and a real contract runs through them**: C++ calls
`runEscrowWasm`, the engine services `ldgr_index` by calling back into
`xrpl::HostFunctions`, and the guest reads the answer out of its own memory. Five host
functions are registered (`ldgr_index`, `home_le_field`, `sha512_half`, `trace`,
`trace_num`) out of the ~65 the full ABI will carry.

**Seventeen of the eighteen review findings are closed**, C12 the only one left
([history.md](history.md)).

## Next

1. **`preflightEscrowWasm`.** The engine half is done — `check` in `preflight.rs`. What is
   left is the second bridge entry (`check_escrow`, a `CheckStatus`/`CheckResult` pair
   mirroring `RunStatus`/`RunResult`) and the C++ front, whose signature is
   `(Bytes, beast::Journal, std::string_view) -> NotTEC`: **no `HostFunctions&`**, since a
   check needs no host and a `PreflightContext` has no ledger to build one from.
   Two decisions are still open — what a *panic* at preflight returns
   (`telFAILED_PROCESSING` reads as the preflight analogue of `tecINTERNAL`'s "the fault is
   the node's"), and whether the apply-side map moves `Instantiate` off `tecINTERNAL` in the
   same change; see [bridge.md](bridge.md).
2. **A caller.** `EscrowFinish.cpp` still has no wasm reference, so `runEscrowWasm` is
   reached only from `src/tests/libxrpl/tx/wasm/`. Wiring it up is what makes
   `WasmHostFunctionsImpl` (over a real `ApplyContext`) the host in production rather than
   in principle.
3. **A gas parity oracle.** `Wasm_test.cpp` asserts exact gas numbers (e.g. 29'502) and is
   the best oracle we have, but it is commented out and its fixtures cannot run on this
   engine — see the `env` finding in [testing.md](testing.md).
4. **The `Bytes`-by-value copy in `HostFunctions`** — [bridge.md](bridge.md). A
   49-signature sweep, so it wants a caller to measure against first.

Also open: the two performance items and the ABI questions in
[open-questions.md](open-questions.md).

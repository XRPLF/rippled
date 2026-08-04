# WASM Module for Programmable Escrows

WebAssembly execution for programmable escrows. When an escrow is finished, its contract
runs to decide whether the release conditions are met. Specification:
[XLS-0102: WASM VM](https://xls.xrpl.org/xls/XLS-0102-wasm-vm.html).

The engine itself is Rust (`crates/xrpl-wasm-vm`, over wasmi), reached through a cxx
bridge.

## What is in this directory

- **`WasmVM.h`** — the entry points xrpld calls: `runEscrowWasm` (execute a contract,
  returning a result and its gas cost, or a `WasmTER`) and `preflightEscrowWasm` (screen a
  module with no host and no execution). Both own their TER maps.
- **`HostFunc.h`** — the `HostFunctions` interface: one virtual per host function, each
  defaulting to `Unimplemented`, returning `std::expected<T, HostFunctionError>`.
- **`HostFuncImpl.h`** — `WasmHostFunctionsImpl`, the implementation over an
  `ApplyContext&`. Bodies are split across `HostFuncImpl*.cpp` by category.
- **`HostContext.h`** — the bridge's C++ half: an ABI-shaped, `noexcept` view of
  `HostFunctions` that the engine calls back into. Nothing may unwind into Rust, so every
  method routes through one `guarded()`.
- **`WasmCommon.h`** — the shared vocabulary: `HostFunctionError` (the codes a contract
  sees), `Bytes`, `FieldLocator`, `WasmTER`, and `adjustWasmEndianess`, which is where the
  boundary's byte order is decided.

## Host functions

Grouped by what they reach: ledger information; transaction and ledger-object field access;
keylet construction; cryptography; float arithmetic; NFT queries; tracing.

The wire names and per-call gas costs are declared in `crates/xrpl-host-functions` —
one `host_functions!` block that generates the ABI trait and the spec table. That
declaration is the single source of truth; `HostFunc.h` is the C++ side of it.

## Entry point

A module must export `escrow_finish` (`escrowFunctionName`) taking no parameters and
returning `int32_t`: positive means the conditions are met, zero or negative rejects the
finish. Everything the contract needs it asks for through a host call.

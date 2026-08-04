[← Rust WASM VM docs](index.md)

# The cxx bridge

`crates/xrpl-wasm-vm-ffi/src/lib.rs` is the whole of the Rust half; `HostContext.{h,cpp}` and
`WasmVM.{h,cpp}` are the C++ half. Two decisions carry the design.

Three crossings, not two: `run_escrow` in, the host calls back out, and `check_escrow`
in. The third goes one way only — screening a module needs no host — so it takes no
`HostContext`, has no C++-exception half to contain, and is the one bridge function the
crate's own tests can call outright.

**The result is total, not `Result<T>`.** cxx's `Result` sugar throws a `rust::Error` into
C++; a status is the better interface for a condition the caller has to turn into a TER
anyway. `RunResult { status, result, gas_used, detail }` flattens the engine's
`Result<RunOutcome, RunFailure>` because a cxx enum carries no payload, and `RunStatus` is
1:1 with `RunError` plus `Ok` and `Panic`. Both directions of that map are compile-enforced:
`status_of`'s `match` is exhaustive over `RunError`, and C++'s `switch` over the generated
enum has no `default`, so an outcome added to the engine fails to build until it has been
given a status *and* a TER.

**Neither side may unwind into the other, and the two halves are not symmetric.**

- A **C++ exception** is stopped in C++. Every `HostContext` method is `noexcept` and every
  body goes through one `guarded()` that catches `std::exception` and `...`, journals, and
  returns -1. Nothing relies on cxx's own `trycatch`, which only catches `std::exception`
  and only for `Result` returns.
- A **Rust panic** is caught in Rust, by `guarded()` in the bridge. `[profile.release]`
  turns overflow checks on, so this is a live path; `#[cfg(panic = "abort")]
  compile_error!` keeps a profile change from silently defeating it.
- The asymmetry is what makes each half sufficient: because the C++ shims never unwind,
  every frame between a panic and `catch_unwind` is Rust.

Both halves are named `guarded`, and each is one function that every crossing goes through:
Rust's takes the panic arm as an argument (`ffi::RunResult::panicked`), C++'s takes the value
to answer with if the call throws. Anything C++ catches there is xrpld's own — a bad
allocation, or a `funcName` that is not valid UTF-8 and so cannot become a `rust::Str` —
never a wasm outcome, since those arrive as statuses.

`HostContext` holds a `HostFunctions&` and lowers its typed `std::expected` onto the wire.
The `&self`-vs-non-const worry was a non-issue: a `const` member function holding a
non-const reference can still call `cacheLedgerObj`/`updateData`. `cxx_name` on each method
keeps ABI names on the Rust side and rippled's camelBack on the C++ side. `guarded` names the
failing call through a defaulted `std::source_location` rather than a string per call site;
`__func__` would expand to `operator()` inside the lambda.

The five current functions needed **no change to `HostFunctions`** — the shim absorbs the
`uint32 → bytes` (via `adjustWasmEndianess`, which is where the wasm boundary's byte order
is decided for the whole system), `i32 → SField` and `Hash → 32 bytes` lowerings. Where that
will stop being true: `float_to_mant_exp` (two output regions), the `FieldLocator` entries,
and `updateData`.

## The TER map

`runEscrowWasm` owns it. `tecINTERNAL` reports no cost by convention: it says the fault is the
node's, and charging a transaction for a node's defect would write that defect into the ledger.

| `RunStatus` | TER | cost |
|---|---|---|
| `Ok` | — | `gas_used` |
| `OutOfGas` | `tecOUT_OF_GAS` | `gas_used` |
| `Trap`, `NoMemory`, `Instantiate` | `tecFAILED_PROCESSING` | `gas_used` |
| `Compile`, `EntryPoint` | `tecINTERNAL` | none |
| `Internal`, `Panic` | `tecINTERNAL` | none |

`Compile` and `EntryPoint` are `tecINTERNAL` because preflight decides both from the same
bytes and the same engine, so agreement is not a matter of degree: reaching apply means the
screening did not happen. `Instantiate` is **not** in that row, and the reason is the point of
the whole arrangement — see below. `NoMemory` had no old TER to match (it used to reach the
guest as code -14); `tecFAILED_PROCESSING` treats it as the contract fault it is.

One thing to settle before a long-lived escrow exists: `Compile` is only a node fault while
the engine's configuration never changes. A contract created under one feature set and
finished under another could legitimately fail to compile at apply, so either the config is
amendment-gated or `Compile` joins the charged row.

`gas <= 0` is refused as `temBAD_AMOUNT` before the engine is called, restoring what
`WasmiEngine::run` did — see [open-questions.md](open-questions.md).

## The preflight map

`preflightEscrowWasm` owns it, and it is deliberately flat: every fault in the module is
one answer, because a caller's only decision is whether the transaction may proceed.

| `CheckStatus` | `NotTEC` |
|---|---|
| `Ok` | `tesSUCCESS` |
| `Compile`, `Import`, `EntryPoint`, `Memory` | `temBAD_WASM` |
| `Panic` | `telFAILED_PROCESSING` |

The statuses stay distinct anyway: the *detail* is what a contract author needs, and one
status per stage keeps the map's arms reviewable and lets it grow without inventing
distinctions later.

`Panic` is not `temBAD_WASM`. A defect in the engine teaches nothing about the module, and
`tem` would record our bug as the transaction's malformation; `tel` is the preflight
analogue of `tecINTERNAL`'s "the fault is the node's" — local, not forwarded, no fee. Two
things follow that are worth stating: divergence between nodes is not what the code choice
fixes (a panic in deterministic code is not node-local, and if it were, no TER would
reconcile the two), and this arm has no test on the C++ side, because there is no reliable
way to make the engine panic from a fixture.

**The signature the C++ front does not have is the point**: `(Bytes, beast::Journal,
std::string_view) -> NotTEC`, with no `HostFunctions&`. The deleted `preflightEscrowWasm`
took one and could therefore never have been called from a real `preflight()` —
`PreflightContext` has no view to build a host over.

## Why `Instantiate` is the contract's fault

The map must not depend on preflight being exhaustive, because it cannot be. `check` closes
compile, imports, the entry point and an exported memory over the page cap — but a memory a
module *keeps to itself* is absent from its exports, so such a module passes screening and
then fails to instantiate ([engine.md](engine.md)). That is a deterministic property of the
code, identical on every node, and nothing this node did; charging it as
`tecFAILED_PROCESSING` says so, where `tecINTERNAL` would blame the node and forgive the gas.

The other half is in the engine rather than the map: `vm::instantiation_failure` reports a
failure carrying a trap code as `RunError::Trap`, because a start section that traps is guest
code trapping, and a trap is the guest's fault wherever it happens. What is left for
`Instantiate` is a module the linker or the store would not accept at all —
`vm_limits::instantiation_failure_is_a_module_the_engine_will_not_accept` pins both shapes.

So `tecINTERNAL` now means what it says: `Internal` and `Panic`, the node's own defects, plus
the two stages preflight decides exactly.

## The one copy left on the byte path, and why it needs `HostFunctions` to change

The engine's side of the byte path is copy-free by construction — `write_into` hands the host
guest memory directly, `write_buffered` copies once after every rule has passed
([engine.md](engine.md)). **The C++ side then puts a copy back**, because `HostFunctions`
returns its answer *by value*:

```cpp
std::expected<Bytes, HostFunctionError> getCurrentLedgerObjField(SField const&) const;
```

`Bytes` is a `std::vector<std::uint8_t>`, so serving one field allocates, fills, gets copied
into `out` by `HostContext::answer`, and is freed — a heap round trip per host call, on a
consensus path, for a value the caller already has a buffer for. **49 of the 66 virtuals
return `Bytes` this way**; only the two `Hash` ones are inline.

The fix is an out-param form on `HostFunctions` itself — `(…, std::span<std::uint8_t> out)
-> std::expected<std::size_t, HostFunctionError>`, returning the value's true length on the
same "write only if it all fits" contract the ABI already uses end to end. That makes the
convention identical on both sides of the bridge and leaves `HostContext` with no copy to
make. Note the two shapes are not equivalent for every function: one that cannot know its
length without building the value still allocates internally, so the win is real for field
and keylet reads and smaller for the float ops.

Deliberately **not** done with the bridge: it touches 49 signatures plus
`WasmHostFunctionsImpl`, `HostFuncImpl*.cpp` and the test hosts, which is a mechanical sweep
that would bury the bridge in review. Sequence it after a caller exists, so the sweep can be
measured against something that runs.

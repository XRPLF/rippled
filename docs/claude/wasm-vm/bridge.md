[← Rust WASM VM docs](index.md)

# The cxx bridge

`crates/xrpl-wasm-vm-ffi/src/lib.rs` is the whole of the Rust half; `HostContext.{h,cpp}` and
`WasmVM.{h,cpp}` are the C++ half. Two decisions carry the design.

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
| `Trap`, `NoMemory` | `tecFAILED_PROCESSING` | `gas_used` |
| `Compile`, `Instantiate`, `EntryPoint` | `tecINTERNAL` | none |
| `Internal`, `Panic` | `tecINTERNAL` | none |

The `Compile`/`Instantiate`/`EntryPoint` row is `tecINTERNAL` because preflight is meant to
have refused such a module with `temBAD_WASM` long before apply — which is why preflight is
item 1 in [the roadmap](index.md#next). `NoMemory` had no old TER to match (it used to reach
the guest as code -14); `tecFAILED_PROCESSING` treats it as the contract fault it is.

`gas <= 0` is refused as `temBAD_AMOUNT` before the engine is called, restoring what
`WasmiEngine::run` did — see [open-questions.md](open-questions.md).

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

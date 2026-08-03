# rippled fork — Rust WASM VM work

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
"no magic" means throughout this document, and it is why byte outputs are written as
explicit out-params.

The C-API path is gone: `b7059deb9f` ("Remove wasmi dependency") deleted
`WasmVM.{h,cpp}`, `WasmiVM.h`, `HostFuncWrapper.cpp` and dropped the conan `wasmi`
package. Old semantics are recoverable with `git show b7059deb9f^:<path>` — do that rather
than guessing.

## Where the code lives

- `crates/` — cargo workspace (edition 2024, resolver 3), built into the C++ build via
  corrosion (`crates/CMakeLists.txt`, which already registers the cxxbridge target).
  - `xrpl-host-functions/` — `no_std` ABI declaration: `host_functions! { … }` generates the
    `HostFunctions` trait and the `HostFunctionSpec` enum (import name + gas per function).
    Also `HostError`. **The single source of truth for the ABI.**
  - `xrpl-host-functions-macros/` — the proc macro. An implementation detail of the crate
    above, deliberately not re-exported: the ABI has one declaration site.
  - `xrpl-wasm-vm/` — the wasmi wrapper. `vm.rs` (engine, store, `run`), `abi.rs` (gas,
    transfer budget, guest-memory marshaling), `region.rs` (the `(ptr, len)` type),
    `register.rs` (one `func_wrap` per host function).
  - `xrpl-wasm-vm-ffi/` — the cxx bridge, both crossings. `RunStatus`/`RunResult`,
    `run_escrow`, `CxxHost`, the panic guard.
  - `xrpl-wasm-testkit/` — **test-only**: `compile_wat`, so the C++ tests write their modules
    as WebAssembly text. A crate of its own so `wat` cannot reach the shipped node; see
    "How the C++ tests are built" below.
- `include/xrpl/tx/wasm/`, `src/libxrpl/tx/wasm/` — C++ side: `HostFunc.h` (the ~60-method
  `HostFunctions` interface), `HostFuncImpl*.cpp` (its implementations, over
  `ApplyContext&`), `WasmCommon.h` (`HostFunctionError`, `Wmem`, `WasmTER`, `FieldLocator`).
  The bridge's C++ half is `HostContext.{h,cpp}` (the ABI-shaped view of `HostFunctions`)
  and `WasmVM.{h,cpp}` (`runEscrowWasm`, gas validation, the TER map).
- `include/xrpl/tx/wasm/README.md` is **stale**: it uses the long name `get_ledger_sqn`
  where the code registers `ldgr_index`, and references `detail/WasmVM.cpp`,
  `detail/HostFuncWrapper.cpp`, `HostFuncWrapper.h` and `ParamsHelper.h`, none of which
  exist.

## Current state (2026-08-03)

**The whole workspace is green**: `cargo test --workspace`, `clippy --workspace
--all-targets`, `fmt`, and `cargo doc -p xrpl-wasm-vm --no-deps`. **137 tests** — 33 macro,
12 facade, 1 doctest, **79 in `xrpl-wasm-vm`** (10 unit; 69 integration — 13 `budgets`,
12 `host_calls`, 23 `memory_policy`, 21 `vm_limits`), 10 in `xrpl-wasm-vm-ffi`, 2 in
`xrpl-wasm-testkit`. On the C++ side, **27 tests over the whole loop** in six fixtures:
`./xrpl_tests --gtest_filter='WasmVMTest.*:*Call.*'`.

**Both crossings are wired and a real contract runs through them**: C++ calls
`runEscrowWasm`, the engine services `ldgr_index` by calling back into
`xrpl::HostFunctions`, and the guest reads the answer out of its own memory. Five host
functions are registered (`ldgr_index`, `home_le_field`, `sha512_half`, `trace`,
`trace_num`) out of the ~65 the full ABI will carry.

**Seventeen of the eighteen review findings are closed**, C12 the only one left (see the
appendix). What is left overall: preflight and a caller (below), two performance items gated
on a benchmark (below), and the open ABI questions.

## The bridge, as built

`crates/xrpl-wasm-vm-ffi/src/lib.rs` is the whole of it. Two decisions carry the design.

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
keeps ABI names on the Rust side and rippled's camelBack on the C++ side.

The five current functions needed **no change to `HostFunctions`** — the shim absorbs the
`uint32 → bytes` (via `adjustWasmEndianess`, which is where the wasm boundary's byte order
is decided for the whole system), `i32 → SField` and `Hash → 32 bytes` lowerings. Where that
will stop being true: `float_to_mant_exp` (two output regions), the `FieldLocator` entries,
and `updateData`.

### The one copy left on the byte path, and why it needs `HostFunctions` to change

The engine's side of the byte path is copy-free by construction — `write_into` hands the host
guest memory directly, `write_buffered` copies once after every rule has passed. **The C++
side then puts a copy back**, because `HostFunctions` returns its answer *by value*:

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

## How the C++ tests are built

`src/tests/libxrpl/tx/wasm/`, in the `xrpl_tests` gtest binary. Four decisions, each of which
had an obvious cheaper alternative that was worse.

**Modules are WebAssembly text, assembled at run time.** Checked-in hex blobs do not scale
past one module — every host function needs its own, with its own import signature — and they
are unreviewable. So `compile_wat` comes over cxx from **`crates/xrpl-wasm-testkit`**, a crate
of its own that nothing in `libxrpl` or `xrpld` links.

That separation is the whole point and is worth not undoing. The engine pins
`wasmi = { default-features = false }` because wasmi's `wat` feature makes `Module::new`
accept text as readily as binary, which would make a transaction's validity a build flag
(finding A5). Putting `compile_wat` on the production bridge would link an assembler into the
shipped node even though nothing called it; a cargo feature would make the test and production
binaries differ. A separate crate makes "no assembler in the node" a property of the link
graph. `WasmVMTest.ATextFormatModuleIsNotAModule` then feeds the engine the very text the rest
of the suite assembles, so the guest-side half of A5 is pinned too.

*Two Rust staticlibs in one binary is fine* — `xrpld` already links `rs_hello_world` and
`xrpl_wasm_vm_ffi`. The `_rust_eh_personality` collision earlier in this document came from
conan's *separately compiled* `std`, not from two crates in this workspace.

**The host is a `StrictMock`.** `MockHostFunctions` mocks only the methods the ABI declares;
the ~60 others keep `HostFunctions`' `Unimplemented` default, so a contract reaching past the
ABI fails the way production would. What this buys over a hand-written fake is assertions on
*what the host was asked* — that a guest `i32` became the right `SField`, that two borrowed
regions and a flag all arrived, that an `i64` survived as `INT64_MIN`.

Strict rather than nice, because these modules import exactly what they mean to exercise: a
host call no test asked for means the engine reached for something on its own, which is worth
a failure rather than a warning. The cost is one line in the fixture —
`EXPECT_CALL(host_, checkSelf()).WillRepeatedly(Return(true))`, since `runEscrowWasm` asks
every run whether the host is clean. Verified by mutation: giving `escrow_finish` an
unstubbed host call fails the test under Strict and passes silently under `NiceMock`.

*One trap worth knowing even so*: gmock's default action for `std::expected<T, E>` is a
**successful** `T{}`, so a method with an `EXPECT_CALL` but no action would answer `0` and a
test could pass on an answer nobody chose. The mock's constructor therefore `ON_CALL`s every
method to the base class's `std::unexpected(Unimplemented)`.

**Two levels of fixture.** `WasmTest` holds the mock, a capturing journal sink and `run(wat,
gas, entryPoint)`. `HostCallTest` adds a `wat()` the derived fixture supplies and `hostAnswer()`,
so a per-function test says only what the host was asked and what came back. Then one fixture
per host function — `LedgerSqnCall`, `CurrentLedgerObjFieldCall`, `Sha512HalfCall`, `TraceCall`,
`TraceNumCall` — because the module *is* that function's shared setup. `WasmVMTest` keeps what
belongs to the engine rather than to any function.

**The journal is captured, not sent to a null sink.** `AThrowingHostFunctionIsCaughtAndBecomes-
Internal` asserts the exception text *and* that the log names `getLedgerSqn`; without that, an
exception silently swallowed with no log would pass, and `HostContext::guarded`'s
`source_location` would be untested.

Two properties are pinned from the guest's side rather than asserted about internals:
`ABufferTooSmallIsRefusedWholeRatherThanTruncated` has the contract report whether *anything*
reached its memory, which is "a refused value reaches it in no part" as a contract can observe
it; and `EverySoftHostErrorCodeCrossesToTheContractUnchanged` walks all 18 soft
`HostFunctionError` codes, because the C++ and Rust error enums are two hand-maintained lists
of the same numbers that **have already drifted once** (-11 is `OutOfTransferLimit` in C++ and
`Decoding` in the Rust ABI — open question 3).

*Mutation-checked*: making a too-large value write a truncated prefix, and pointing the
sha512 input matcher at bytes the guest does not send, each fail exactly one test and nothing
else.

## Next: preflight, then a caller

1. **`preflightEscrowWasm`.** The gap the TER map is currently papering over: a module that
   will not compile, instantiate, or expose the entry point maps to `tecINTERNAL` with no
   cost, which is only defensible because preflight is *meant* to have refused it with
   `temBAD_WASM` first. Nothing does that yet. It needs a second bridge entry that compiles
   and looks up the export without executing.
2. **A caller.** `EscrowFinish.cpp` still has no wasm reference, so `runEscrowWasm` is
   reached only from `src/tests/libxrpl/tx/WasmVM.cpp`. Wiring it up is what makes
   `WasmHostFunctionsImpl` (over a real `ApplyContext`) the host in production rather than
   in principle.
3. **A gas parity oracle.** `Wasm_test.cpp` asserts exact gas numbers (e.g. 29'502) and is
   the best oracle we have, but it is commented out and its fixtures cannot run on this
   engine (see below).
4. **The `Bytes`-by-value copy in `HostFunctions`** — see "The one copy left on the byte
   path" below. A 49-signature sweep, so it wants a caller to measure against first.

**Two findings from wiring the bridge, both worth knowing before the parity work:**

- **The C fixtures under `src/test/app/wasm_fixtures/` import from module `env`**, not
  `host_lib` (`kLedgerSqnWasmHex` decodes to `... 03 656e76 0a 6c6467725f696e646578 ...`),
  and their `target_features` include `sign-ext`, `multivalue` and `reference-types`, which
  this engine disables. The deleted C++ engine ignored the import module name entirely —
  `wasm_importtype_module` is commented out at its `WasmiVM.cpp:428`. So those fixtures are
  not usable as a parity oracle without recompiling them with
  `-Wl,--import-module=host_lib` and the engine's feature set. The gtest carries its own
  WAT-derived fixtures for that reason.
- **`OutOfGas` does not always report the whole limit.** A contract that loops until the
  meter empties reports the full budget, but a budget too small to reach the first charge
  reports `0`: wasmi leaves the remaining fuel in place on its own `OutOfFuel` trap, and
  only `abi::charge` forces it to zero. The deleted C++ path did this deliberately
  (`iw.setGas(0)` on out-of-gas, so the cost was always the full limit). Closing the gap is
  a one-line change in `vm::run`, but it is consensus-visible metadata, so it is called out
  rather than slipped in.

## Performance, gated on a benchmark

**Two optimisations are deferred pending measurement, not rejected.** Neither is worth
guessing at, and one benchmarking pass with the google-benchmark harness on a
host-call-heavy module settles both.

1. **Lazy output buffer.** `VmState::out_buffer` is an inline `[u8; MAX_FIELD_BYTES]`,
   zero-filled once per run whether or not the contract makes a call that uses it. The lazy
   form is `Option<[u8; N]>` with `get_or_insert_with` (not `OnceCell` — that is for init
   behind a shared borrow; `write_buffered` holds `&mut VmState`). The case against it today
   is a magnitude argument a measurement could overturn: it defers one ~1 KiB fill per run,
   invisible beside the `Module::new` that starts every run, and pays for it with a
   discriminant test on **every host call** — the direction C11 moved cost away from. Also
   note `Option<[u8; N]>` does not shrink `VmState` (no niche in a byte array), and
   `Option<Box<[u8; N]>>` does but then charges a malloc to the 38 functions that use this
   path in order to save the ones that do not.
2. **C12: cached `Linker`, cached module.** Two independent halves.
   - *Module compile cache* is the bigger win — a whole wasm translation per run versus
     building a five-entry linker — and it is **not** blocked by the lifetime problem, since
     a `Module` is engine-scoped. But it carries a question that is not the engine's to
     answer: **who owns a compiled contract's lifetime?** An unbounded static cache inside a
     library on a consensus path brings an eviction policy nobody asked for; the alternative
     is handing `run` a pre-compiled module, which changes the signature the bridge is about
     to consume. Either way the answer comes from the caller side.
   - *Per-run `Linker`* is blocked: `VmState<'h>`'s lifetime forces `Linker<VmState<'h>>` to
     be per-run, so hoisting it means making the store data `'static` — not holding
     `&'h dyn HostFunctions`. This was expected to fall out of the bridge; **it did not.**
     `CxxHost<'a>` borrows the C++ `HostContext` for one run and coerces to
     `&'h dyn HostFunctions` unchanged, so hoisting the linker is still its own piece of
     work with no other reason to do it.

So: **measure first**, and treat the linker and the lazy buffer as whatever the numbers say.
The module cache's open question is unchanged by the bridge — `run(wasm, gas, host, name)`
is now a signature the C++ side consumes, so handing it a pre-compiled module is a change to
a live interface rather than a hypothetical one, and **who owns a compiled contract's
lifetime** is still the caller's question to answer.

## Open decisions

**`gas = 0` — resolved: refused in C++ as `temBAD_AMOUNT`.** The old code already decided
this and the doc had it garbled: `WasmiEngine::run` rejected `gas <= 0` for *every* value
including `-1`, and `-1 = unlimited` applied only to preflight's `check`. `runEscrowWasm`
restores exactly that, which is why the engine's own budget stays a `u64` with no invalid
value to represent.

**ABI / guest-SDK interop.** Found auditing the guest SDK (`~/Documents/rust/xrpl-wasm-stdlib`,
checkout `435a091f`) against this fork. All are decisions rather than code.

1. ~~Import module name~~ — **resolved: `host_lib`**, matching the SDK and this fork's
   fixtures. `the_import_module_name_must_match` rejects `host`, `env` and the empty name.
2. **Import name lineage.** The fixtures pin the SDK at `branch = renames` and use **short**
   wire names (`parent_ldgr_hash`, `cache_le`, `tx_inner_arr_len`, `accountroot_id`,
   `trustline_id`), matching `ldgr_index` / `home_le_field` / `sha512_half`. The standalone
   SDK checkout is the **long**-name lineage (`get_parent_ledger_hash`, `cache_ledger_obj`,
   `compute_sha512_half`). Which is authoritative is undecided.
3. **New error codes are UB in the guest.** The SDK decodes with a bare transmute and no
   range check (`xrpl-common-stdlib/src/host/mod.rs:325`), valid only for `-1..=-20`. Making
   host-fatal errors trap (A1) removed `OutOfGas` from the guest's view, and the
   soft/fatal question is settled — **`OutOfTransferLimit` stays soft**. The *encoding*
   question is not: `OutOfTransferLimit = -23` still reaches a transmuting guest, and
   `NoRuntime = -21` would if anything returned it. Closing it needs either a range check in
   the SDK or a remap into `-1..=-20`.
4. **`-1` collides semantically** — host `Unimplemented` vs Rust `Internal`. **Now a
   decision rather than an accident**: the bridge treats them as one condition, "the host
   could not serve this call, and the contract has no business interpreting why". Both are
   host-fatal, so both stop the run and report `tecINTERNAL`, which is also what a C++
   exception caught in `HostContext::guarded` becomes. The guest-side half of the collision
   (its own `InternalError`) is untouched.
5. **`float_to_mant_exp` byte count.** The host returns **12** (8 mantissa + 4 exponent);
   the guest doc says 8, and the guest's `match_result_code_with_expected_bytes` **panics**
   on a non-negative mismatch. Note this function writes *two* output regions, a shape no
   current helper serves.
6. **Return conventions are not uniform** — six of them: bytes-written; value-in-return
   (`*_arr_len`, `nft_flags`); boolean 0/1 (`amendment_enabled`, `check_sig`); 1-based handle
   (`cache_le`, always ≥ 1); status-0 (`trace*`, `set_data`); tri-state (`float_cmp` — `0`
   equal, `1` first >, `2` second >).
7. **The SDK's drift checker is silently broken.** `tools/compareHostFunctions.js`
   regex-parses `WasmVM.cpp` and `HostFuncWrapper.h`, both deleted. A generated C header
   would give it a stable target again.

## The ABI: one declaration, three outputs

`crates/xrpl-host-functions/` is the one declaration. C compatibility adds a **third
output** beside the trait and the spec enum — a *generated, checked-in* C header with a CI
diff — not a second input. "Explicit vs hidden" is the wrong axis; "derivable and emitted"
is the right one, because C authors read a header, not a macro.

### The lowering table

The DSL already implied this; it was never written down, and that was the whole gap.

```
params, in declared order:
  &self              -> nothing                   (receiver, not part of the ABI)
  i32, bool          -> i32                       (bool: nonzero = true)
  i64                -> i64
  &[u8], &str        -> i32 ptr, i32 len          const uint8_t*, int32_t
  &mut [u8]          -> i32 ptr, i32 len          uint8_t*, int32_t   (an output region)

returns, always HostResult<T>; Err(e) -> negative code, or a trap when host-fatal:
  HostResult<usize>       -> i32 = bytes written into the output region
  HostResult<i32>, <bool> -> i32 = the value
  HostResult<()>          -> i32 = 0
```

Total, unambiguous and **positional**: every wasm parameter is a declared parameter in
order, so the C prototype is a direct reading of the declaration. **The macro must reject
any type not in this table** — that is C++'s `WasmImpArgs` `static_assert` restored, and it
is what keeps the C API always surfaceable.

All five current declarations lower to exactly the deleted C++ `_proto` aliases (verified
2026-07-29; `&self` dropped below since it contributes no C parameter):

| Declaration | Derived C |
|---|---|
| `get_ledger_sqn(out: &mut [u8]) -> HostResult<usize>` | `int32_t(uint8_t*, int32_t)` |
| `get_current_ledger_obj_field(field: i32, out: &mut [u8]) -> HostResult<usize>` | `int32_t(int32_t, uint8_t*, int32_t)` |
| `sha512_half(data: &[u8], out: &mut [u8]) -> HostResult<usize>` | `int32_t(const uint8_t*, int32_t, uint8_t*, int32_t)` |
| `trace(msg: &str, data: &[u8], as_hex: bool) -> HostResult<()>` | `int32_t(const uint8_t*, int32_t, const uint8_t*, int32_t, int32_t)` |
| `trace_num(msg: &str, number: i64) -> HostResult<()>` | `int32_t(const uint8_t*, int32_t, int64_t)` |

**Discipline the table requires**: a byte output is an explicit `out: &mut [u8]` plus
`HostResult<usize>`, never a returned value. `get_ledger_sqn` writes 4 LE bytes and returns
4 — it does not return the sequence number; by the same rule `float_to_int` takes an out
region rather than returning `i64`. A scalar `HostResult<T>` means value-in-the-return.

**The out-region contract, which the engine relies on: write only if the whole value fits,
and return its true length either way.** So a host never needs to know the guest's buffer
size — the engine turns `n > cap` into `BufferTooSmall`.

### Closing the drift gap to `register.rs`

**wasmi 1.1 cannot introspect a registered host function's signature.** `Linker::get`
returns `None` for `func_wrap`'d functions — they land in `Definition::HostFunc`
(`wasmi-1.1.0/src/linker.rs:147`), and `Definition::ty()` exists at `:171` but `Definition`
and `get_definition` are private. So "assert `Func::ty()` equals the spec" is unavailable.

| Approach | `register.rs` | Guarantee |
|---|---|---|
| Generate closures wholesale | disappears | by construction |
| **Generate `link_*` shims, hand-write bodies** | **stays, readable** | **compile-time** |
| Hand-write everything + probe-module test | stays | test-time |

**Preferred: the middle row** — the macro emits the *type* without the *body*:

```rust
pub type Sha512HalfFn =
    fn(Caller<'_, VmState<'_>>, i32, i32, i32, i32) -> Result<i32, wasmi::Error>;

pub fn link_sha512_half(l: &mut Linker<VmState<'_>>, f: Sha512HalfFn)
    -> Result<(), LinkerError> { l.func_wrap(MODULE, HostFunctionSpec::Sha512Half.wasm_name(), f) }
```

Wrong arity, scalar type or return then becomes a compile error, and the same lowering
table emits both the alias and the C prototype so they cannot drift. *Constraint*: `fn`
pointers accept only non-capturing closures; every arm today is non-capturing, and one that
needs to capture can take `impl Fn(..) + Send + Sync + 'static` instead. Cheap extra worth
having: a **probe-module test** that synthesises a WAT module importing every function with
its declared type and instantiates it — the only check that also catches module-name and
missing-import mistakes, from the guest's side.

Deferred together: the shims, the generated header, the probe test.

### The ABI crate is a library both sides link

It is consumed as an ordinary dependency — by `xrpl-wasm-vm` today, the guest stdlib next.
Neither invokes `host_functions!`; consumers get the generated code, not the generator.
That makes four properties load-bearing:

| Property | Why | Status |
|---|---|---|
| `#![no_std]`, no allocator | the guest stdlib is strictly `no_std` | ✓ `Vec` left when byte outputs became `out: &mut [u8]` |
| zero runtime dependencies | anything else must also build for the guest | ✓ `cargo tree` is the proc-macro crate alone |
| builds for `wasm32-unknown-unknown` | it links into the guest | ✓ verified |
| implementable by **both** sides | one declaration, two implementors | ✓ the out-param shape is what buys this |

A host impl writes into `out` and returns the length; a guest impl forwards to the import
and decodes the `i32` through `HostError::from_code`, which range-checks (unlike the SDK's
transmute — question 3). One trait serves both *because the declaration is now the wire
shape*.

**Known gap.** The `#[link(wasm_import_module = "…")] unsafe extern "C" { … }` block is not
generated; the PoC's macro did generate it plus a `GuestHost` impl. If the stdlib
hand-writes it, that is precisely the drift a single source of truth exists to prevent. One
wrinkle to decide first: a generated guest impl needs `HostError::from_code`, a name no
declaration mentions, so it would be the first vocabulary dependency inside an otherwise
closed expansion.

**Convention: the expansion is closed.** Every name in it is generated or written in the
declarations; `names_no_crate_of_its_own` enforces it. The macro owns `HostFunctions`,
`HostFunctionSpec`, `ALL`, `wasm_name()`, `gas()` and the private `HostFnSpec` row type.
The facade hand-writes only the vocabulary declarations are written in — `HostError`,
`HostResult`, `HASH_LEN` — which resolve at the call site like `&[u8]` does. `HostFnSpec`
and `spec()` are private; read the table through `wasm_name()` / `gas()`.

## How the engine works

Contracts worth knowing before changing anything, and the reasons that are not visible in
the code.

**Two channels for a result.** A value or a guest-actionable error is the `i32` the wasm
function returns (`>= 0` value, `< 0` a `HostError` code). A **host-fatal** error —
`OutOfGas`, `Internal`, `NoMemExported` — traps instead, carrying `FatalHostError(HostError)`
as the payload so `run` can name the condition with `downcast_ref` rather than
string-comparing a message. XLS-0102 requires immediate halting on gas exhaustion, and a
guest handed `OutOfGas` as a code would run to the end of its current basic block — a
stopping point wasmi's `ConsumeFuel` placement decides rather than the protocol.
`is_fatal` spells the set variant by variant so a new `HostError`'s channel is chosen, not
inherited from its number. **`OutOfTransferLimit` is soft**: the one budget a contract can
be expected to handle.

`is_fatal` and `vm::host_fatal` are two lists that must agree. One direction is
compiler-enforced (`host_fatal` is exhaustive, so a new variant fails to build);
`every_fatal_error_has_an_outcome_of_its_own` covers the other. `HostError::ALL` and
`HostFunctionSpec::ALL` exist because an exhaustive `match` forces you to *write an arm* but
cannot *enumerate* variants, and every const-assertion scheme over `ALL` is beaten by "add
the variant, give its arm a value, leave `ALL` alone". The airtight mechanism is a single
declaration site: a `host_errors!` macro emits the enum, `ALL` and `from_code` from one list.

**Every failure carries its cost.** `run` returns `Result<RunOutcome, RunFailure>` where
`RunFailure` is `{ error: RunError, fuel_used }`, so gas is on both paths by construction. A
cost that cannot be read becomes `RunError::Internal` rather than a number — `0` would
forgive a run its whole cost and `gas` would charge an untouched one for everything.
`guest_halted` asks "did the guest halt?" at *every* stage from instantiation on, so a start
section that burns the limit is `OutOfGas`, not `Instantiate`: the stage a run stopped at is
not what the caller maps.

**Two ways a byte answer reaches the guest**, both taking a `Region`:

- `write_into` — the host writes straight into the guest's output region. Used by calls with
  no byte input. Zero copy.
- `write_buffered` — the host fills `VmState::out_buffer` and the engine copies it to the
  guest once every rule has passed. Used by calls that also *read* guest memory, because a
  `&mut` view of that memory admits no simultaneous `&` view. `Memory::data_and_store_mut`
  (`memory/mod.rs:165`) returns `(&mut [u8], &mut T)` — guest bytes and store data in one
  split borrow — which is what lets any number of inputs stay borrowed while the answer is
  written. No copying inputs out, no `unsafe`.

`write_buffered` never tells the host the guest's capacity: it offers the whole buffer and
takes the value's true length, so nothing reaches guest memory until the length, bounds, fit
and budget have all passed — **a refused value reaches it in no part**, which `write_into`
can only bound rather than prevent. The output is judged *after* the inputs, so a call with
both malformed reports the input's verdict; `NoMemExported` precedes both, because there is
no memory to validate a region against. `MAX_FIELD_BYTES` is checked beside the clamp on
purpose: the clamp bounds the **bytes**, the check bounds the **status**, since a host
reports a true length that can exceed the region it was offered.

Why this shape: of the ~65 ABI entries, **38 have a byte input *and* a byte output**, 9 are
output-only, 18 are input-only or scalar. Of the 38, **22 have more than one region** — every
two-argument keylet, `nft_uri`, all four float arithmetic ops — which a one-input helper
cannot express at all. The 9 output-only ones are exactly the row a buffer makes worse, and
they keep `write_into`.

**`Region`** (`region.rs`) is the wire's `(ptr, len)` as one type. It cannot catch a swapped
pair — `Region::new(len, ptr)` compiles, and no type can do better where the values arrive
as indistinguishable `i32`s in positional order; that is a job for a reader or for the shim
generator. What it enforces is that the pair cannot be *used* unchecked: `range()` is the
only way to indices, and it is where `InvalidParams` (the conversion to `usize` is the
negativity check) and the end-overflow guard live. It sits in its own module because Rust
privacy is module-level — inside `abi.rs` the helpers could still read `.ptr` and skip the
check. Verified: an attempted bypass is `error[E0616]: field ptr of struct Region is
private`. Construction is **infallible on purpose**; validating in `new` would hoist the
output region's verdict above the host call and break the input-first order that
`a_read_write_checks_its_input_before_its_output` pins.

`Region::read` is then ordinary safe slicing, because a guest pointer is an *index*: wasm
linear memory is a byte array in the store, `mem.data(caller)` is a `&[u8]` over it, and
`data.get(start..end)` does the bounds check and returns a slice **aliasing** guest memory.
`get` rather than `[..]` because indexing panics, and a panic on a consensus path is a node
crash. Elision ties the returned slice's lifetime to `data`, so a host cannot stash an input
past the call.

**Two budgets.** Gas is charged per host call from the spec table, before the body runs
(`charged` is the one path, so it cannot be forgotten); exhaustion spends what is left, which
is what makes the reported cost the whole limit. The transfer budget counts only bytes
*copied* across — `charge_transfer` has one call site per write path. A borrowed read copies
nothing and is not charged; what bounds how many reads a run makes is gas. Typed reads that
materialise a host object will charge; this ABI has none yet, and the alignment-copy charge
for unaligned field reads has nothing to attach to until a `FieldLocator` function exists.

**Guest memory is resolved once per run, by kind.** `run` takes
`instance.exports(&store).find_map(Export::into_memory)` after `instantiate_and_start` and
keeps the handle in `VmState::memory`, so no call pays for an export lookup. By *kind*, never
by name: nothing in the wasm spec attaches meaning to `"memory"`. Caching is sound because a
`Memory` is an arena index, not a pointer — it survives `memory.grow`. Two consequences:
the field assumes **one module, one instance, one store per `run`** (module linking would
have to resolve per instance, or serve a call against the wrong memory), and **a start
section cannot make a host call needing memory** — `Module::instantiate` is `pub(crate)`, so
instantiation cannot be split from the start section. `a_start_section_cannot_make_a_host_call`
pins it.

**Engine config is consensus-fixed**: fuel on, floats off, every post-MVP proposal off, one
process-wide `Engine` behind a `LazyLock` (an `Engine` is internally `Arc`ed and `Send +
Sync`). Notably `wasmi = { default-features = false, features = ["std"] }` — wasmi's `wat`
feature is **on by default** and makes `Module::new` accept text as readily as binary, which
would put a text assembler in the consensus path and make a transaction's validity a build
flag. `the_vm_refuses_a_text_format_module` catches that coming back.

**A start section cannot be rejected outright.** wasmi 1.1 exposes no
`InstancePre`/`ensure_no_start` and `ModuleHeader::start` is private, so only a byte-level
section scan would do it. It is metered and memory-capped regardless, since `run` installs
the fuel and the limiter before `instantiate_and_start`.

**A dead end, recorded so nobody retries it.** Host-function parameters cannot be newtypes.
`wasmi::WasmTy` looks implementable — public, no sealing supertrait — but its bound names
`UntypedVal`, which wasmi re-exports only through a **private** `mod core`
(`wasmi-1.1.0/src/lib.rs:109-137`). Probed: `error[E0603]: module core is private`. The
escape hatch is a direct `wasmi_core` dependency pinned in lockstep with wasmi's own, plus a
`#[doc(hidden)]` method — not worth it on a consensus path. So the wire stays `i32` and pairs
are formed on the first line of each arm.

## Build / test loop

- Fast: `cd crates && cargo check --workspace --all-targets`, `cargo test --workspace`,
  `cargo clippy --workspace --all-targets`.
- **`cargo doc -p xrpl-wasm-vm --no-deps` is part of the loop, not a nicety.** `lib.rs`
  carries `deny(rustdoc::broken_intra_doc_links)`, and neither `cargo test` nor `clippy`
  checks doc links. **Caveat: it does not cover private modules**, which are not documented
  by default — a dead link inside `abi.rs` passes silently (this is how a `VmState::scratch`
  link survived the `out_buffer` rename). Add `--document-private-items` to check those, and
  grep after renaming a field. `lib.rs` also carries `forbid(unsafe_code)`,
  `deny(unreachable_pub)` and `deny` on four clippy cast lints, so an unargued cast fails the
  build rather than warning.
- **Tests come in two kinds, and the split is forced.** A wasmi `Caller` exists only during
  a host call, so everything in `abi.rs` that takes one cannot be reached from a unit test.
  Unit tests in `src/` cover what needs no live instance (wire conversions, budget
  arithmetic, the limits); guest-memory policy lives in `tests/`, running real modules
  against a configurable fake host.
- Those integration tests write modules as **WAT text** and assemble it themselves — `wat` is
  a `[dev-dependencies]` entry and `support::assemble` its only caller, so the assembler
  never enters the library. `run` takes binaries; there is no `run_wat`.
- `tests/support/mod.rs` holds the fake host and the import declarations. `Answer` separates
  *what the host writes* from *what length it reports*, which is what makes the over-cap and
  buffer-fit rules testable without values that large existing.
- Guest-linkability (needs `rustup target add wasm32-unknown-unknown`):
  `cargo check -p xrpl-host-functions --target wasm32-unknown-unknown`. Only the ABI crate —
  `xrpl-wasm-vm` is host-side and pulls in wasmi, and `crates/hello_world` cannot be checked
  for that target at all because it depends on `cxx` → `link-cplusplus`, which wants a C++
  toolchain for the target.
- **The bridge crate's unit tests link only because nothing in them reaches a C++ shim.**
  The `extern "C++"` symbols exist only in the CMake build, and the test binary links
  because `-dead_strip` drops what no test path reaches. Verified: forcing a reference
  (`let f: fn(&ffi::HostContext) -> _ = ...`) fails with `Undefined symbols:
  _rs$wasm_vm$cxxbridge1$…`. So keep those tests on the pure logic — the status map, the
  panic guard, the wire conversions — and put anything that needs a host in the gtest.
- Full C++↔Rust: normal CMake build, then
  `./xrpl_tests --gtest_filter='WasmVMTest.*:*Call.*'`. See "How the C++ tests are built".
- `src/test/app/Wasm_test.cpp` and `HostFuncImpl_test.cpp` are **entirely inside `/* */`**
  and compile to nothing, as is `src/libxrpl/tx/wasm/WasmiVM.cpp`.
- **A stale build directory will fail to link with `duplicate symbol
  '_rust_eh_personality'`.** The conan `wasmi` package ships a Rust `std`, and so does our
  staticlib. `b7059deb9f` dropped the conan requirement but left `find_package(wasmi
  REQUIRED)` and `wasmi::wasmi` in the CMake, both now removed; a build folder generated
  before that still has `build/generators/wasmi-*.cmake`, so re-run `conan install .. 
  --output-folder . --build missing --settings build_type=Debug` and delete them.
- VCS is **jj** (`jj st`, `jj log`), not raw git, for local work.

## Conventions

**Comments.** Terse. A comment should say something the compiler cannot check and the code
cannot show; everything else is a candidate for deletion. Keep: why an apparent redundancy is
not one (the `MAX_FIELD_BYTES` check beside the clamp; `is_fatal`/`host_fatal` as two lists;
`MUST_TRAP` not deriving from `is_fatal` — each of these has been "simplified" wrongly in a
mutation test at least once); load-bearing invariants; hidden contracts a signature cannot
state; wasmi facts that decide a design. Cut: prose restating the next line; the same
rationale on a field and on its reader; retellings of this document.

**No references to C++ that will not survive the merge.** They read as evidence but point at
deleted files. The crate has none, in `src/` or `tests/`. Two live exceptions stand:
`Protocol.h`'s `kMaxWasmDataLength` and `kWasmTransferLimit`, which are where those numbers
are defined for the rest of the system. The parity evidence itself lives here instead — see
the appendix, which is commit-pinned and therefore stays resolvable.

**No historical comments** in code — describe the present, not how it differs from a previous
state.

## Appendix: review findings (2026-07-29)

A read of `vm.rs`, `abi.rs` and `register.rs` against the vendored wasmi 1.1.0. **Seventeen
of the eighteen are closed, C12 the only one left** — earlier revisions of this document said
"fourteen of seventeen", which never matched the table. The rationale that is still
load-bearing has moved into "How the engine works" above.

| # | Finding | Outcome |
|---|---|---|
| A1 | Out-of-gas returned a code instead of trapping, so how much guest code ran after exhaustion was wasmi's business | ✓ two-channel design, `FatalHostError` payload |
| A2 | `run` discarded gas accounting on every failure path, and its error was a `String` | ✓ `RunFailure { error, fuel_used }` over a typed `RunError` |
| A3 | `HOST_MODULE = "host"` matched no guest that exists | ✓ `host_lib`, pinned by test |
| A4 | Transfer budget charged for bytes never copied, and charged before validation | ✓ one call site per write path; reads are free |
| A5 | `Module::new` accepted WAT text — a behaviour the rewrite introduced by accident | ✓ `default-features = false` |
| A6 | The memory export's *name* was a rule the rewrite introduced | ✓ resolved by kind, as C++ did |
| B6 | `AbiRet` was vestigial | ✓ deleted |
| B7 | The `i64` pipeline was pointless and lossy (silent truncating cast) | ✓ `HostResult<i32>` end to end |
| B8 | `cxx` was an unused dependency of this crate | ✓ removed |
| B9 | Seven broken intra-doc links, plus historical comments | ✓ fixed, `deny` added |
| C10 | The `"memory"` export was a string hash lookup on every host call | ✓ resolved once per run, by kind |
| C11 | `read_write` memset 1 KiB of stack per call and did not generalize past one byte input | ✓ replaced by `write_buffered` + `Region` |
| C12 | `Linker` rebuilt per run; module compiled per run with no cache | **open — see "Performance"** |
| D13 | The public surface was accidental (`RunOutcome` unnameable, limits unreachable) | ✓ exported; `MAX_FIELD_BYTES` renamed out of `abi.rs` |
| D14 | `abi.rs` *claimed* every access was a checked slice op | ✓ `forbid(unsafe_code)` + cast lints enforce it |
| D15 | Zero tests | ✓ 79 |
| D16 | `gas = 0` accepted silently; `get_fuel().unwrap_or(0)` reported the whole limit; entry-point diagnostic wrong for a wrong-signature export | ✓ closed — `gas <= 0` is `temBAD_AMOUNT` in `runEscrowWasm` |
| D17 | The start-section TODO reads like a hole | ✓ documented as not closeable with wasmi 1.1 |

Three of the closed findings were **behaviour changes nobody had chosen** — A3, A5, A6 — and
all three restored C++ behaviour the rewrite had altered by accident. That is the pattern
worth carrying into the bridge: on this path, "tidier than C++" is usually "different from
C++". A fourth decision, C11's buffer, extends it rather than restoring it.

## Appendix: reference points from the deleted C++ path

Import names and gas costs are ABI. The rest is *evidence of prior behaviour* — useful for
comparison and for the gas assertions in `Wasm_test.cpp`, not gospel. All recoverable with
`git show b7059deb9f^:<path>` — note that `WasmVM.{h,cpp}` exist again at that path with
entirely different contents, so the revision in that command is doing real work.

Deleted later, with the dead `wasmi::wasmi` link that was the only thing supplying their
`<wasm.h>`: `include/xrpl/tx/wasm/HostFuncWrapper.h` (the `*_proto` aliases and `*_wrap`
declarations, whose `.cpp` went in `b7059deb9f`) and `WasmImportsHelper.h` (`ImportVec`,
`WasmImpArgs`'s `static_assert`). Every remaining reference to either was inside a
commented-out file. The `_proto` aliases are the C lowering the table above reproduces, so
they are worth reading before extending it.

- **Import names + per-call gas**: `src/libxrpl/tx/wasm/WasmVM.cpp`
  (`setCommonHostFunctions`, 64 entries plus `set_data` registered only in
  `createWasmImport`; e.g. `ldgr_index` 60, `sha512_half` 2000, `set_data` 1000, `float_pow`
  5'500).
- **Guest-visible error codes**: `HostFunctionError` in `include/xrpl/tx/wasm/WasmCommon.h`
  (-1 `Unimplemented` … -20 `FloatComputationError`; note **-11 is `OutOfTransferLimit`**
  there, `InvalidDecoding` in the SDK — question 3).
- **Host-fatal conditions were traps**: out-of-gas and internal errors threw
  `hfErrOutOfGas` / `hfErrInternal` → trap → `tecOUT_OF_GAS` / `tecINTERNAL`. Only the
  transfer limit was a soft, guest-visible failure.
- **Limits**: `maxPages = 128` (8 MiB), `kMaxWasmDataLength = 1024`, `kWasmTransferLimit =
  1 << 20`. The last two are still live in `include/xrpl/protocol/Protocol.h:328,333`.
- **Transfer limit** was charged for bytes actually copied: host→guest writes (`setData`) and
  typed reads materialising a host object (uint256, AccountID, Currency, Asset), plus
  unaligned `FieldLocator` copies (+`unalignedGas = 50`). Plain slice/string reads were not
  charged.
- **Check order after a value existed** (`setData`): params → data-too-large → no-memory →
  out-of-bounds → buffer-too-small → transfer → copy. Inputs (`getDataSlice`) were validated
  before the call. `write_buffered` follows this; `write_into` cannot, since it must
  bounds-check before handing over a slice.
- **Entry point** was `escrow_finish` (`escrowFunctionName`); gas `-1` meant unlimited,
  `<= 0` meant `temBAD_AMOUNT`; on out-of-gas the reported cost was the full limit. Positive
  return = conditions met; `0` or negative = reject.
- **wasmi's fuel table is consensus input** — pin the version deliberately (currently
  `wasmi = "1.1.0"`).

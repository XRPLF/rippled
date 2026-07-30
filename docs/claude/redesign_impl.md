# rippled fork — Rust WASM VM work

## What this branch is doing

We are on `Wasm-vm-redesign`: replacing the C++ wasmi **C-API** integration with a
Rust wasmi wrapper, written as **refined, production-ready code** built on the ideas
of the PoC — not a cleanup pass over the PoC itself.

`Rust_wasm_PoC` (and `Rust_wasm_PoC_benchmark`) are **reference branches**: the PoC
lives there, read-only, to be consulted for approach and prior art. Code copied
across from it is a starting point, not a baseline to preserve — the PoC's shapes,
comments and trade-offs are all open for redesign here. Its crates are named
differently: `host_functions`, `host_functions_macros`, `wasm_vm` (with `imports.rs`
where we have `register.rs`, plus `ffi.rs`), `stdlib`, `example_contract`. Read them
with `git show Rust_wasm_PoC:crates/<path>`.

**Read this before `register.rs` confuses you.** `abi.rs`/`register.rs`/`vm.rs` were
brought over from the PoC in `d8d1ec46` ("WIP"), and the PoC's macro was doing far more
than ours: `host_abi!` inserted `&self`, wrapped the declared return in `HostResult<_>`,
and — for a `Vec<u8>` or `[u8; N]` return — **appended `out: &mut [u8]` and replaced the
return with `HostResult<usize>`** (`crates/host_functions_macros/src/lib.rs` on that
branch). So a declaration reading `-> [u8; 4]` produced a trait method taking an output
region, which is why the copied VM code expects one. It also generated the whole wasm32
guest side. This branch does none of that: the declaration *is* the signature. Those
transformations are what "no magic" refers to throughout this document.

The C-API path is already gone: commit `b7059deb9f` ("Remove wasmi dependency")
deleted `WasmVM.{h,cpp}`, `WasmiVM.h`, `HostFuncWrapper.cpp` and dropped the conan
`wasmi` package; `src/libxrpl/tx/wasm/WasmiVM.cpp` is now one big comment block kept
only for reference. Anything we need about the old semantics is recoverable with
`git show b7059deb9f^:<path>` — do that rather than guessing.

## Where the code lives

- `crates/` — cargo workspace (edition 2024, resolver 3), built into the C++ build
  via corrosion (`crates/CMakeLists.txt`).
  - `crates/xrpl-host-functions/` — `no_std` crate holding the ABI declaration:
    `host_functions! { ... }` generates the `HostFunctions` trait + the
    `HostFunctionSpec` enum (wasm import name + gas per function). Also `HostError`.
    **This crate is the single source of truth for the ABI.**
    Each declaration spells its receiver — always `&self`, checked by the macro, so
    a declaration reads exactly as the trait method it becomes. `&self` is what lets
    the VM hold the host as one shared `&dyn HostFunctions` in the wasmi `Store`; a
    host that needs to mutate uses interior mutability.
  - `crates/xrpl-host-functions-macros/` — the `host_functions!` proc macro. An
    implementation detail of the crate above: the dependency arrow runs facade →
    macro, and the macro depends on nothing but syn/quote. It is deliberately *not*
    re-exported — the ABI has one declaration site, so nothing outside
    `xrpl-host-functions` should be invoking it.

    **Convention: the expansion is closed.** Every name in it is either generated
    or written in the declarations — `Self::Variant` is the only path it builds, and
    a test (`names_no_crate_of_its_own`) enforces that. So the macro owns
    `HostFunctions`, `HostFunctionSpec`, `ALL`, `wasm_name()`, `gas()`, and the
    private `HostFnSpec` row type that keeps both accessors fed from one `match`.
    The facade hand-writes only the *vocabulary the declarations are written in* —
    `HostError` (23 codes plus `from_code`, which wants to stay greppable and
    testable), `HostResult`, `HASH_LEN`. Those resolve at the call site because the
    declarations name them, exactly like `Vec<u8>` and `&[u8]`; the macro never
    emits them.

    Corollary: `HostFnSpec` and `spec()` are **private** to the ABI crate. Read the
    table through `HostFunctionSpec::wasm_name()` / `::gas()`.

    The macro crate dev-depends on the facade so its doctest — whose declarations
    name `HostResult` — compiles; cargo allows that cycle because dev-dependencies
    sit outside the library build graph.
  - `crates/xrpl-wasm-vm/` — the wasmi wrapper: `vm.rs` (engine/store/run),
    `abi.rs` (gas + transfer-limit + guest-memory marshaling), `register.rs`
    (hand-written `Linker::func_wrap` per host function).
  - `crates/xrpl-wasm-vm-ffi/` — cxx bridge to C++. **Still empty** (`mod ffi {}`);
    nothing is wired to C++ yet.
- `include/xrpl/tx/wasm/`, `src/libxrpl/tx/wasm/` — C++ side: `HostFunc.h` (the
  ~60-method `HostFunctions` interface the ledger implements), `HostFuncImpl*.cpp`
  (its implementations), `WasmCommon.h` (`HostFunctionError`, `Wmem`, `WasmTER`,
  `FieldLocator`), `README.md` (ABI docs, worth reading — but stale in places, see below).

## The ABI crate is a library both sides link (2026-07-29)

`xrpl-host-functions` is the single source of truth, and the way that is realised is:
**it is consumed as an ordinary dependency**, by `xrpl-wasm-vm` today and by the guest
stdlib next. Neither invokes `host_functions!` — the macro has exactly one call site,
inside the ABI crate itself, which is why it is deliberately not re-exported. Consumers
get the *generated code*, not the generator.

That makes four properties load-bearing rather than incidental:

| Property | Why | Status |
|---|---|---|
| `#![no_std]`, **no allocator** | the guest stdlib is strictly `no_std` | ✓ `Vec` left the ABI when byte outputs became `out: &mut [u8]`; `extern crate alloc` went with it |
| **zero runtime dependencies** | anything else must also build for the guest | ✓ `cargo tree` is the proc-macro crate alone (build-time, host-side) |
| builds for **`wasm32-unknown-unknown`** | it links into the guest | ✓ verified 2026-07-29 |
| the trait is implementable by **both** sides | one declaration, two implementors | ✓ see below |

The last one is what the out-param shape buys. A host impl writes into `out` and returns
the length; a guest impl forwards to the import, passing `out.as_mut_ptr()` / `out.len()`
and decoding the returned `i32` through `HostError::from_code`. One trait serves both
*because it is now the wire shape* — with value-returning signatures the guest side
would need the macro to transform them again, which is exactly the PoC magic we removed
(see "The lowering table" below).

A side effect worth having: the guest inherits `HostError::from_code`, which range-checks
the wire code. The SDK today transmutes it unchecked — open question 3.

**Known gap.** The `#[link(wasm_import_module = "…")] unsafe extern "C" { … }`
declarations are *not* generated; the PoC's `host_abi!` did generate them, along with a
`GuestHost` impl, behind `#[cfg(target_arch = "wasm32")]`. If stdlib hand-writes that
extern block, it is precisely the drift the single source of truth exists to prevent, so
generating it is the natural follow-up. One wrinkle to decide first: the generated guest
impl needs `HostError::from_code`, a name no declaration mentions, so it would be the
first thing to put a vocabulary dependency back into the expansion (which is otherwise
closed — see the convention note above).

## Agreed direction (2026-07-28)

- **Nothing has been released yet.** We follow XLS-0102 for the *shape* of the ABI
  (import names, signatures, error-code-as-negative-i32 convention, limits), but we
  are free to fix behaviour that is simply wrong — we are not bound to reproduce the
  deleted C++ implementation bug-for-bug.
- What XLS-0102 actually pins down is thinner than the C++ code implies: there is
  **no error-code table in the spec** (only "negative return = error code"), so the
  binding authority for the numeric codes is the guest SDK, not `HostFunctionError`.
  The spec *does* say gas exhaustion "triggers immediate execution halting" — so
  out-of-gas must **trap**, never return a code. It states a "1 MiB limit, per host
  function call, on total data transfer across the WASM boundary" and a "1 KiB limit
  … in a single host function call"; the C++ implementation made the 1 MiB a
  per-invocation budget, which is stricter than a literal reading (unresolved).
- **Host-function registration stays hand-written** in `xrpl-wasm-vm/src/register.rs`
  for now — generating it from `host_functions!` was tried and the macro got too
  complicated. Reduce the per-function boilerplate with a small set of generic
  adapters in `abi.rs` instead of codegen. (Revisited 2026-07-29 — see below. The
  target is macro-emitted *typed shims* rather than full codegen, but it is a later
  refactor, not a prerequisite.)

## C-level ABI compatibility (2026-07-29)

### The requirement

Guests must not be limited to Rust. C — and any language targeting wasm32 — must be
able to call host functions.

### This is already satisfied at the wire, by construction

WASM imports can only carry `i32`/`i64`/`f32`/`f64`. There is no way to expose a
non-C-expressible host function. Proof already in-tree, a plain C guest with no Rust
anywhere:

```c
// src/test/app/wasm_fixtures/ledgerSqn.c:3
int32_t ldgr_index(uint8_t *, int32_t);
```

`register.rs` is what defines the C signature: wasmi derives the `FuncType` from the
closure's **parameter and result Rust types** (each must implement `WasmTy`);
`Caller<'_, VmState<'_>>` is special-cased and excluded. Parameter *names* are not
part of the ABI. The full contract a C author binds against is:

1. import module name,
2. import (field) name,
3. ordered param `ValType`s,
4. result `ValType`,
5. the return-value semantics (negative = error code; non-negative meaning varies —
   see "Return conventions are not uniform" below).

### What the C++ path had that the Rust redesign lost

The deleted C++ code declared each host function as a **literal C function type**:

```cpp
// include/xrpl/tx/wasm/HostFuncWrapper.h
using getLedgerSqn_proto = int32_t(uint8_t*, int32_t);
using trace_proto        = int32_t(uint8_t const*, int32_t, uint8_t const*, int32_t, int32_t);
using traceNum_proto     = int32_t(uint8_t const*, int32_t, int64_t);
```

`WasmImpArgs`/`WasmImpRet` (`include/xrpl/tx/wasm/WasmImportsHelper.h:41-84`) mapped
pointer/`int32_t` → `WtI32`, `int64_t` → `WtI64`, and `static_assert`-ed on anything
else. **C-expressibility was compile-enforced — you could not declare a host function
that wasn't C-callable.**

Caveat worth remembering: `_proto` was a *second, hand-maintained* declaration
alongside the virtual method in `HostFunc.h`. `WasmImpArgs` asserted `_proto` was
C-shaped; nothing checked that `_proto` matched the method it wrapped. That pairing
was hand-synced in `HostFuncWrapper.cpp`.

In the Rust redesign the wasm signature exists only as an emergent property of how
someone hand-typed a closure in `register.rs`. Nothing prevents a future arm from
omitting an out-pair, and nothing tells a C author what the signature is.

### Decision: the source of truth does not move

`crates/xrpl-host-functions/` stays the one declaration. C compatibility adds a
**third output** next to the trait and the spec enum — not a second input. The C
header becomes a *generated, checked-in artifact* with a CI diff. One generated
declaration that cannot drift is strictly stronger than two explicit ones that can.

"Explicit vs hidden" is the wrong axis; **"derivable and emitted"** is the right one.
C authors read a header — they do not read the macro.

### The lowering table (the missing rule)

The existing DSL vocabulary already implies this; it was simply never written down.
That is the entire gap.

```
params, in declared order:
  &self              -> nothing                   (receiver, not part of the ABI)
  i32, bool          -> i32                       (bool: nonzero = true)
  i64                -> i64
  &[u8], &str        -> i32 ptr, i32 len          const uint8_t*, int32_t
  &mut [u8]          -> i32 ptr, i32 len          uint8_t*, int32_t   (an output region)

returns, always `HostResult<T>`; `Err(e)` -> negative code, or a trap when host-fatal:
  HostResult<usize>       -> result i32 = bytes written into the output region
  HostResult<i32>, <bool> -> result i32 = the value
  HostResult<()>          -> result i32 = 0
```

Total, unambiguous, and **positional**: every wasm parameter is a declared parameter,
in order, so the C prototype is a direct reading of the declaration rather than
something the macro appends to it. **The macro must reject any type not in this
table** — that is `WasmImpArgs`' `static_assert`, restored, and it is what guarantees
the C API is always surfaceable.

**Validation** — all five current declarations (the `host_functions!` block at the
bottom of `xrpl-host-functions/src/lib.rs`) lower to exactly the deleted C++ `_proto`
aliases. Abbreviated below by dropping `&self`, which contributes no C parameter.

| Declaration | Derived C | C++ `_proto` |
|---|---|---|
| `fn get_ledger_sqn(out: &mut [u8]) -> HostResult<usize>` | `int32_t(uint8_t*, int32_t)` | `getLedgerSqn_proto` ✓ |
| `fn get_current_ledger_obj_field(field: i32, out: &mut [u8]) -> HostResult<usize>` | `int32_t(int32_t, uint8_t*, int32_t)` | `getTxField_proto` ✓ |
| `fn sha512_half(data: &[u8], out: &mut [u8]) -> HostResult<usize>` | `int32_t(const uint8_t*, int32_t, uint8_t*, int32_t)` | ✓ |
| `fn trace(msg: &str, data: &[u8], as_hex: bool) -> HostResult<()>` | `int32_t(const uint8_t*, int32_t, const uint8_t*, int32_t, int32_t)` | `trace_proto` ✓ |
| `fn trace_num(msg: &str, number: i64) -> HostResult<()>` | `int32_t(const uint8_t*, int32_t, int64_t)` | `traceNum_proto` ✓ |

**Discipline the table requires**: a byte output is an explicit `out: &mut [u8]`
parameter plus `HostResult<usize>`, never a returned value. `get_ledger_sqn` writes 4
LE bytes and returns 4 — it does *not* return the sequence number, and by the same
rule `float_to_int` takes an out region rather than returning `i64`. A scalar
`HostResult<T>` means value-in-the-return-register (`get_tx_array_len(field: i32) ->
HostResult<i32>`, `nft_flags`, `float_cmp`, `cache_le`, `check_sig`,
`amendment_enabled`).

The contract on an out region, which the engine relies on: **write only if the value
fits, and return its true length either way.** The host therefore never needs to know
the guest's buffer size — the engine turns `n > cap` into `BufferTooSmall`.

### Closing the drift gap between `register.rs` and the generated header

**wasmi 1.1 cannot introspect a registered host function's signature.**
`Linker::get` returns `None` for `func_wrap`'d functions — they land in
`Definition::HostFunc` (`wasmi-1.1.0/src/linker.rs:147`), not `Definition::Extern`
(doc comment at `:335`). `Definition::ty()` exists at `:171` and would give the
`FuncType`, but `Definition` and `get_definition` are private. So "assert
`Func::ty()` equals the spec" is **not available**.

Guarantee ladder:

| Approach | `register.rs` | Guarantee |
|---|---|---|
| Generate closures wholesale | disappears | by construction |
| **Generate `link_*` shims, hand-write bodies** | **stays, readable** | **compile-time** |
| Hand-write everything + probe-module test | stays | test-time |

**Preferred: the middle row.** The macro emits the *type* without emitting the *body*:

```rust
// generated by host_functions!
pub type Sha512HalfFn =
    fn(Caller<'_, VmState<'_>>, i32, i32, i32, i32) -> Result<i32, wasmi::Error>;

pub fn link_sha512_half(l: &mut Linker<VmState<'_>>, f: Sha512HalfFn)
    -> Result<(), LinkerError>
{
    l.func_wrap(MODULE, HostFunctionSpec::Sha512Half.wasm_name(), f)
}
```

`register.rs` keeps its hand-written bodies but becomes constrained:

```rust
HostFunctionSpec::Sha512Half => link_sha512_half(linker,
    |mut caller, data_ptr, data_len, out_ptr, out_len| { /* logic, unchanged */ }),
```

Wrong arity, wrong scalar type or wrong return is now a **compile error**. The same
lowering table emits both `Sha512HalfFn` and
`int32_t sha512_half(const uint8_t*, int32_t, uint8_t*, int32_t);`, so they cannot
drift. That type alias is the regenerated `_proto` — the artifact C++ had, now derived
from the single source of truth instead of maintained beside it.

*Constraint*: `fn` pointers only accept non-capturing closures. Every arm in
`register.rs` today is non-capturing. If one ever needs to capture, that shim can take
`impl Fn(...) + Send + Sync + 'static` instead — weaker inference, same guarantee.

**Belt-and-braces (cheap, worth having anyway)**: a probe-module test. Synthesize a
WAT module from the spec table that imports every host function with its declared
type, then `linker.instantiate()` it. A signature mismatch fails instantiation. This
is the only check that also catches module-name and missing-import mistakes, and it
tests the *guest's* view end-to-end.

### Open: where the output region points (2026-07-29)

Nothing above depends on this — the declaration is the same either way, and it is
internal to `abi.rs`. Both `register.rs` and the trait are untouched by the choice.

`write_into` today hands the host a slice **of guest linear memory**
(`mem.data_mut(&mut *caller).get_mut(dst..end)`), so the host writes straight into wasm
memory with no copy. The cost is that this `&mut` borrow cannot coexist with a `&`
borrow of guest memory for the inputs, which is the only reason `read_write` exists: it
memcpies the input into a `[0u8; MAX_FIELD_BYTES]` stack array first. That does not
generalize — `credential_keylet`, `check_sig` and `paychan_keylet` each take three byte
inputs, so each would need its own stack buffer.

The alternative is a **host-side scratch buffer** the adapter owns, with one copy into
guest memory after the call. Then inputs stay borrowed from guest memory (any number of
them, zero copies), `read_write` disappears, and the fit check precedes the guest write.
This is what C++ did (`std::expected<Bytes, …>` + `setData`), so gas/behaviour parity
is preserved.

Cost is roughly a wash:
- `sha512_half` — today ≤1 KiB input copied to stack + 32 bytes written ≈ 1056 bytes
  moved. Scratch: input borrowed zero-copy, 32 bytes copied out. **Better.**
- `get_tx_field` (no byte input, ≤1 KiB output) — today 1024 direct; scratch 1024 +
  1024. **Worse.**

**One argument for scratch has since been spent, and it was the strongest one.** It used
to be that `write_into` checked `n > cap` *after* `fill` had already written, so a
refused call left bytes in the guest's buffer, and only a scratch buffer could check
before the copy the way C++'s `setData` did. **A4 closed most of that without scratch**:
`fill` receives at most `min(cap, MAX_FIELD_BYTES)`, so an over-cap value cannot reach
guest memory at all. What remains is narrower — under the cap the clamp is a no-op, so a
host that cannot fit a value could still leave a prefix behind, and a scratch buffer
would make that impossible rather than contractual. So the wart is now a **host-contract
question, not an engine defect**, and it should carry much less weight in the decision
than the paragraph above once implied. Judge C11 mainly on the cost table and on
`read_write` not generalizing past one byte input.

### Status: this is the open decision (2026-07-30)

The VM compiles and works, so the reason this was deferred is spent, and C10 has landed —
so the per-call export lookup both designs would otherwise pay is already gone. **This is
now the next thing on the list and it needs answering before C11 is codeable.** The typed
shims, generated header and probe-module test stay deferred.

## Open ABI questions and interop risks (2026-07-29)

Found while auditing the guest SDK (`~/Documents/rust/xrpl-wasm-stdlib`, checkout
`435a091f`) against this fork. As of 2026-07-30, **question 1 is resolved and question 3
is narrowed**; each says so in place. The rest are open, and all of them are decisions
rather than code.

1. **Import module name.** The old C++ VM ignored it entirely —
   `wasm_importtype_module()` is commented out at `src/libxrpl/tx/wasm/WasmiVM.cpp:429-431`
   and only the field name is looked up. `register.rs:8` enforced `"host"` when this was
   audited. The SDK
   and the fork's own fixture (`src/test/app/wasm_fixtures/codecov_tests/src/host_bindings_loose.rs:20`)
   use `"host_lib"`. Plain clang emits `"env"` unless annotated. `"host"` matched
   nothing that exists. **Resolved: `host_lib`** (finding A3).
2. **Import name lineage.** The fixtures pin the SDK at `branch = renames` and use
   **short** wire names (`parent_ldgr_hash`, `cache_le`, `tx_inner_arr_len`,
   `accountroot_id`, `trustline_id`), matching rippled's `ldgr_index` / `home_le_field`
   / `sha512_half`. The standalone SDK checkout is the **long**-name lineage
   (`get_parent_ledger_hash`, `cache_ledger_obj`, `compute_sha512_half`). Which is
   authoritative is undecided.
3. **New error codes are UB in the guest.** The SDK decodes with a bare transmute and
   no range check — `xrpl-common-stdlib/src/host/mod.rs:325`,
   `unsafe { core::mem::transmute(code) }` — valid only for `-1..=-20`. Our `HostError`
   adds `NoRuntime = -21`, `OutOfGas = -22`, `OutOfTransferLimit = -23`, and
   `to_wasm_i32` returns all of them as codes.
   *Fix that solves this and the XLS-0102 halting requirement together*: make
   host-fatal errors **traps**. The closure returns `Result<i32, wasmi::Error>`; the
   wasm signature is unchanged (still `(…) -> i32`), and the guest-visible table
   collapses back to exactly `-1..-20`. This also restores the C++ two-channel design
   (`"HfOutOfGas"` / `"HfInternal"` trap strings vs negative returns).
   *Still open*: is `OutOfTransferLimit` soft or fatal? The guest has no code for it —
   `-11` is `InvalidDecoding` there but `OutOfTransferLimit` in `WasmCommon.h:47`.
   Fatal is the only resolution that needs no SDK change.

   **Partly resolved by A1**, and the remainder is sharper for it. The trap channel
   exists, and `OutOfGas = -22` no longer reaches the guest at all. But the decision
   was **`OutOfTransferLimit` stays soft** (C++ parity — it was the one soft failure
   there), so `-23` still reaches a guest that transmutes it, and `NoRuntime = -21`
   still would if anything returned it. So the guest-visible table is `-1..-20` plus
   those two, not `-1..-20`: closing this needs either a range check in the SDK or
   `OutOfTransferLimit` remapped onto an in-range code. The soft/fatal question is
   settled; the encoding question is not.
4. **`-1` collides semantically**: host `Unimplemented` vs guest `InternalError`.
5. **`float_to_mant_exp` byte count.** Host returns **12** (8 mantissa + 4 exponent,
   `HostFuncWrapper.cpp:497` at `b7059deb9f^`); the guest doc says 8. The guest's
   `match_result_code_with_expected_bytes` **panics** on a non-negative mismatch.
6. **Return conventions are not uniform** — six of them, today documented only in
   comments: bytes-written; value-in-return (`*_arr_len`, `nft_flags`); boolean 0/1
   (`amendment_enabled`, `check_sig`); 1-based handle (`cache_le`, always ≥ 1);
   status-0 (`trace*`, `set_data`); tri-state (`float_cmp` — `0` equal, `1` first >
   second, `2` first < second).
7. **The SDK's drift checker is silently broken.** `tools/compareHostFunctions.js`
   regex-parses `WasmVM.cpp` and `HostFuncWrapper.h`, both deleted at HEAD. A
   generated header would give it a stable target again.

Also noted: `include/xrpl/tx/wasm/README.md` is stale — its worked example uses the
long name `get_ledger_sqn` where the code registered `ldgr_index`, and it references
`detail/WasmVM.cpp`, `detail/HostFuncWrapper.cpp` and `ParamsHelper.h`, none of which
exist (the helper is `WasmImportsHelper.h`).

## `xrpl-wasm-vm` review findings (2026-07-29)

A read of all three files (`vm.rs`, `abi.rs`, `register.rs`) against the vendored
wasmi 1.1.0 source. Grouped by kind and ordered within each group by how much they
matter. Items marked ✓ are done.

### A. Correctness — behaviour changes, land before the cxx bridge

1. ✓ **Out-of-gas is not a trap, and how much guest code runs after exhaustion is
   wasmi's business.** `charge` (`abi.rs:77`) returned `HostError::OutOfGas`, which
   `to_wasm_i32` hands the guest as `-22` with fuel already at 0. wasmi meters by
   emitting `ConsumeFuel` instructions at *block boundaries*
   (`engine/translator/func/instrs.rs`), so the guest keeps executing to the end of
   the current basic block before it traps. The stopping point is a function of
   wasmi's block layout — implementation-defined behaviour on a consensus path.
   XLS-0102 requires immediate halting and C++ trapped (`hfErrOutOfGas`); `-22` is
   also outside the range the SDK's `transmute` accepts (open question 3). Fix is the
   two-channel design: host-fatal errors (`OutOfGas`, `Internal`, `NoMemExported`)
   return `Err(wasmi::Error)` from the closure and trap. The wasm signature is
   unchanged. This reshapes `abi.rs`'s return type, so it precedes any cosmetic work
   there.

   Landed with A2 and A3. The fatal set is carried out of a closure as
   `abi::FatalHostError(HostError)`, a payload `wasmi::Error::host` accepts and
   `run` names again with `downcast_ref` — so the condition survives the crossing
   as a value rather than as message text, which is what the C++ path had to
   string-compare (`"HfOutOfGas"`). `is_fatal` spells the set variant by variant,
   so which channel a new `HostError` takes is a choice someone makes rather than
   one its number makes for it. **`OutOfTransferLimit` stays soft** — the decision
   below, and C++'s behaviour.
2. ✓ **`run` discards gas accounting on every failure path.** `Result<RunOutcome,
   String>` (`vm.rs:96`) meant a trap yielded `Err(String)` with no `fuel_used` — but a
   contract that traps or exhausts gas still has to be charged (C++: full limit →
   `tecOUT_OF_GAS`; internal → `tecINTERNAL`). `String` also cannot be matched on, so
   the cxx bridge would end up string-comparing error text, which is exactly what the
   deleted C++ did with its `"HfOutOfGas"` trap strings. `fuel_used` belongs on both
   paths, and the error wants to be a typed enum C++ can map to a TER.

   Now `Result<RunOutcome, RunFailure>`, where `RunFailure` is `{ error: RunError,
   fuel_used }` — so the gas is on both paths by construction rather than by
   remembering. `RunError` is `Compile`/`Instantiate`/`EntryPoint`/`Trap`, each
   carrying wasmi's diagnostic, plus `OutOfGas`/`Internal`/`NoMemory`, which carry
   nothing because the variant *is* the information C++ needs. Gas exhaustion
   reaches `run` by two routes — wasmi's own `OutOfFuel` for guest instructions,
   our trap payload for a refused host charge — and both land on `OutOfGas`.
   `guest_halted` asks that question at **every** stage from instantiation on, so a
   start section that burns the limit is `OutOfGas` and not `Instantiate`: the stage
   a run stopped at is not what the caller maps.
3. ✓ **`HOST_MODULE = "host"` (`register.rs:8`) matches no guest that exists** — the SDK
   and this fork's own fixtures use `host_lib`, plain clang emits `env`. A decision,
   not a code fix, but nothing real instantiates until it is made (open question 1).
   **Decided: `host_lib`**, matching the SDK and the fixtures. `the_import_module_name_must_match`
   now rejects `host`, `env` and the empty name, so the choice is pinned rather than
   incidental.
4. ✓ **The transfer budget is charged for bytes that are never copied, and charged
   before validation.** `read_borrowed` *aliases* guest memory — zero copies — yet
   called `charge_transfer` (`abi.rs:135`); C++ deliberately did not charge plain
   slice/string reads (`trace` msg/data, `sha512_half` input — see "Reference points"
   below). The charge also precedes the bounds check, so a guest can drain the 1 MiB
   budget with out-of-bounds pointers. Related, in `write_into`: `fill` gets a slice
   of the guest's full `cap`, uncapped by `MAX_WASM_DATA_LEN`, so an over-cap value
   lands in guest memory before `n > MAX_WASM_DATA_LEN` rejects it — clamping `out` to
   `min(cap, MAX_WASM_DATA_LEN)` makes that post-check unreachable by construction.

   Landed, with **one claim in the paragraph above corrected**: the clamp does *not*
   make the post-check unreachable, and the check is load-bearing. `fill` reports the
   value's *true* length, which can exceed the region it was handed, so `n >
   MAX_FIELD_BYTES` is what turns an over-cap value into `DataFieldTooLarge` instead
   of a silently-accepted 1025-byte count. Deleting it fails three tests. The clamp
   and the check bound different things: the clamp bounds the **bytes** that can reach
   guest memory, the check bounds the **status** the guest is given.

   Both read charges are gone — `read_borrowed`'s and `read_write`'s input — so
   `charge_transfer` has exactly one call site, in `write_into`, and the budget means
   what C++ meant by it: bytes actually copied host→guest. Removing the read charge
   also dissolves the out-of-bounds drain rather than reordering around it. Nothing
   replaces those charges: gas already bounds how many reads a run can make, since
   every host call pays its spec's gas before its body runs, which is the property
   C++ relied on. The bounds check still spans the guest's **whole declared `cap`**,
   not the clamped length, so a buffer running past memory is `PointerOutOfBounds`
   even when its first `MAX_FIELD_BYTES` bytes would have been valid.

   *Partly a host contract, not an engine guarantee.* The engine guarantees at most
   `min(cap, MAX_FIELD_BYTES)` bytes are **writable**. That a refused over-cap value
   leaves *nothing* behind additionally relies on the host writing only when the whole
   value fits `out` — what `setData` did and what `Answer::bytes` does. A host impl
   that scribbled the clamped prefix and then reported a larger `n` would still leave
   bytes behind. Worth stating in the `HostFunctions` declaration's doc comment; the
   ABI crate was not touched here.
5. ✓ **`Module::new` accepted WAT text — a behaviour the rewrite introduced by
   accident.** wasmi's default features include `wat`, and `Module::new` runs
   `wat::parse_bytes` over its input (`module/mod.rs:228`), so the VM compiled
   text-format modules straight from a transaction blob and `wat`/`wast`/`bumpalo` sat
   in the release build.

   **The C++ path did not do this**, and the reason is worth recording, because it is
   the whole finding. `ModuleWrapper::init` called `wasm_module_new` with the raw
   transaction bytes (`WasmiVM.cpp:314-318` at `b7059deb9f^`), which wraps
   `Module::new` (`crates/c_api/src/module.rs:54` of the `wasmi/1.0.9` conan package).
   wasmi 1.0.9 carries the *same* `#[cfg(feature = "wat")]` parse and the same
   `default = ["std", "wat"]` — but the C-API crate takes wasmi with
   `default-features = false` (wasmi workspace `Cargo.toml:34`) and never re-enables
   `wat` (`wasmi_c_api_impl` has only `std`, `prefix-symbols`, `simd`). So that line was
   compiled out of the C++ build, and the C-API exposes no wat2wasm entry point either —
   unlike wasmtime's, `wasmi.h` has nothing of the kind. Binary only, and no mention of
   WAT anywhere in the deleted C++ wasm sources.

   Linking the wasmi *Rust* crate directly is what picked the default up: the feature
   the C-API had already turned off upstream came back silently. `default-features =
   false, features = ["std"]` restores parity — it is not a new policy. (The secondary
   argument still holds: it also keeps a module's validity a protocol rule rather than a
   function of a cargo flag.) The tests assemble text themselves from a dev-dependency,
   so nothing of ours is needed to keep them working — see "Build / test loop".

### A, addendum: the memory export's *name* is a rule the rewrite introduced (2026-07-30)

Found while scoping C10, and it is the same class of item as A3 and A5: a behaviour
change nobody chose.

`abi.rs` resolves guest memory with `caller.get_export("memory")` — **by name**. The C++
path did not use the name at all. `InstanceWrapper::getMem`
(`WasmiVM.cpp:224-249` at `b7059deb9f^`) scanned the instance's exports for the first one
whose *kind* is `WASM_EXTERN_MEMORY`, whatever it was called:

```cpp
if (wasm_extern_kind(e) == WASM_EXTERN_MEMORY) { memIdx_ = i; mem = ...; break; }
```

So a module exporting its memory as `"mem"` or `"linear"` worked under C++ and is refused
today — and `the_memory_export_must_be_a_memory_named_memory` in `memory_policy.rs` pins
the stricter rule. With `wasm_multi_memory(false)` the C++ scan was unambiguous: at most
one memory exists, so "the first memory export" names exactly one thing.

Nothing in the wasm spec attaches meaning to the name `"memory"`, or requires a module to
export its memory at all; the name is a toolchain convention (LLVM, Rust's
`wasm32-unknown-unknown`, Emscripten and wasi all emit it), which is why matching on it
works in practice. **The decision to make**: keep the name as an ABI rule, or restore
C++'s match-by-kind. Either is defensible — but if the name stays, it is as much part of
the wire contract as `HOST_MODULE`, and unlike `HOST_MODULE` it is a bare literal inside a
private helper with no named constant and no mention in the ABI docs. That asymmetry is
the part to fix regardless of which way the decision goes.

**Decided: match by kind**, restoring C++'s behaviour, which also dissolves the
asymmetry rather than fixing it — the name is no longer in the code at all. Landed with
C10; see finding 10 for what that forced about start sections.

Second observation from the same code: **C++ already cached the resolution**, memoizing
`memIdx_` on first use. So C10 is not an optimization past the C++ path, it is restoring
something the rewrite dropped. `memIdx_` was a per-`InstanceWrapper` member, which is the
same one-instance-per-run assumption C10's cache would take on.

### B. Dead weight — pure simplification, no behaviour change

6. ✓ **`AbiRet` is vestigial.** `type Out` is always `()`, `impl AbiRet for u32` is never
   used, and the trait's only call site is `<() as AbiRet>::write((), c, ())` — nine
   tokens for `Ok(0)`. Delete the trait and both impls. Done with A1, which rewrote
   those call sites anyway.
7. ✓ **The `i64` pipeline is pointless and lossy.** Every host function returns `i32` on
   the wire, but the internals threaded `HostResult<i64>` and `to_wasm_i32` then did
   `v as i32` — a silent truncating cast on a consensus path. `to_wasm_i64` was dead
   code behind `#[allow]`. `HostResult<i32>` end to end removed both. Done with A1
   for the same reason as B6: A1 rewrites exactly these signatures, and the `n as
   i32` in `write_into` now sits after the `MAX_FIELD_BYTES` check, where it cannot
   lose bits.
8. ✓ **`cxx` is an unused dependency** of this crate — the bridge lives in the ffi crate.
9. ✓ **Stale docs.** Seven broken intra-doc links name types that no longer exist:
   `AbiArg` (`register.rs:20`, `abi.rs:7`), `HostFn` (`register.rs:14,16`),
   `run_escrow` (`vm.rs:19,64`). And `abi.rs:147-150` / `vm.rs:71` are historical
   comments ("used to pay", "The `CxxHost` path additionally used to marshal … that
   too is gone", "Unchanged from the original skeleton"), against the
   no-historical-comments convention. `#![deny(rustdoc::broken_intra_doc_links)]`
   stops the links from rotting again.

   The links are fixed and the `deny` is in. **The historical comments were already
   gone** — the A1–A4 slices rewrote those lines. A sweep for `used to` / `no longer` /
   `formerly` / `originally` / `unchanged from` / `previously` across `crates/` found
   nothing but present-tense prose and C++ reference points, which the convention
   allows. The one stale comment left was in `vm_limits.rs`, citing D16 as an open bug;
   it went with D16.

### C. Performance

10. ✓ **The `"memory"` export is a string hash lookup on every host call.** `memory()`
    (`abi.rs:104`) → `Caller::get_export` → `InstanceEntity::exports: Map<Box<str>,
    Extern>`. Resolve it once after instantiation and keep the `Memory` in `VmState`.
    Two bonuses: `NoMemExported` becomes an instantiation-time error, where it
    belongs, and a per-call failure path disappears. Cheapest real win in the crate,
    and the benchmark can measure it.

    Caching is sound: `wasmi::Memory` is `Stored<MemoryIdx>`, an arena index into the
    store rather than a pointer (`memory/mod.rs:31`), so the handle survives
    `memory.grow` — only the data slice is re-derived, per call, by `data`/`data_mut`.
    It is also worth more than "one lookup per call": `trace` resolves the export twice
    (two `read_borrowed`s) and `sha512_half` twice (`read_write`, then `write_into`).

    **Decline the first bonus.** Failing instantiation when there is no `"memory"`
    export is a *behaviour change*, not a tidy-up: a module that exports no memory and
    makes no host call runs today and would stop. C++ also only discovered this at the
    call, since it resolved the export per call too. The version with identical
    observable behaviour is to resolve eagerly into an `Option<Memory>` in `VmState`,
    leave it `None` when the export is absent, and have the accessor answer
    `NoMemExported` — every call is then free of the lookup and nothing observable
    moves. The residual "`None` after `run` set it" case is a defect in this crate, not
    a guest one, so it belongs on `Internal` rather than `NoMemExported`.

    Consequence for the tests either way: `memory_policy.rs`'s `assert_no_memory`
    asserts `fuel_used > 0`, which holds because the guest burns fuel reaching the
    call. That stays true under the `Option` design and would become `== 0` under
    instantiation-time failure — a useful tell for which design got built.

    **Landed, resolving by kind** (the addendum's decision), so the name is gone from
    the resolution path: `instance.exports(store).find_map(Export::into_memory)`, once,
    after `instantiate_and_start`, into a plain `Option<Memory>` on `VmState`. Plain
    rather than `Cell` because it is written once through `store.data_mut()` before
    `finish.call` and only read after — unlike `transfer_budget`, whose read path holds
    a shared borrow.

    **Kind-matching cannot be lazy, and that decides one behaviour.**
    `Caller::get_export` is name-only and `Caller`'s `instance` field is private
    (`func/caller.rs:13,32`), so exports cannot be enumerated from inside a host call;
    and `Module::instantiate` is `pub(crate)`, so instantiation cannot be split from the
    start section (D17's root cause again). The resolution therefore happens after the
    start section runs, and **a start section can no longer make a host call needing
    memory** — it gets `NoMemExported`. That is parity, not a regression: C++ ran
    `wasm_instance_new` (start included, `WasmiVM.cpp:154`) and filled its export table
    with `wasm_instance_exports` only afterwards (`:161`), so its scan found nothing
    during a start section either. `a_start_section_cannot_make_a_host_call` pins it.
    Today's lazy name-based lookup was the outlier on *both* axes.

    A residual `None` is therefore **not** the `Internal` case sketched above: with
    resolution after instantiation, `None` is reachable for two legitimate guest-caused
    reasons — no memory export, and a call from a start section — so `NoMemExported` is
    the only correct answer.

    Two notes from writing the tests. wasmi's export map is a `BTreeMap` in this feature
    configuration, so `"finish"` sorts first and a kind-blind "first export" resolution
    fails 38 tests rather than a subtle few — cheap to catch. And a global exported as
    `"memory"` **cannot on its own** pin kind-matching: a module with no memory export
    answers `None` under both the correct and the kind-blind resolution, so that
    assertion holds either way. The test needed a second half — a real memory exported
    as `"mem"` *beside* a global named `"memory"`, asserting the call succeeds — which
    states the rule in both directions: the conventional name neither qualifies a
    non-memory nor hides the real one.
11. `read_write` memsets 1 KiB of stack per call and does not generalize past one byte
    input — that is the scratch-buffer decision already open above ("Open: where the
    output region points"), which is now the live one and needs answering before this is
    codeable. #10 makes either choice easier. Note A4 spent that section's
    leaves-bytes-behind argument; read the amendment there before deciding.
12. `Linker` is rebuilt per `run` (five `func_wrap`s plus string interning) and the
    module is compiled per run with no cache. Lower priority. The blocker worth
    recording: `VmState<'h>`'s lifetime forces `Linker<VmState<'h>>` to be per-run —
    a design change rather than a tweak, and one the bridge forces anyway, so it is
    better done with that context than before it.

### D. Hardening

13. ✓ **The public surface was accidental.** `lib.rs` was `pub use vm::run` alone, so
    `RunOutcome` was `pub` inside a private module and unreachable: a caller could
    invoke `run` but not name its return type, and `MAX_MEMORY_PAGES` /
    `TRANSFER_LIMIT_BYTES` / `MAX_MEMORY_BYTES` were likewise unreachable. Exported
    with the test work, since the tests need to name them.

    The 1 KiB per-field cap was a further case, and the odd one out: three of the four
    protocol limits lived in `vm.rs` and were `pub`, while this one sat private in
    `abi.rs` as `MAX_WASM_DATA_LEN`. Being unreachable is why the tests had restated
    `1024`/`1025` as literals twenty-one times. Now `vm::MAX_FIELD_BYTES`, beside the
    others — **renamed**, so a search for the old name (or for C++'s
    `kMaxWasmDataLength`, which its doc comment still cites) lands here.
14. ✓ `#![forbid(unsafe_code)]` — `abi.rs:64` *claims* every access is a checked wasmi
    slice op; let the compiler enforce the claim. Plus `unreachable_pub` and clippy's
    cast lints.

    All three are on, at `deny` — the whole lint block is uniform rather than half
    advisory, so a violation fails the build rather than scrolling past. Verified:
    making `wasm_engine` `pub` again fails `cargo build`, not merely `clippy`. The
    `#[expect]` on the one remaining cast keeps working under `deny`, and being
    `expect` rather than `allow` it also fires if a restructure makes the cast
    unnecessary.

    Both of the new lints paid for themselves.
    `unreachable_pub` found `VmState` and `wasm_engine`: `pub` inside a private module
    and never re-exported, so unreachable from outside the crate — now `pub(crate)`,
    with nothing silenced. The cast lints found **8 sites, all in `abi.rs`**. Six were
    `cast_sign_loss` on the guest's `i32` pointers and lengths, and the fix removed
    code rather than adding it: `let (Ok(ptr), Ok(len)) = (usize::try_from(ptr),
    usize::try_from(len)) else { … }` — **the conversion is the negativity check**, so
    the separate `ptr < 0 || len < 0` guards are gone rather than duplicated. The
    remaining two are the one `n as i32` in `write_into`, bounded by the
    `MAX_FIELD_BYTES` return directly above it, under a scoped `#[expect]` — `expect`
    rather than `allow`, so it fires if a later restructure makes it unnecessary.
15. ✓ **Zero tests.** Nothing checked the bounds/cap/transfer/gas policy, and every
    item above edits exactly that policy. Closed first, for that reason.
16. Minor: `gas = 0` is accepted silently (C++ rejected it as `temBAD_AMOUNT`);
    `store.get_fuel().unwrap_or(0)` (`vm.rs:137`) swallows an error into a
    plausible-looking number; `get_typed_func` failure reports "no entry point" when
    the export exists with the wrong signature.

    **Two of the three are done.** `fuel_used` returns `Result<u64, RunError>`, folded
    through a `failed(store, gas, error)` helper so all four report sites read the
    meter in one place. Worth recording *why* it is not a document-and-assert: the
    `unwrap_or(0)` did not merely swallow an error, it reported `gas - 0`, **the whole
    limit** — an untouched contract charged for everything. No fallback is defensible
    (`0` forgives the run, `gas` overcharges), so a cost that cannot be read replaces
    the outcome with `Internal` rather than being invented. No panic on a consensus
    path. And the entry-point diagnostic is now three cases, told apart by
    `Instance::get_export`: no such export, an export of the wrong signature, and an
    export that is not a function at all — the last two used to claim "no entry point"
    about an export that was right there. `RunError::EntryPoint`'s `Display` carries the
    detail bare for that reason, the one variant without a `stage:` prefix.

    **Still open, and now a decision rather than a bug:** `gas = 0` no longer passes
    silently — it fails with a typed `OutOfGas`. Whether the caller should instead
    reject it up front as C++'s `temBAD_AMOUNT` is a TER question, so it belongs with
    the cxx bridge, where the mapping gets written. (`gas` is `u64`, so C++'s negative
    case cannot arise.)
17. The start-section TODO (`vm.rs:90`) **cannot** be closed with wasmi 1.1's public
    API: there is no `InstancePre`/`ensure_no_start`, and `ModuleHeader::start` is
    private, so only a byte-level section scan would do it. But `set_fuel` and
    `limiter` are both installed *before* `instantiate_and_start`, so start-section
    work is already metered and memory-capped. Recorded because the TODO reads like an
    open hole and is closer to a preference.

## Reference points from the deleted C++ path

Import names and gas costs are ABI; the rest below is *evidence of prior behaviour*,
useful for comparison and for the gas assertions in `Wasm_test.cpp` — not gospel.

- Import names + per-call gas: `git show b7059deb9f^:src/libxrpl/tx/wasm/WasmVM.cpp`
  (`setCommonHostFunctions`, 64 entries + `set_data` registered only in
  `createWasmImport`; e.g. `ldgr_index` 60, `sha512_half` 2000, `set_data` 1000,
  `float_pow` 5'500).
- Guest-visible error codes: `HostFunctionError` in `include/xrpl/tx/wasm/WasmCommon.h`
  (-1 `Unimplemented` … -20 `FloatComputationError`; note **-11 is
  `OutOfTransferLimit`**).
- Host-fatal conditions are **traps**, not return codes: out-of-gas and internal
  errors threw `hfErrOutOfGas` / `hfErrInternal` → trap → `tecOUT_OF_GAS` /
  `tecINTERNAL`. Only the transfer limit is a soft, guest-visible failure.
- Limits: `maxPages = 128` (8 MiB), `kMaxWasmDataLength = 1024`,
  `kWasmTransferLimit = 1 << 20` (both in `include/xrpl/protocol/Protocol.h`).
- Transfer limit is charged for bytes *actually copied*: host→guest writes
  (`setData`) and typed reads that materialize a host object (uint256, AccountID,
  Currency, Asset) plus unaligned `FieldLocator` copies (+`unalignedGas = 50`).
  Plain slice/string reads (`trace` msg/data, `sha512_half` input) are **not** charged.
- Entry point is `escrow_finish` (`escrowFunctionName`); gas `-1` meant unlimited,
  gas `<= 0` meant `temBAD_AMOUNT`; on out-of-gas the reported cost is the full limit.
  Positive return = conditions met; `0` or negative = reject.
- Engine config (fuel on, floats off, all post-MVP proposals off) is in the commented
  `WasmiVM.cpp` `WasmiEngine::init()`; `crates/xrpl-wasm-vm/src/vm.rs` mirrors it.
- wasmi's fuel table is consensus input — pin the wasmi version deliberately
  (currently `wasmi = "1.1.0"`). `src/test/app/Wasm_test.cpp` asserts exact gas numbers
  (e.g. 29'502) and is the best parity oracle we have.

## Build / test loop

- Fast: `cd crates && cargo check --workspace --all-targets`, `cargo test --workspace`,
  `cargo clippy --workspace --all-targets`.
- **`cargo doc -p xrpl-wasm-vm --no-deps` is part of the loop, not a nicety.**
  `lib.rs` carries `deny(rustdoc::broken_intra_doc_links)`, and neither `cargo test` nor
  `clippy` checks doc links — so a rename that leaves a `[`link`]` dangling passes both
  and fails only here. Add `--document-private-items` to check the links on private
  items too, which is most of this crate. `lib.rs` also carries `forbid(unsafe_code)`,
  `deny(unreachable_pub)` and `deny` on four clippy cast lints, so a new unreachable
  `pub` or an unargued cast fails the build rather than warning.
- `xrpl-wasm-vm`'s tests come in two kinds, and the split is forced rather than
  stylistic. A wasmi `Caller` exists only for the duration of a host call, so
  `read_borrowed` / `write_into` / `read_write` / `memory` **cannot be reached from a
  unit test**. The unit tests in `src/` therefore cover only what needs no live
  instance (the wire conversions, the transfer-budget arithmetic, the limits), and the
  guest-memory policy is covered by integration tests in `tests/`, which run real
  modules against a configurable fake host.
- Those integration tests write their modules as **WAT text** and assemble it
  themselves — `wat` is a plain `[dev-dependencies]` entry and `support::assemble` is the
  only caller, so the assembler never enters the library. `run` takes binaries; there is
  no `run_wat` and no cargo feature for one. What makes that hold is `wasmi = {
  default-features = false, features = ["std"] }`: wasmi's `wat` feature is **on by
  default** and makes `Module::new` accept text as readily as binary (finding A5), which
  would put the text assembler in the consensus path and make a transaction's validity a
  build flag. `the_vm_refuses_a_text_format_module` in `vm_limits.rs` is what catches
  that feature coming back.
- `tests/support/mod.rs` holds the fake host and the import declarations. `Answer`
  separates *what the host writes* from *what length it reports*, which is what makes
  the over-cap and buffer-fit rules testable without values that large existing.
- Guest-linkability of the ABI crate (needs `rustup target add wasm32-unknown-unknown`):
  `cargo check -p xrpl-host-functions --target wasm32-unknown-unknown`. Worth keeping
  green — the guest stdlib links this crate, so a `std`/`alloc`/dependency creep here
  breaks it there. Only the ABI crate: `xrpl-wasm-vm` is host-side and pulls in wasmi.
- Full C++↔Rust: normal CMake build, then `xrpl_tests` (`src/test/app/Wasm_test.cpp`,
  `HostFuncImpl_test.cpp`).
- VCS is **jj** (`jj st`, `jj log`), not raw git, for local work.

## Current state (2026-07-30)

**`crates/` compiles**, and the whole workspace is green — `cargo test --workspace`,
`clippy --workspace --all-targets`, `fmt`, and `cargo doc -p xrpl-wasm-vm --no-deps`
(which `deny(rustdoc::broken_intra_doc_links)` now makes load-bearing). 123 tests: 33
macro, 12 facade, 1 doctest, and **77 in `xrpl-wasm-vm`** (10 unit; 67 integration — 12
`host_calls`, 21 `memory_policy`, 13 `budgets`, 21 `vm_limits`).

**Thirteen of the seventeen findings are closed** (2026-07-30): all of section A, all of
B, D13–D15, two thirds of D16, and C10. What is left is **C11**, blocked on the
scratch-buffer decision; **C12**, which the bridge will force anyway; **D16's `gas = 0`**,
a TER decision; and **D17**, which is not work.

`run` is `Result<RunOutcome, RunFailure>` over a typed `RunError`; host-fatal errors trap
instead of answering the guest a code; the import module is `host_lib`; the `i64` pipeline
and `AbiRet` are gone; the transfer budget counts only bytes actually copied host→guest,
and no more than the field cap can reach guest memory; the guest's linear memory is
resolved once, by kind rather than by name. See those entries for what landed and why.

**Three decisions were taken along the way**, each recorded at its finding:
**`OutOfTransferLimit` stays soft** (A1), **the import module name is `host_lib`** (A3),
and **the memory export is matched by kind, not by name** (section A's addendum). All
three restore C++ behaviour that the rewrite had changed without meaning to — which is
the pattern worth carrying into the bridge: on this path, "tidier than C++" is usually
"different from C++".

Every test that existed only to pin behaviour a finding said should change is gone,
replaced by a test of the new behaviour: `a_host_call_refused_its_gas_stops_the_run`
and `an_endless_loop_is_stopped_by_gas` for A1, `reads_do_not_spend_the_transfer_budget`
and `an_over_cap_value_is_refused_without_reaching_guest_memory` for A4.
`the_wire_conversion_truncates` went with the cast it pinned. What the work turned up
that reading the code did not:

- **An endless guest loop and a refused host charge are the same outcome**, and both
  report the whole limit as spent — the loop because wasmi's meter reaches zero, the
  refused charge because `charge` spends what is left before it fails, which is what
  makes C++'s "reported cost is the full limit" fall out rather than be arranged.
- **A start section is guest code, so the stage is not the reason.** Gas exhausted
  during `instantiate_and_start` first reported `Instantiate`, hiding a
  `tecOUT_OF_GAS`, because every error from that call was named after the stage.
  `guest_halted` runs at both stages now, and
  `a_start_section_that_exhausts_gas_is_out_of_gas_not_an_instantiation_failure`
  pins it. `as_trap_code()` is what makes this work at all: it reports
  `TrapCode::OutOfFuel` for whichever of wasmi's several error kinds carried the
  exhaustion (`error.rs:236-252`).
- **The compile-time guarantee on the fatal set is narrower than it looks.**
  `vm::host_fatal` is exhaustive over `HostError`, so a variant *added to the ABI*
  cannot compile until it is placed. But moving an *existing* variant into
  `abi::is_fatal`'s set is not caught: it falls into the grouped soft arm and
  reports `Internal`. The two lists are read together, and the doc comment says so.
- `NoMemExported` being fatal makes C10 (resolve the `"memory"` export once at
  instantiation) a move rather than a behaviour change — its failure is already a
  run-ender, so hoisting it to an instantiation-time `RunError::NoMemory` only
  changes which stage reports it.
- **A clamp and a check that look redundant are not.** See A4: the clamp bounds the
  bytes, the `MAX_FIELD_BYTES` check bounds the status. The finding's own text claimed
  the clamp made the check unreachable; a mutation proved otherwise, which is the
  argument for mutating rather than reasoning about a test's value.
- **Nothing in the suite reached the budget through `sha512_half`.** Removing
  `read_write`'s input charge would have been invisible, so
  `only_the_output_half_of_a_read_write_spends_the_budget` was written to catch it —
  2048 calls hashing twice the budget while writing a sixteenth of it. A finding whose
  fix no test can notice is a finding with no net under it.

**How the suite was checked.** A code review of the diff mutation-tested it, and the
result is worth recording because it found a test that pinned nothing: the multi-value
row of the old `the_disabled_proposals_do_not_compile` left a stray value on the wasm
stack, so the module was refused as a *type error* rather than for the proposal, and
`config.wasm_multi_value(false)` could be deleted with the whole suite still green. The
general lesson — a stage-only `assert_stage(…, "compile")` cannot tell "refused for the
reason under test" from "my wasm was malformed" — is now the design of
`every_disabled_feature_is_refused_by_name`: one row per disabled feature, each
asserting the *fragment of wasmi's message that names the feature*. Verified by deleting
each knob in turn: ten of twelve rows fail when their knob goes. The two that don't are
`wasm_custom_page_sizes` and `wasm_wide_arithmetic`, which wasmi 1.1 already defaults to
off (`engine/config.rs:72,74`), so those calls are redundant and no test can notice them
going — their rows guard against wasmi changing that default instead.

Three knobs have no module of their own, and `the_knobs_without_a_module_of_their_own`
records why rather than leaving it to a comment: `wasm_saturating_float_to_int` is masked
by `floats(false)` (every saturating conversion takes a float operand, and the test
asserts the message proves which knob answered); `ignore_custom_sections` is not
observable through accept/reject at all, since a module carrying a custom section
compiles either way; `consume_fuel` is covered by construction, because with it off
`Store::set_fuel` fails and every test in the suite breaks.

The other findings acted on: `MAX_MEMORY_PAGES` had **no** golden pin (it could be halved
with nothing failing), so `the_limits_are_the_protocol_limits` in `vm.rs` now pins all
four limits against their `Protocol.h` names, and the misnamed
`the_field_cap_is_far_below_the_run_budget` became the inequality its name promised.
`generated_abi.rs`'s "one place for literals" claim was false twice in its own file — the
subsumed name list and a restated `500` are gone. And `Answer::claiming` writes nothing,
which hid finding A4's actual hazard: `an_over_cap_value_is_written_before_it_is_refused`
now uses a real over-cap value and shows the bytes reaching guest memory before the
refusal. Verified by applying the `min(cap, MAX_FIELD_BYTES)` clamp — the test flips, as
its comment says it should.

Those 65 are review finding 15, closed: the bounds / field-cap / buffer-fit / gas /
transfer policy now has a net under it, which is what the rest of the findings need
before they can be acted on. Writing them turned up things reading the code did not:

- **wasmi parses WAT by default** (finding A5), so the VM compiled text-format modules
  straight from a transaction blob. The C++ path did not — its C-API took wasmi with
  `default-features = false` — so this was an accidental behaviour change, not a choice.
  Fixed, and back at parity.
- `wasm_mutable_global(false)` does **not** forbid a guest's own mutable globals — the
  proposal is about mutable globals crossing the module boundary. An internal one is
  core wasm and still compiles.
- A declared memory *maximum* above the 128-page cap instantiates fine; only the size
  actually reached is capped. And growth past the cap **traps** rather than answering
  `-1`, because the limiter is built with `trap_on_grow_failure(true)`.
- The two directions check in opposite orders, observably: an over-long *input* reports
  `DataFieldTooLarge` (the cap precedes the bounds check) while an over-long *output*
  reports `PointerOutOfBounds` (bounds precede the cap).
- wasmi's guest-side fuel for a host call is exactly `14 × operands + 1` (29/43/57/71
  for 2/3/4/5 operands, across all five functions; operand *type* is irrelevant — an
  `i64` costs what an `i32` does). With that and the 30-fuel empty-module floor known, a
  one-call run's total is known to the unit, so `a_host_call_costs_its_gas_every_time_it_is_called`
  asserts each function's charge directly rather than by differencing.

**Where the gas numbers live.** `the_spec_table_matches_the_declarations` in the ABI
crate's `generated_abi.rs` is the **one** place wire names and gas costs appear as
literals, as a whole-table comparison — a deliberate change-detector on consensus input,
which also pins `ALL`'s order and membership. Everything else reads
`HostFunctionSpec::gas()`. That split matters because the two properties are different:
*what the table says* is the ABI crate's business, while *whether the engine charges the
row the table holds* is the VM's. Verified by mutating `#[gas = 70]` to `71` — exactly
one test fails, the table one, and the VM's fuel tests follow the new value. Before the
split the VM restated all five values, so a legitimate gas change meant editing three
files. (Corollary: `every_variant_appears_in_all_exactly_once` is now subsumed by the
table comparison and could go.)

No test now **pins behaviour a finding says should change**. The two that did were
rewritten when their findings landed, which is what they were for.

The trait is settled, and every part of it is written in the declaration rather than
synthesized: `&self`, `HostResult<T>`, and byte outputs as explicit
`out: &mut [u8]` parameters. The macro checks the first two. Nothing is appended to a
signature behind the reader's back, which is what the PoC's `host_abi!` did — see the
lowering table above and "Open: where the output region points".

Consequences worth remembering:
- Declaring the out-params is what made the VM compile *unchanged* — `write_into` and
  `read_write` already took `FnOnce(&dyn HostFunctions, …, &mut [u8]) -> HostResult<usize>`.
- The ABI crate is now guest-linkable (`no_std`, no allocator, no runtime deps, checks
  for `wasm32-unknown-unknown`) — see "The ABI crate is a library both sides link".

Next, from the findings above, **C11 and C12 are all that remain**, and neither is
ordinary work. **C11** is blocked on the scratch-buffer decision — see "Open: where the
output region points", and read its amendment first, because A4 spent that section's
strongest argument. **C12** (per-run `Linker`, no module cache) stays last:
`VmState<'h>`'s lifetime forces `Linker<VmState<'h>>` to be per-run, so it is a design
change rather than a tweak, and the cxx bridge will force that lifetime question anyway.
Of the seventeen findings, thirteen are closed; the other two open items are D16's
`gas = 0`, a TER decision, and D17, which is not work.

The real remaining work is not in the findings list: **the cxx bridge**
(`xrpl-wasm-vm-ffi` is still `mod ffi {}`) and **real `ApplyContext` wiring**. A1 and A2
were sequenced first so the bridge has a typed `RunError` and a `fuel_used` to marshal
instead of error text to parse. D16's `gas = 0` decision belongs there too, since it is
a TER choice. Deferred as before: macro-emitted `link_*` shims, the generated C header,
the probe-module test.

## Once the crate is finished: cut the comments back

**`xrpl-wasm-vm`'s comments are too verbose, and they should be edited down in one pass
once the crate stops moving.** Do not do it while findings are still landing — several of
them turned on a rationale that only existed in a comment, and losing those mid-flight
costs more than the reading time.

Why they got this way is worth knowing, because it tells you what to keep. Each finding
was argued out in its doc comment as it landed: why a rule exists, which C++ line it
mirrors, why the obvious simplification is wrong. That was the right thing to write at the
time — the review found real bugs precisely where the code had asserted something no
comment justified — but the accumulation now reads as an essay per function. `write_into`
and `VmState::memory` are the clearest cases.

What the pass should keep, roughly in order of value:

- **The C++ reference points.** `WasmiVM.cpp:224-249`, `HostFuncWrapper.cpp:497`,
  `Protocol.h`'s names. These are consensus parity evidence and cannot be recovered from
  the code.
- **Why an apparent redundancy is not one.** The `n > MAX_FIELD_BYTES` check beside the
  clamp; `is_fatal` and `host_fatal` being two lists; `MUST_TRAP` restating the fatal set
  rather than deriving it. Every one of these has been "simplified" wrongly at least once
  in a mutation test, so each earns its sentence.
- **Load-bearing invariants**, like `VmState::memory`'s one-instance-per-run assumption.

What it should cut:

- Prose restating what the next line plainly does.
- The same rationale on a field and on the function that sets it — pick the one a reader
  reaches first. `WasmiVM.cpp:224-249` is currently cited twice for two different facts.
- Paragraphs duplicating this document. A pointer here beats a retelling in `abi.rs`.
- The worked examples that have served their purpose, where a sentence now does.

A rule of thumb that fits what actually paid off: a comment should say something the
compiler cannot check and the code cannot show. Everything else is a candidate.

One incidental constraint found while checking the guest target: `crates/hello_world`
cannot be checked for `wasm32-unknown-unknown` — it depends on `cxx` →
`link-cplusplus`, which wants a C++ toolchain for the target. Pre-existing, but it means
the guest-linkability check has to name the ABI crate rather than being a blanket
workspace command.

Four things the slices turned up rather than the original read went into that pass, and
one of them is worth more than its size:

- `register_host_functions` now returns `Result<(), wasmi::errors::LinkerError>`; its
  `format!` was dead once `run` began discarding the string.
- **`HostError::ALL` exists, and how it had to be built is the interesting part.**
  `only_the_host_fatal_errors_trap` checked a hand-listed sample, so a variant added to
  the ABI was not covered. The obvious fix — a wildcard-free `match`, the trick
  `vm::host_fatal` uses — **cannot close this**, and the reason generalizes: an
  exhaustive `match` forces you to *write an arm*, but checking "every variant is in
  `ALL`" requires *enumerating* variants, and Rust has no stable way to do that
  (`mem::variant_count` is unstable). Every const-assertion scheme over `ALL` is beaten
  by "add the variant, give its arm a value, leave `ALL`
  alone", because the assertion only ever iterates `ALL` — the very thing missing the
  variant. So the airtight mechanism is a **single declaration site**: a `host_errors!`
  macro emits the enum, `ALL` and `from_code` from one list of codes.
  `HostFunctionSpec::ALL` is complete for exactly the same reason. It also retired a
  hand-duplicated 23-arm `from_code` table that nothing tested; `tests/host_errors.rs`
  now pins the 23 wire codes as literals, which is where a consensus-visible number
  belongs.
- With `ALL` in hand, `every_fatal_error_has_an_outcome_of_its_own` closes the
  `is_fatal`/`host_fatal` coupling gap the other direction — an existing variant moved
  into `is_fatal` without `host_fatal` gaining an arm now fails a test instead of
  silently reporting `Internal`. (Its wrinkle: `RunError::Internal` is both the soft
  arm's answer and `HostError::Internal`'s own, so the test asks about that one by
  name.)
- The generated `HostFunctions` trait now carries an output contract: a host writes into
  `out` only when the whole value fits, and returns the value's true length either way.
  That is what makes A4's "a refused value leaves nothing behind" hold end to end,
  since `write_into` can only bound what is *writable*.

Deferred to a later refactor, once there is working code: macro-emitted `link_*`
shims, the generated C header, and the probe-module conformance test.

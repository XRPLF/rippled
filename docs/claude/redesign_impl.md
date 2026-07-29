# rippled fork — Rust WASM VM work

## What this branch is doing

We are on `Wasm-vm-redesign`: replacing the C++ wasmi **C-API** integration with a
Rust wasmi wrapper, written as **refined, production-ready code** built on the ideas
of the PoC — not a cleanup pass over the PoC itself.

`Rust_wasm_PoC` (and `Rust_wasm_PoC_benchmark`) are **reference branches**: the PoC
lives there, read-only, to be consulted for approach and prior art. Code copied
across from it is a starting point, not a baseline to preserve — the PoC's shapes,
comments and trade-offs are all open for redesign here.

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

    **Convention: the macro emits what varies per declaration; the facade
    hand-writes the invariants and the macro refers to them by absolute path.**
    So `HostFunctions`, `HostFunctionSpec` and its spec table are generated, while
    `HostError`, `HostResult` and `HostFnSpec` are hand-written (greppable,
    documented, testable, one rustdoc page). `host_fn_spec_path()` in the macro is
    the single place that path is spelled; `extern crate self as
    xrpl_host_functions;` in the facade is what makes it resolve inside the crate
    the ABI is declared in. Never emit a bare type name — an absolute path is what
    keeps the expansion independent of what the call site imported.

    The macro crate dev-depends on the facade so its doctest compiles; cargo allows
    that cycle because dev-dependencies sit outside the library build graph.
  - `crates/xrpl-wasm-vm/` — the wasmi wrapper: `vm.rs` (engine/store/run),
    `abi.rs` (gas + transfer-limit + guest-memory marshaling), `register.rs`
    (hand-written `Linker::func_wrap` per host function).
  - `crates/xrpl-wasm-vm-ffi/` — cxx bridge to C++. **Still empty** (`mod ffi {}`);
    nothing is wired to C++ yet.
- `include/xrpl/tx/wasm/`, `src/libxrpl/tx/wasm/` — C++ side: `HostFunc.h` (the
  ~60-method `HostFunctions` interface the ledger implements), `HostFuncImpl*.cpp`
  (its implementations), `WasmCommon.h` (`HostFunctionError`, `Wmem`, `WasmTER`,
  `FieldLocator`), `README.md` (ABI docs, worth reading — but stale in places, see below).

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
params:
  &self              -> nothing                   (receiver, not part of the ABI)
  i32, bool          -> i32                       (bool: nonzero = true)
  i64                -> i64
  &[u8], &str        -> i32 ptr, i32 len          const uint8_t*, int32_t

returns:
  [u8; N], Vec<u8>   -> appends i32 out_ptr, i32 out_len; result i32 = bytes written
  i32, bool          -> no out params; result i32 = the value
  ()                 -> no out params; result i32 = 0
```

Total and unambiguous. **The macro must reject any type not in this table** — that is
`WasmImpArgs`' `static_assert`, restored, and it is what guarantees the C API is
always surfaceable.

**Validation** — all five current declarations (`xrpl-host-functions/src/lib.rs:87-107`)
lower to exactly the deleted C++ `_proto` aliases:

| Declaration | Derived C | C++ `_proto` |
|---|---|---|
| `fn get_ledger_sqn() -> [u8; 4]` | `int32_t(uint8_t*, int32_t)` | `getLedgerSqn_proto` ✓ |
| `fn get_current_ledger_obj_field(field: i32) -> Vec<u8>` | `int32_t(int32_t, uint8_t*, int32_t)` | `getTxField_proto` ✓ |
| `fn sha512_half(data: &[u8]) -> [u8; 32]` | `int32_t(const uint8_t*, int32_t, uint8_t*, int32_t)` | ✓ |
| `fn trace(msg: &str, data: &[u8], as_hex: bool)` | `int32_t(const uint8_t*, int32_t, const uint8_t*, int32_t, int32_t)` | `trace_proto` ✓ |
| `fn trace_num(msg: &str, number: i64)` | `int32_t(const uint8_t*, int32_t, int64_t)` | `traceNum_proto` ✓ |

**Discipline the table requires**: byte outputs must be spelled as arrays.
`get_ledger_sqn` is correctly `-> [u8; 4]` (C++ writes 4 LE bytes and returns 4 — it
does *not* return the sequence number). By the same rule `float_to_int` must be
declared `-> [u8; 8]`, never `-> i64`. A scalar return type means value-in-the-return-
register (`get_tx_array_len(field: i32) -> i32`, `nft_flags`, `float_cmp`, `cache_le`,
`check_sig`, `amendment_enabled`).

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

### Mechanism note: why the PoC's value-returning trait is the right shape

Generated or type-checked registration needs one uniform phase order:

> lift inputs with `&Caller` → call the host → lower outputs with `&mut Caller`

The uncommitted `write_into` / `HostResult<usize>` fill-the-guest-buffer code fights
this: it hands the host impl a `&mut [u8]` **into guest memory** while inputs also
alias guest memory. That is why `read_write` has to memcpy inputs into a
`[0u8; MAX_WASM_DATA_LEN]` stack array first — and that workaround does not
generalize, because `credential_keylet`, `check_sig` and `paychan_keylet` each take
three byte inputs.

The fix is for the host to write into a **host-side scratch buffer** that the dispatch
adapter owns, with a single copy into guest memory afterwards. Not guest memory → no
aliasing → no scratch-per-input. This is what C++ did (`std::expected<Bytes, …>` +
`setData`), so gas/behaviour parity is preserved, and it is essentially the PoC's
original value-returning trait plus `HostResult<T>` for the error channel.

Cost is roughly a wash, not a straight loss of the zero-extra-copy work:
- `sha512_half` — today: ≤1 KiB input copied to stack + 32 bytes written ≈ 1056 bytes
  moved. New: input borrowed zero-copy, 32 bytes copied out. **Better.**
- `get_tx_field` (no byte input, ≤1 KiB output) — today 1024 direct; new 1024 + 1024.
  **Worse.**

It also fixes a real wart: `write_into` checks `n > cap` *after* `fill` has already
written, so a rejected call leaves garbage in the guest buffer. C++ `setData` checked
before the memcpy.

### Status: deferred

**Not a blocker.** Get the VM compiling and working first; the typed shims, generated
header and probe-module test are a follow-up refactor once there is working code.

## Open ABI questions and interop risks (2026-07-29)

Found while auditing the guest SDK (`~/Documents/rust/xrpl-wasm-stdlib`, checkout
`435a091f`) against this fork. All unresolved.

1. **Import module name.** The old C++ VM ignored it entirely —
   `wasm_importtype_module()` is commented out at `src/libxrpl/tx/wasm/WasmiVM.cpp:429-431`
   and only the field name is looked up. `register.rs:8` now enforces `"host"`. The SDK
   and the fork's own fixture (`src/test/app/wasm_fixtures/codecov_tests/src/host_bindings_loose.rs:20`)
   use `"host_lib"`. Plain clang emits `"env"` unless annotated. `"host"` currently
   matches nothing that exists.
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
- Full C++↔Rust: normal CMake build, then `xrpl_tests` (`src/test/app/Wasm_test.cpp`,
  `HostFuncImpl_test.cpp`).
- VCS is **jj** (`jj st`, `jj log`), not raw git, for local work.

## Current state (2026-07-29)

`crates/` does **not** compile: the macro-generated trait (value-returning,
infallible) and the VM code (fill-caller's-buffer, `HostResult<usize>`) are two
different ABI shapes. The 8 errors are all in `xrpl-wasm-vm` — every byte-returning
call site in `register.rs`, plus `?` on the infallible `trace`/`trace_num`.

`xrpl-host-functions` and `xrpl-host-functions-macros` are green (`cargo test` +
`clippy`): 28 macro tests, 5 ABI tests, 1 doctest.

Resolving that shape is the immediate work. Per the mechanism note above, the
value-returning direction (plus `HostResult<T>`) is the one that composes with
typed/generated registration; the fill-the-guest-buffer shape is what fights it.

Deferred to a later refactor, once there is working code: macro-emitted `link_*`
shims, the generated C header, and the probe-module conformance test.

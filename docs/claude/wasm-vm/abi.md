[← Rust WASM VM docs](index.md)

# The ABI: one declaration, three outputs

`crates/xrpl-host-functions/` is the one declaration. C compatibility adds a **third
output** beside the trait and the spec enum — a *generated, checked-in* C header with a CI
diff — not a second input. "Explicit vs hidden" is the wrong axis; "derivable and emitted"
is the right one, because C authors read a header, not a macro.

**Adding a host function** is one `host_functions!` entry plus one arm in
`register_host_functions`'s exhaustive `match` (a new `HostFunctionSpec` variant will not
compile until it is registered), one `MOCK_METHOD` in `MockHostFunctions`, and one method on
`HostContext` if C++ is to serve it.

## The lowering table

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

## Closing the drift gap to `register.rs`

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

## The ABI crate is a library both sides link

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
and decodes the `i32` through `HostError::from_code`, which range-checks — unlike the SDK's
bare transmute, which is "new error codes are UB in the guest" in
[open-questions.md](open-questions.md). One trait serves both *because the declaration is now
the wire shape*.

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

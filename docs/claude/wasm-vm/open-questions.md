[← Rust WASM VM docs](index.md)

# Open questions and deferred work

## Resolved, recorded so they are not reopened

**`gas = 0` — refused in C++ as `temBAD_AMOUNT`.** The old code already decided this and an
earlier revision of these docs had it garbled: `WasmiEngine::run` rejected `gas <= 0` for
*every* value including `-1`, and `-1 = unlimited` applied only to preflight's `check`.
`runEscrowWasm` restores exactly that, which is why the engine's own budget stays a `u64` with
no invalid value to represent.

**Import module name — `host_lib`**, matching the SDK and this fork's Rust fixtures.
`the_import_module_name_must_match` rejects `host`, `env` and the empty name.

**`-1` collides semantically** — host `Unimplemented` vs Rust `Internal`. Now a decision
rather than an accident: the bridge treats them as one condition, "the host could not serve
this call, and the contract has no business interpreting why". Both are host-fatal, so both
stop the run and report `tecINTERNAL`, which is also what a C++ exception caught in
`HostContext::guarded` becomes. The guest-side half of the collision (its own `InternalError`)
is untouched.

## ABI / guest-SDK interop

Found auditing the guest SDK (`~/Documents/rust/xrpl-wasm-stdlib`, checkout `435a091f`)
against this fork. All are decisions rather than code.

1. **Import name lineage.** The fixtures pin the SDK at `branch = renames` and use **short**
   wire names (`parent_ldgr_hash`, `cache_le`, `tx_inner_arr_len`, `accountroot_id`,
   `trustline_id`), matching `ldgr_index` / `home_le_field` / `sha512_half`. The standalone
   SDK checkout is the **long**-name lineage (`get_parent_ledger_hash`, `cache_ledger_obj`,
   `compute_sha512_half`). Which is authoritative is undecided.
2. **New error codes are UB in the guest.** The SDK decodes with a bare transmute and no
   range check (`xrpl-common-stdlib/src/host/mod.rs:325`), valid only for `-1..=-20`. Making
   host-fatal errors trap (A1) removed `OutOfGas` from the guest's view, and the
   soft/fatal question is settled — **`OutOfTransferLimit` stays soft**. The *encoding*
   question is not: `OutOfTransferLimit = -23` still reaches a transmuting guest, and
   `NoRuntime = -21` would if anything returned it. Closing it needs either a range check in
   the SDK or a remap into `-1..=-20`.
3. **The two error enums have already drifted.** C++ `HostFunctionError` spells -11
   `OutOfTransferLimit`; the Rust ABI spells it `Decoding`. `WasmVMTest.SoftHostErrorCodes-
   CrossUnchanged` now walks every soft code so a further renumbering is caught, but the
   divergence itself is unresolved — one of the two lists is wrong.
4. **`float_to_mant_exp` byte count.** The host returns **12** (8 mantissa + 4 exponent);
   the guest doc says 8, and the guest's `match_result_code_with_expected_bytes` **panics**
   on a non-negative mismatch. Note this function writes *two* output regions, a shape no
   current helper serves.
5. **Return conventions are not uniform** — six of them: bytes-written; value-in-return
   (`*_arr_len`, `nft_flags`); boolean 0/1 (`amendment_enabled`, `check_sig`); 1-based handle
   (`cache_le`, always ≥ 1); status-0 (`trace*`, `set_data`); tri-state (`float_cmp` — `0`
   equal, `1` first >, `2` second >).
6. **The SDK's drift checker is silently broken.** `tools/compareHostFunctions.js`
   regex-parses `WasmVM.cpp` and `HostFuncWrapper.h`, both deleted. A generated C header
   would give it a stable target again ([abi.md](abi.md)).

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
     is handing `run` a pre-compiled module, which changes the signature the bridge now
     consumes. Either way the answer comes from the caller side.
   - *Per-run `Linker`* is blocked: `VmState<'h>`'s lifetime forces `Linker<VmState<'h>>` to
     be per-run, so hoisting it means making the store data `'static` — not holding
     `&'h dyn HostFunctions`. This was expected to fall out of the bridge; **it did not.**
     `CxxHost<'a>` borrows the C++ `HostContext` for one run and coerces to
     `&'h dyn HostFunctions` unchanged, so hoisting the linker is still its own piece of
     work with no other reason to do it.

So: **measure first**, and treat the linker and the lazy buffer as whatever the numbers say.
The module cache's open question is unchanged by the bridge — `run(wasm, gas, host, name)` is
now a signature the C++ side consumes, so handing it a pre-compiled module is a change to a
live interface rather than a hypothetical one, and **who owns a compiled contract's lifetime**
is still the caller's question to answer.

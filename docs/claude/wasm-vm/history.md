[← Rust WASM VM docs](index.md)

# History: review findings, and the deleted C++ path

## Review findings (2026-07-29)

A read of `vm.rs`, `abi.rs` and `register.rs` against the vendored wasmi 1.1.0. **Seventeen
of the eighteen are closed, C12 the only one left** — earlier revisions of these docs said
"fourteen of seventeen", which never matched the table. The rationale that is still
load-bearing has moved into [engine.md](engine.md).

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
| C12 | `Linker` rebuilt per run; module compiled per run with no cache | **open — [open-questions.md](open-questions.md)** |
| D13 | The public surface was accidental (`RunOutcome` unnameable, limits unreachable) | ✓ exported; `MAX_FIELD_BYTES` renamed out of `abi.rs` |
| D14 | `abi.rs` *claimed* every access was a checked slice op | ✓ `forbid(unsafe_code)` + cast lints enforce it |
| D15 | Zero tests | ✓ 79 in `xrpl-wasm-vm` |
| D16 | `gas = 0` accepted silently; `get_fuel().unwrap_or(0)` reported the whole limit; entry-point diagnostic wrong for a wrong-signature export | ✓ closed — `gas <= 0` is `temBAD_AMOUNT` in `runEscrowWasm` |
| D17 | The start-section TODO reads like a hole | ✓ documented as not closeable with wasmi 1.1 |

Three of the closed findings were **behaviour changes nobody had chosen** — A3, A5, A6 — and
all three restored C++ behaviour the rewrite had altered by accident. That is the pattern
worth carrying forward: on this path, "tidier than C++" is usually "different from
C++". A fourth decision, C11's buffer, extends it rather than restoring it.

## Reference points from the deleted C++ path

Import names and gas costs are ABI. The rest is *evidence of prior behaviour* — useful for
comparison and for the gas assertions in `Wasm_test.cpp`, not gospel. All recoverable with
`git show b7059deb9f^:<path>` — note that `WasmVM.{h,cpp}` exist again at that path with
entirely different contents, so the revision in that command is doing real work.

Deleted later, with the dead `wasmi::wasmi` link that was the only thing supplying their
`<wasm.h>`: `include/xrpl/tx/wasm/HostFuncWrapper.h` (the `*_proto` aliases and `*_wrap`
declarations, whose `.cpp` went in `b7059deb9f`) and `WasmImportsHelper.h` (`ImportVec`,
`WasmImpArgs`'s `static_assert`). Every remaining reference to either was inside a
commented-out file. The `_proto` aliases are the C lowering the table in [abi.md](abi.md)
reproduces, so they are worth reading before extending it.

- **Import names + per-call gas**: `src/libxrpl/tx/wasm/WasmVM.cpp`
  (`setCommonHostFunctions`, 64 entries plus `set_data` registered only in
  `createWasmImport`; e.g. `ldgr_index` 60, `sha512_half` 2000, `set_data` 1000, `float_pow`
  5'500).
- **Guest-visible error codes**: `HostFunctionError` in `include/xrpl/tx/wasm/WasmCommon.h`
  (-1 `Unimplemented` … -20 `FloatComputationError`; note **-11 is `OutOfTransferLimit`**
  there, `InvalidDecoding` in the SDK; see "the two error enums have already drifted" in
  [open-questions.md](open-questions.md)).
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

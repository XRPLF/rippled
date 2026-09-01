# WASM host-function tests — layering

These tests are deliberately **layered**: each layer isolates one thing, so a failure points at
one place instead of "somewhere in the stack." If a folder looks thin, the breadth it seems to be
missing lives in a sibling layer.

## The layers

| Layer                                 | Location                                            | host | VM  | ledger | Answers                                                                             |
| ------------------------------------- | --------------------------------------------------- | ---- | --- | ------ | ----------------------------------------------------------------------------------- |
| Engine / gas / limits / ABI           | `crates/xrpl-wasm-vm`, `crates/xrpl-host-functions` | mock | ✓   | ✗      | gas, transfer budget, memory/field limits, preflight, VM limits, generated ABI      |
| `host_context/` (`HostContextTest`)   | `.../host_context`                                  | mock | ✗   | ✗      | the `HostContext` marshalling shim alone (byte order, buffer sizing, `SField` xlat) |
| `host_calls/` (`HostCallTest`)        | `.../host_calls`                                    | mock | ✓   | ✗      | per-function **wire contract** — what the host was asked, what came back            |
| `host_functions/` (`RealHostFixture`) | `.../host_functions`                                | real | ✗   | real   | each function's **actual answer** vs. a real `TxTest` ledger                        |
| `e2e/` (`RealVmTest`)                 | `.../e2e`                                           | real | ✓   | real   | **full-stack integration** — VM + `HostContext` + real impl + real ledger           |

Run the C++ side with:

```bash
./build/xrpl_tests --gtest_filter='*Impl.*:*Call.*:*E2e.*:WasmVMTest.*:WasmVMDeathTest.*:PreflightTest.*'
```

(720 tests, 138 suites.) The engine-level coverage is Rust: `cd crates && cargo test`.

## `fixtures/` — split by whether it needs a test framework

|                                                |                                                                                                                                                                                                             |
| ---------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **No GTest** — the `xrpl.testkit.wasm` library | `WasmLedger` (real genesis ledger + the real host over it), `WasmRun` (WAT assembler), `NftSetup`, `FloatConstants`                                                                                         |
| **GTest** → `xrpl_tests`                       | `RealHostFixture` (`: testing::Test, WasmLedger` + `expectValue`/`expectError`/`expectKeyletMatches`), `FloatFixture`, `NFTFixture`, `MockHostFunctions`, `WasmFixture`, `RealVmTest`, `HostContextFixture` |
| **Benchmark harness** → `xrpl.bench.wasm`      | `WasmBench`, `BenchFixtures`                                                                                                                                                                                |

A benchmark wants a ledger and a host, not GTest's lifecycle. Both binaries link the library;
`xrpl.bench.wasm` links no GTest and no GMock at all.

Setup steps in `WasmLedger` and `NftSetup` **throw** (`fixtureFailed`) rather than using `EXPECT_`.
Not stylistic: an `EXPECT_` outside a running test is recorded and discarded, so a benchmark whose
escrow was never created would still run its host call, take the not-found path, and report a
cheap, plausible, completely wrong price. **If you add a setup step that can fail, throw.**

## `*.bench.cpp` — gas calibration

Not tests: nothing asserts, and a number moving is not a build failure. They answer the question
the tests cannot — whether each `#[gas = N]` in `crates/xrpl-host-functions/src/lib.rs` matches
what the function costs.

One `.bench.cpp` per host function, named after its test, so adding a function is a two-file
checklist rather than a judgement call. Shared ledger setup is the `Fixtures` type — one ledger,
funded once, for the whole binary.

```bash
cmake --build build --target xrpl.bench.wasm
./build/xrpl.bench.wasm # everything (~2 min)
./build/xrpl.bench.wasm --benchmark_filter=sha512Half
```

**Build Release first.** Debug inflates the crossing far more than the impls. Ratios between
`Impl` cases survive Debug; `suggested_gas` does not.

### Reading the output

| Counter             | Meaning                                                                            |
| ------------------- | ---------------------------------------------------------------------------------- |
| `suggested_gas`     | **the answer** — what this function should be priced at                            |
| `host_function_gas` | what `lib.rs` says today, read through the `wasm_testkit` bridge so it can't drift |
| `price_ratio`       | `host_function_gas / suggested_gas`. **1.0 is correct; below 1 is underpriced**    |
| `implied_gas`       | the raw measurement, before the crossing is added back                             |
| `charged_gas`       | what the engine actually billed; confirms the right call was measured              |
| `ns_per_call`       | raw wall time, for debugging a suspicious ratio                                    |

`price_ratio` is what you sort by. **Below 1 is the direction that matters** — an underpriced call
is one a contract can buy too cheaply, a denial-of-service vector rather than a rounding error:

```bash
./build/xrpl.bench.wasm --benchmark_format=json |
    jq -r '.benchmarks[] | select(.price_ratio) | [.price_ratio, .name] | @tsv' | sort -n
```

### How `suggested_gas` is measured

A wall-clock number cannot be a gas number. The bridge is that **gas is wasmi fuel** —
`set_fuel(gas)` meters guest instructions and host charges from one pool — so one unit of gas is
about one guest instruction, and the question becomes a ratio: _how many guest instructions' worth
of work is this call?_ Every step is a subtraction, so fixed costs cancel:

```
secondsPerGas  = (time_busy − time_idle) / (fuel_busy − fuel_idle)   # a pure-wasm loop, N vs 0
implied_gas    = secondsPerCall / secondsPerGas
crossing_floor = (ldgr_index ThroughVm − ldgr_index Impl) / secondsPerGas

suggested_gas  = implied_gas                     # ThroughVm — the guest already paid the crossing
suggested_gas  = implied_gas + crossing_floor    # Impl — a guest cannot call without paying it
price_ratio    = host_function_gas / suggested_gas
```

`secondsPerCall` is itself a subtraction: a `ThroughVm` case runs a contract making N host calls
against a **byte-identical** one making none, so compilation, instantiation and the guest's own
loop cancel. It is reported per iteration, so Google Benchmark's variance statistics describe the
host call rather than the run containing it. An `Impl` case times `kCallsPerRun` direct calls and
divides.

**`guestInstruction` is the self-test, and it has a number.** It runs the same loop body the
calibration uses, so its `implied_gas` (wall time) and `charged_gas` (the engine's fuel meter) are
two measurements of one quantity. Release, quiet machine: **≈13.6 against 13.007, ~4% high with
~4% spread.** A persistent gap much beyond that is a harness bug — do not trust any other number
in the run until it is closed.

That check has already caught a real defect: calibration took a _best-of-N_ while the cases report
a _mean_, which biased every number +40%. Both are means now. **If you change how either side is
estimated, change both.**

**Two limits.** `suggested_gas` for an `Impl`-only case is a **lower bound** — the crossing floor
is measured on a call with no input, so a function that moves bytes pays more (the swept cases,
`Sha512Half` and `UpdateData`, measure that per-byte term). And `--benchmark_repetitions=N`
averages down noise but will not touch a systematic bias.

`ThroughVm` cases are deliberately **one per crossing shape, not one per function**: what the
crossing costs depends on a call's shape, not on which function makes it.

### Gotchas, each of which has already cost someone an afternoon

- **The wasm ABI is not the trait's argument order.** `float_add(x, y, mode, out)` in Rust is
  `(x_ptr, x_len, y_ptr, y_len, out_ptr, out_len, mode)` on the wire — scalars move _after_ the
  output region. Check `register.rs`, not `lib.rs`, when writing WAT.
- **A soft host error still "succeeds".** The run completes and gas is charged _before_ the body,
  so a wrong-argument case reports a plausible, confidently wrong number. The harness requires the
  contract's result to be `>= 0`; the tell is a `ThroughVm` case coming out _faster_ than its `Impl`.
- **A host serves exactly one run** (`checkSelf` in `WasmVM.cpp`), so a benchmark builds a fresh
  host per run and cannot pre-cache a slot.
- **`MAX_FIELD_BYTES` is 1024** — nothing crosses the boundary above 1 KiB, so size sweeps stop there.
- Cases pin `->Iterations(...)`: with `UseManualTime`, automatic sizing reads only the tiny reported
  residue and would ask for millions of iterations.

## What `e2e/` covers — the rule

**`e2e/` covers every marshalling shape and cross-call convention exactly once. It does not cover
every function.** That is a completeness claim on the axis e2e uniquely tests, not a sample.

`host_calls` pins what the bridge _asks_ with a _canned_ answer; `host_functions` pins what the
real impl _answers_. The type system guarantees they agree on signatures. Nothing guarantees they
agree on **conventions** — units, endianness, buffer layout — because in neither test does a real
guest write bytes a real host reads. That is exactly the `seq`-as-little-endian-region bug: every
internal test passed, and it was caught by cross-checking the guest SDK.

Convention mismatch is a property of a call's **shape**, not of the function. All 19 keylets share
one shape, so a 19th keylet e2e proves nothing the 1st did. The inventory is meant to be exhaustive:

| Shape / convention                        | Covered by                 | Why it is its own row                                    |
| ----------------------------------------- | -------------------------- | -------------------------------------------------------- |
| no-input scalar getter                    | `LedgerSqnE2e`             | header read; the minimal call                            |
| field code in, bytes out (ledger object)  | `CurrentLedgerObjFieldE2e` | `SField` translation over a real object                  |
| field code in, bytes out (transaction)    | `TxFieldE2e`               | a different source than a ledger object                  |
| region in, bytes out + `u32` region       | `CacheLedgerObjE2e`        | the 4-byte little-endian region convention               |
| slot in, bytes out — **cross-call state** | `CacheLedgerObjE2e`        | the slot table is the only host state outliving one call |
| locator (path of i32 steps)               | `TxNestedFieldE2e`         | a wire format the guest writes and the host walks        |
| **two** output regions                    | `FloatToMantExpE2e`        | two bounds checks, two writes, an ordering between them  |
| write / mutation                          | `SetDataE2e`               | the one thing a contract changes                         |
| **error** path from a real impl           | `HostErrorE2e`             | a soft code from a real failure, not a staged one        |
| realistic multi-call contract             | `HostFunctionTourE2e`      | the old `all_host_functions` tour shape, as one test     |

Adding a function needs no new e2e case unless it introduces a shape not in that table. Per-function
breadth lives in `host_functions/` and `host_calls/`, one case each.

## Out of scope

**The guest SDK** (`xrpl-std` / `xrpl-escrow`, external `xrpl-wasm-stdlib` repo) is not exercised
here — that is the SDK repo's own suite. These tests hand-write the ABI in WAT (raw imports,
literal field codes, hand-built byte layouts), deliberately bypassing all SDK code. Agreement is
verified _transitively_: the SDK repo tests the SDK against the ABI spec, this repo tests the host
against the same spec. That would not catch a drift where both diverge on an ambiguous point;
closing it needs a **cross-repo integration test** (compiled guests against a real host) in CI
where the Rust→wasm toolchain exists.

**Transactor-level (L5) tests** are deferred: the redesign does not yet wire `runEscrowWasm` into
the `EscrowFinish` transactor, so there is no caller under `src/xrpld`. When it is wired, these
need a home as C++ transactor tests over a real `Env` — `set_data` persistence (including on
`tecBYTECODE_REJECTED`), `sfGasUsed` / `sfVMReturnCode` in transaction metadata, and owner-reserve
accounting for a bytecode-bearing escrow. The layers here deliberately stop at the VM boundary.

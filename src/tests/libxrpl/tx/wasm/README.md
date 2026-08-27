# WASM host-function tests — layering

These tests are deliberately **layered**: each layer isolates one thing, so a failure points at
one place instead of "somewhere in the stack." If a folder looks thin, that is usually because
the breadth it might seem to be missing lives in a sibling layer. This file is the map.

## The layers

| Layer                                 | Location                                                                                    | host | VM  | ledger | Answers                                                                                                                                     |
| ------------------------------------- | ------------------------------------------------------------------------------------------- | ---- | --- | ------ | ------------------------------------------------------------------------------------------------------------------------------------------- |
| Engine / gas / limits / ABI           | `crates/xrpl-wasm-vm/tests`, `crates/xrpl-host-functions/tests` (Rust, `.wat` + `FakeHost`) | mock | ✓   | ✗      | gas, transfer budget, memory/field limits, preflight screening, VM limits, the generated ABI + error codes                                  |
| `host_context/` (`HostContextTest`)   | `src/tests/libxrpl/tx/wasm/host_context`                                                    | mock | ✗   | ✗      | the `HostContext` marshalling shim in isolation (byte order, buffer sizing, `SField` translation)                                           |
| `host_calls/` (`HostCallTest`)        | `.../host_calls`                                                                            | mock | ✓   | ✗      | per-host-function **wire contract** — what the host was asked, what came back — a real guest through the real VM + `HostContext`, mock host |
| `host_functions/` (`RealHostFixture`) | `.../host_functions`                                                                        | real | ✗   | real   | each host function's **actual answer** vs. real `TxTest` ledger state (impl called directly in C++)                                         |
| `e2e/` (`RealVmTest`)                 | `.../e2e`                                                                                   | real | ✓   | real   | **full-stack integration** — VM + `HostContext` + real impl + real ledger, driven by a WAT contract                                         |

`MockVmTest` / `RealVmTest` are the mock-host and real-host counterparts of the same VM harness;
both forward to the shared `runWat(HostFunctions&, ...)` in `WasmRun.h`, differing only in the
host they inject.

## `*.bench.cpp` — gas calibration

Interleaved with the tests are `*.bench.cpp` files. They are **not tests**: nothing asserts,
and a number moving is not a build failure. They answer the pricing question the tests cannot —
whether each `#[gas = N]` in `crates/xrpl-host-functions/src/lib.rs` matches what the function
actually costs.

**One `.bench.cpp` per host function, named after its test** — `EscrowKeylet.cpp` and
`EscrowKeylet.bench.cpp` sit next to each other, 61 of each. That is a checklist rather than a
judgment call: adding a host function means adding two files, and nobody has to decide where a
benchmark belongs. Shared ledger setup lives in the `Fixtures` type (`BenchFixtures.h`) — one
ledger, funded once, for the whole binary — so each file holds only the call it measures.

They build into a **separate executable** (`xrpl.bench.wasm`), and `xrpl_tests` filters
`*.bench.cpp` out of its source globs, so benchmark runtime never lands on the `ctest` path.

### Running them

The executable lands in the **build root**, beside `xrpl_tests`:

```bash
# configure with -o benchmark=True (the conan default), then:
cmake --build build --target xrpl.bench.wasm
./build/xrpl.bench.wasm # everything (~2 min)
./build/xrpl.bench.wasm --benchmark_filter=sha512Half
./build/xrpl.bench.wasm --benchmark_format=json >gas.json
```

**Build Release first.** A Debug build inflates the crossing (templates and `std::expected`,
none of it inlined) far more than it inflates the impls, so Debug overstates what leaving the
guest costs. Google Benchmark prints a warning when it detects this. Ratios between `Impl`
cases survive Debug reasonably; absolute `implied_gas` does not.

> **On hardware performance counters.** Google Benchmark can read instruction and cycle counts
> through libpfm on Linux, and that was wired up here at one point, but it has been removed: its
> counters start and stop around the whole `for (auto _ : state)` body, which for these cases
> covers the loaded run _and_ the baseline run _and_ two module compilations. They would not
> reflect the subtraction that makes these numbers mean anything. Getting useful instruction
> counts needs a custom `perf_event_open` around the same regions `timeRun` brackets — worth
> doing, but the dependency buys nothing until then.

### Reading the output

Gas _is_ wasmi fuel — `set_fuel(gas)` meters guest instructions and host charges from one pool —
so a host call's price is answerable as a **ratio**: how many guest instructions' worth of work
is it? That is machine-independent, which matters because a consensus rule cannot be derived
from one laptop's nanoseconds.

| Counter         | Meaning                                                                                      |
| --------------- | -------------------------------------------------------------------------------------------- |
| `suggested_gas` | **the answer** — what this function should be priced at                                      |
| `declared_gas`  | what `lib.rs` says today                                                                     |
| `price_ratio`   | `declared / suggested`. **1.0 is correct; below 1 is underpriced**                           |
| `implied_gas`   | the raw measurement, before the crossing is added back                                       |
| `charged_gas`   | what the engine actually billed (`EscrowResult::cost`); confirms the right call was measured |
| `ns_per_call`   | raw wall time, for debugging a suspicious ratio                                              |

`declared_gas` is read from the declaration through the `wasm_testkit` bridge
(`declared_gas(wasm_name)`), not transcribed into C++ — 61 copied constants would drift from
`lib.rs` the first time a price changed, and drift _silently_, because a benchmark has nothing
to fail.

`price_ratio` is what you sort by. **Below 1 is the direction that matters**: an underpriced call
is one a contract can buy too cheaply, which is a denial-of-service vector rather than a rounding
error. Above 1 the table merely overcharges. The mispriced functions come to the top with:

```bash
./build/xrpl.bench.wasm --benchmark_format=json |
    jq -r '.benchmarks[] | select(.price_ratio) | [.price_ratio, .name] | @tsv' | sort -n
```

### How `suggested_gas` is measured

Gas is not a unit of time, so a wall-clock number cannot be a gas number. The bridge between them
is that **gas is wasmi fuel** — `set_fuel(gas)` meters guest instructions and host charges from
one pool — so one unit of gas is, by construction, about one guest instruction. That turns the
question into a ratio: _how many guest instructions' worth of work is this host call?_ Everything
below exists to answer that without any hard-coded constant.

Four steps, each a subtraction, all in `WasmBench.h` / `WasmBench.cpp`.

**1. `Calibration::secondsPerGas()` — what one unit of gas costs on this machine.**
Assemble two modules that differ only in a loop bound: one runs a trivial `i32.add` body
`kCallsPerRun` times, the other zero times. Run both, and take

```
secondsPerGas = (time_busy − time_idle) / (fuel_busy − fuel_idle)
```

Both numerator terms include module compilation, instantiation and process noise; both
denominator terms include the engine's fixed overhead. Subtracting cancels all of it, leaving
seconds per unit of fuel. Taken as the **minimum over 32 pairs** (after 8 warm-up pairs), because
the fastest run is the one least disturbed by the scheduler. Cached — it describes the machine,
not the case.

**2. `secondsPerCall` — the isolated cost of one host call.**
The same subtraction, one level up. A `ThroughVm` case runs a contract making N host calls and a
**byte-identical** one making none, then reports `(t_loaded − t_baseline) / N` _per iteration_, so
Google Benchmark's variance statistics describe the host call rather than the run containing it.
An `Impl` case times `kCallsPerRun` direct calls and divides, spreading the clock read over
enough work that it does not distort a cheap call.

**3. `implied_gas` — the measurement, in gas.**

```
implied_gas = secondsPerCall / secondsPerGas
```

Machine-independent: both terms scale with the box, so the ratio does not.

**4. `Calibration::crossingFloorGas()` — the toll every call pays.**
Measured once, from `ldgr_index` — the cheapest host function there is, taking no input and
answering from a header already in hand, so almost nothing remains after subtracting it away:

```
crossing_floor = (secondsPerCall_ThroughVm − secondsPerCall_Impl) / secondsPerGas
```

That is region decode, bounds checks and the cxx hop, and nothing else.

**Putting it together:**

```
suggested_gas = implied_gas                     # ThroughVm — the guest already paid the crossing
suggested_gas = implied_gas + crossing_floor    # Impl — a guest cannot call without paying it
price_ratio   = declared_gas / suggested_gas
```

**Why you can trust it measured the right thing.** `charged_gas` comes from the engine's own fuel
meter (`EscrowResult::cost`), independently of every timing above. On a `ThroughVm` case it should
equal the declared gas plus the fuel the guest itself burns — the loop body (13, which
`GuestInstruction` reports on its own) plus the `i32.const`s pushing the call's arguments. So
`escrow_id` reports `charged_gas ≈ 367` against a declared 350: 13 for the loop, 4 for its six
argument constants. When that arithmetic does not line up, the case is measuring something other
than the call it names.

**`guestInstruction` is the harness's self-test, and it has a number.** It runs the same loop body
`Calibration::secondsPerGas()` calibrates against, so its `implied_gas` (from wall time) and
`charged_gas` (from the engine's fuel meter) are two measurements of one quantity and must agree.
On a quiet machine, Release, that is currently **`implied_gas` ≈ 13.6 against `charged_gas` 13.007
— about 4% high, with roughly 4% run-to-run spread.** Treat a persistent gap much beyond that as a
harness bug rather than a property of the machine, and do not trust any other number in the run
until it is closed.

That check is worth running because it has already caught a real defect. Calibration originally
took a _best-of-N_ while the cases report a _mean_, and since `implied_gas =
secondsPerCall / secondsPerGas`, a minimum in the divisor against a mean in the dividend biased
every reported number one way — `guestInstruction` read 18.2 against 13.007, +40%, and every
`suggested_gas` in the report was inflated by that factor. Both estimators are now means. **If you
change how either side is estimated, change both.**

**Two limitations, both real.**

- `suggested_gas` for an `Impl`-only case is a **lower bound**. The crossing floor is measured on a
  call with no input, so a function that moves bytes pays more than the floor. The swept cases
  (`Sha512Half`, `UpdateData`) measure that per-byte term where it matters.
- A Debug build inflates the crossing far more than the impls, so absolute values are not usable
  there. **Ratios between `Impl` cases survive Debug; `suggested_gas` does not.**
- Run-to-run spread is a few percent, which is immaterial next to the pricing errors this suite finds
  (6x-20x). If you need it tighter, `--benchmark_repetitions=N` averages the noise down; it will
  not touch a systematic bias, which is what the `guestInstruction` check above is for.

### The two case kinds, and why the subtraction is the point

Every function has an `Impl` case; the distinct crossing shapes also have a `ThroughVm` case.

- **`Impl`** — the host method called directly. No guest, no VM, no marshalling: the computation alone.
- **`ThroughVm`** — the same call made by a real WAT contract through the real VM, against a real
  ledger. Measured as the difference between a contract making N host calls and a **byte-identical**
  one making none, so compilation, instantiation and the guest's own loop cancel out.

`ThroughVm − Impl` is the **crossing**: region decode, bounds checks, memory copies, the cxx hop.
`Crossing.bench.cpp` brackets its floor with the cheapest possible host call, and the size-swept
cases (`Sha512Half`, `UpdateData`) expose its per-byte term.

`ThroughVm` cases are deliberately **one per crossing shape, not one per function** — the same
argument as the e2e rule above. What the crossing costs depends on a call's shape, not on which
function makes it, so a `float_sub` ThroughVm would only re-measure `float_add`'s.

### Gotchas, all of which have already cost someone an afternoon

- **The wasm ABI is not the trait's argument order.** `float_add(x, y, mode, out)` in Rust is
  `(x_ptr, x_len, y_ptr, y_len, out_ptr, out_len, mode)` on the wire — scalars move _after_ the
  output region. Check `register.rs`, not `lib.rs`, when writing WAT.
- **A soft host error still "succeeds".** The run completes and gas is charged _before_ the body,
  so a wrong-argument case reports a plausible, confidently wrong number — it measures the
  rejection path. The harness guards this by requiring the contract's result to be `>= 0`. The
  tell is a `ThroughVm` case coming out _faster_ than its `Impl` pair.
- **A host serves exactly one run** (`checkSelf` assert in `WasmVM.cpp`), so a benchmark builds a
  fresh host per run and cannot pre-cache a slot.
- **`MAX_FIELD_BYTES` is 1024** — no value crosses the boundary in either direction above 1 KiB,
  so size sweeps stop there.
- Cases pin `->Iterations(...)`: with `UseManualTime`, Google Benchmark's automatic sizing reads
  only the tiny reported residue and would ask for millions of iterations.

## What `e2e/` covers — the rule

**`e2e/` covers every marshalling shape and every cross-call convention exactly once. It does
not cover every function.** That is a completeness claim on the axis e2e can uniquely test, not
a sample.

The reasoning: `host_calls` pins what the bridge _asks_ a host and what it does with a _canned_
answer; `host_functions` pins what the real impl _answers_. The C++ type system guarantees the
two agree on signatures — the real impl implements the same interface the mock does. What
nothing guarantees is that they agree on **conventions**: units, endianness, buffer layout, the
meaning of a wire format. A mocked bridge test and a direct impl test can both pass while
meaning different things by "a four-byte sequence number", because in neither test does a real
guest write bytes that a real host reads. That is precisely the `seq`-as-little-endian-region
bug: every internal test passed, and it was caught by cross-checking the guest SDK.

Convention mismatch is a property of the **shape** of a call, not of the function making it. All
19 keylet functions share one shape; a 19th keylet e2e proves nothing the 1st did not. So the
inventory below is indexed by shape, and it is meant to be exhaustive:

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
| **error** path from a real impl           | `HostErrorE2e`             | soft code produced by a real failure, not a staged one   |
| realistic multi-call contract             | `HostFunctionTourE2e`      | the old `all_host_functions` tour shape, as one test     |

Adding a function does not require a new e2e case — unless it introduces a shape or a convention
not in that table, in which case it does. Per-function breadth (does fn X return the right
value, does it marshal correctly) lives in `host_functions/` and `host_calls/`, one case each,
and re-driving that shared machinery 61 times e2e would cost heavy per-test ledger setup for no
added signal.

The **guest SDK** (`xrpl-std` / `xrpl-escrow`, from the external `xrpl-wasm-stdlib` repo) is
intentionally **not** exercised here: that is the SDK repo's own test suite. WAT tests the host
side (this repo's code); a compiled guest would couple this suite to that repo and a Rust→wasm
toolchain.

## SDK ↔ host agreement — what the retired fixtures tested, and why it lives elsewhere

The old `wasm_fixtures/` guests (`all_host_functions`, `all_keylets`, `codecov_tests`) were
compiled from the real `xrpl-std` / `xrpl-escrow` SDK, so beyond exercising host functions they
implicitly tested the **SDK's side of the ABI contract** — that the SDK and the host agree on the
wire format:

- **host bindings** — import module/name and parameter order/types actually reach the host functions
- **field-code & locator encoding** — `sfield` constants and nested-field `Locator` serialization
- **type serialization** — `Issue` / `Currency` / `MptId` / `XrpIssue` encode to the byte layouts the host decodes
- **error-code enum** — the SDK's `error_codes` match the host's wire numbers
- **size constants** — `DEFAULT_BLOB_SIZE` / `XRPL_CONTRACT_DATA_SIZE` match the host's caps
- **typed accessors** — `get_current_escrow`, `ledger_object::get_field`, `keylets::*` build requests and decode responses

None of that is host code — it is the SDK's, and it is the `xrpl-wasm-stdlib` repo's job to test.
The tests here hand-write the ABI in WAT (raw imports, literal field codes, hand-built byte
layouts), which **deliberately bypasses all SDK code**. So SDK correctness is out of scope here by
design.

**The one residual gap** is the _direct_ SDK↔host cross-check a compiled guest gave for free. The
new split verifies agreement **transitively**: the SDK repo tests the SDK against the ABI spec, and
this repo tests the host against the same spec (`host_calls`, the `generated_abi.rs` spec table,
`host_errors.rs`). That is sound as long as both conform to the spec; it would not catch a drift
where the SDK and host diverge on an ambiguous point. Closing that gap is **not** an xrpld unit
test — it is a **cross-repo integration test** (compiled `xrpl-wasm-stdlib` guests run against a
real xrpld host) belonging in CI where the Rust→wasm toolchain exists.

## Out of scope — transactor-level (L5) tests deferred until the transactor is wired

This migration ported `Wasm_test.cpp` + the `wasm_fixtures/` guests, which drive the VM directly
via `runEscrowWasm`. In the upstream `ripple/smart-escrow` branch the **same fixtures** are also
consumed by two **transactor-level** suites that are **not** part of this port and have **no
equivalent here yet**, because the redesign branch does not yet wire `runEscrowWasm` into the
`EscrowFinish` transactor (it has no caller under `src/xrpld`):

- **`EscrowSmart_test.cpp`** — full `Env → EscrowFinish → ledger`. Its cases test things none of
  the layers above cover, because they only exist once a transactor runs the contract:
  - **`set_data` persistence** — "Update escrow data on failure" asserts the contract's data field
    is written to the escrow ledger object **even on `tecBYTECODE_REJECTED`**. (Note: in _this_
    branch `set_data` is _not_ persisted — there is no transactor caller yet — so this is a real
    gap, not a redundancy.)
  - **gas → fee / meta** — `sfGasUsed` and `sfVMReturnCode` surfaced in transaction metadata.
  - **owner reserve / owner count** accounting for the bytecode-bearing escrow.
  - **transactor-driven tours** — "Test all host functions", "Test all keylet host functions",
    "Test large wasm modules".
- **`PayChan_test.cpp`** — also consumes `wasm_fixtures` symbols at the transactor level.

These belong to the **L5 transactor layer**. When `EscrowFinish` is wired to `runEscrowWasm` in the
redesign, those cases need a home (as C++ transactor tests over a real `Env`), and the persistence /
gas-in-meta / reserve behaviors should be pinned there — the WAT layers here deliberately stop at
the VM boundary and do not exercise the transactor.

## Old → new test map

`Wasm_test.cpp` (retired) + `wasm_fixtures/` guests → the new design. ★ = authored during the
migration.

| Old test / fixture                                                    | New home                                                                                                                                           | Status                                               |
| --------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------- |
| `wasm lib test` (inline addTwo)                                       | Rust `vm_limits::WasmVMTest::ContractReturnValueReachesCaller`                                                                                     | superseded                                           |
| `bad wasm test`                                                       | Rust `preflight::garbage_does_not_pass`                                                                                                            | superseded                                           |
| `Wasm get ledger sequence`                                            | `host_calls/LedgerSqn` + ★`e2e/LedgerSqn`                                                                                                          | superseded + e2e                                     |
| `import/export functions`                                             | Rust `preflight` (imports) / `vm_limits` (imports at instantiation)                                                                                | superseded                                           |
| `import/export section corruption`                                    | Rust `preflight::garbage_does_not_pass` + ★`structurally_malformed_modules_are_refused`                                                            | superseded + ported                                  |
| `Wasm fibo`                                                           | Rust `budgets` (gas baseline)                                                                                                                      | superseded                                           |
| `wasm test host functions cost`                                       | Rust `budgets` (`a_host_call_costs_its_gas...`)                                                                                                    | superseded                                           |
| `escrow wasm devnet` (26-fn tour)                                     | `host_functions/*` + `host_calls/*` + ★`e2e/*` (incl. `HostFunctionTourE2e`)                                                                       | decomposed                                           |
| `Codecov wasm test`                                                   | Rust `memory_policy` + `host_calls` error paths                                                                                                    | superseded                                           |
| `float point` (was commented)                                         | `host_functions/Float*` + Rust `host_calls` float (incl. ★8 gaps)                                                                                  | superseded + ported                                  |
| `disabled float`                                                      | Rust `vm_limits::disabled_features` (floats)                                                                                                       | superseded                                           |
| `memory limit tests` (×11)                                            | Rust `memory_policy` + `vm_limits`                                                                                                                 | superseded                                           |
| `table limit tests` (×5)                                              | Rust `vm_limits`                                                                                                                                   | superseded                                           |
| `disabled proposal tests` (×13)                                       | Rust `vm_limits::disabled_features`                                                                                                                | superseded                                           |
| `trap tests` (×5)                                                     | Rust `vm_limits`: `unreachable` + ★div0 / overflow / null-indirect / sig-mismatch                                                                  | all ported                                           |
| `Wasm Wasi tests` (×2)                                                | Rust `preflight`/`vm_limits` (unknown import)                                                                                                      | superseded                                           |
| `Section Corruption` (×10)                                            | Rust ★`structurally_malformed_modules_are_refused` (6) + ★`parser_abuse_shapes_are_refused` (4 DoS)                                                | all ported                                           |
| `start function loop`                                                 | Rust `vm_limits` (start section) / `preflight`                                                                                                     | superseded                                           |
| `Wasm Bad Align`                                                      | Rust ★`host_calls::an_unaligned_input_is_read_intact`                                                                                              | ported                                               |
| `invalid return type` / `invalid params`                              | Rust `preflight`/`vm_limits`                                                                                                                       | superseded                                           |
| `Wasm swap bytes`                                                     | plain C++ endian util test (not a VM test)                                                                                                         | keep as-is                                           |
| `Many params` — params / locals / functions                           | Rust ★`vm_limits` (`too_many_params`, `too_many_locals`, `past_the_register_frame`, `many_functions_currently_run_unbounded` [`#[ignore]` marker]) | ported (functions enforcement deferred to preflight) |
| `deep recursion`                                                      | Rust ★`vm_limits::unbounded_recursion_is_stopped_by_the_call_stack_limit`                                                                          | ported                                               |
| `infinite loop`                                                       | Rust `budgets::an_endless_loop_is_stopped_by_gas`                                                                                                  | superseded                                           |
| `reserved opcodes`                                                    | Rust `vm_limits::disabled_features` (representative)                                                                                               | superseded                                           |
| float sub/mult/div/pow, from_stamount/stnumber, to_int, from_mant_exp | Rust ★`host_calls` (8 tests)                                                                                                                       | ported                                               |
| **Fixture** `all_host_functions`                                      | ★`e2e` (incl. tour) + `host_functions/*` + `host_calls/*`; SDK → external repo                                                                     | decomposed                                           |
| **Fixture** `all_keylets` (was orphaned)                              | `host_functions/*Keylet`; SDK → external repo                                                                                                      | decomposed                                           |
| **Fixture** `codecov_tests`                                           | Rust `memory_policy` + `host_calls` error paths + ★`e2e`                                                                                           | decomposed                                           |
| `getData helper functions`                                            | — (tested a deleted engine API)                                                                                                                    | removed                                              |

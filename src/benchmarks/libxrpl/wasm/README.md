# WASM host functions — gas calibration

These are **not tests**: nothing asserts, and a number moving is not a build failure. They answer
the question the tests cannot — whether each `#[gas = N]` in
`crates/xrpl-host-functions/src/lib.rs` matches what the function actually costs.

```bash
cmake --build build --target xrpl.bench.wasm
./build/xrpl.bench.wasm # everything (~2 min)
./build/xrpl.bench.wasm --benchmark_filter=sha512Half
./build/xrpl.bench.wasm --benchmark_repetitions=25 --benchmark_report_aggregates_only=true
```

**Build Release.** Debug inflates the crossing far more than the impls: `Impl`-to-`Impl` ratios
survive it, `suggested_gas` does not.

## Reading the output

| Counter             | Meaning                                                                            |
| ------------------- | ---------------------------------------------------------------------------------- |
| `suggested_gas`     | **the answer** — what this function should be priced at                            |
| `host_function_gas` | what `lib.rs` says today, read through the `wasm_testkit` bridge so it can't drift |
| `price_ratio`       | `host_function_gas / suggested_gas`. **1.0 is correct; below 1 is underpriced**    |
| `rel_error`         | relative uncertainty of `suggested_gas`. **Quote this one** — ~1.2% when quiet     |
| `unreliable`        | `1` means do not act on this row                                                   |
| `implied_gas`       | the raw measurement, before the crossing is added back                             |
| `charged_gas`       | what the engine actually billed; confirms the right call was measured              |
| `ns_per_call`       | raw wall time, for debugging a suspicious ratio                                    |

Sort by `price_ratio`. **Below 1 is the direction that matters** — an underpriced call is one a
contract can buy too cheaply, a denial-of-service vector rather than a rounding error:

```bash
./build/xrpl.bench.wasm --benchmark_format=json |
    jq -r '.benchmarks[] | select(.price_ratio) | [.price_ratio, .name] | @tsv' | sort -n
```

`unreliable=1` when `rel_error` exceeds 25%, or when `suggested_gas` falls below the crossing floor
— a call whose own cost is small next to the crossing is read off the difference of two nearly
equal numbers.

### With `--benchmark_repetitions`

Adds `_mean` / `_median` / `_stddev` / `_cv` rows. One trap worth knowing:

**`cv` on `suggested_gas` for an `Impl` case is not an error bar.** The crossing floor is measured
once per process, so repetitions never resample it — and it is most of a cheap `Impl` case's value.
Over 25 repetitions on an idle machine, `Impl` `cv` reads **1.3%** against `ThroughVm`'s 2.3%, while
`rel_error` is 1.2% for both. The lowest number in the output is the least trustworthy one.

Use repetitions to confirm the machine is quiet and to get a median; quote `rel_error`. Under load
the `Impl` cases inflate first (3.0% against 1.8% at load ~10), which makes the gap between the two
families a usable load detector.

## How `suggested_gas` is measured

Gas is wasmi fuel — `set_fuel(gas)` meters guest instructions and host charges from one pool — so
one gas is about one guest instruction and the question becomes a ratio. Every step is a
subtraction, so fixed costs cancel:

```
secondsPerGas  = (time_busy − time_idle) / (fuel_busy − fuel_idle)   # pure-wasm loop, N vs 0
implied_gas    = secondsPerCall / secondsPerGas
crossing_floor = (ldgr_index ThroughVm − ldgr_index Impl) / secondsPerGas

suggested_gas  = implied_gas                     # ThroughVm — the guest already paid the crossing
suggested_gas  = implied_gas + crossing_floor    # Impl — a guest cannot call without paying it
price_ratio    = host_function_gas / suggested_gas

rel_error = sqrt( ( sqrt((implied·caseErr)² + (floor·floorErr)²) / suggested )² + perGasErr² )
```

`secondsPerCall` is itself a subtraction: a `ThroughVm` case runs a contract making N host calls
against a **byte-identical** one making none, so compilation, instantiation and the guest's own loop
cancel. An `Impl` case times `kCallsPerRun` direct calls and divides.

`secondsPerGas` is measured **once** and shared by every case, deliberately — two routes to the same
price must divide by the same constant, and per-case calibration was tried and swung 6x between
runs. Its uncertainty is propagated arithmetically instead. See the comments in `WasmBench.cpp` for
why the three terms in `rel_error` do not all combine in quadrature.

**`guestInstruction` is the self-test — read it first.** It runs the calibration's own loop body, so
its `implied_gas` (wall time) and `charged_gas` (the fuel meter) are two independent measurements of
one quantity. Quiet Release machine: **≈13.7 against 13.007, ~5% high.** A persistent gap much
beyond that means every other number in the run shares it. It has already caught an estimator
mismatch worth +40%, and the memory leak below.

**`suggested_gas` for an `Impl`-only case is a lower bound** — the crossing floor is measured on a
call with no input, so a function that moves bytes pays more; `Sha512Half` and `UpdateData` sweep
that per-byte term. `ThroughVm` cases are deliberately one per crossing _shape_, not one per
function.

## VM overhead (`Vm.cpp`)

Everything above prices what a contract _asks for_. `Vm.cpp` prices getting it running at all. Of
the seven stages `runEscrowWasm` performs — compile, store, linker, instantiate, entry-point lookup,
call, fuel read — **only the call is metered**; the rest is wall time no transaction pays for.

These cases report `ns_per_op` and `gas_equivalent` (that time over the same `secondsPerGas`, so an
unpriced stage reads on the same axis as a priced one), plus `module_bytes` and `gas_per_byte` on
size sweeps.

| case                       | measures                                                                                                                     |
| -------------------------- | ---------------------------------------------------------------------------------------------------------------------------- |
| `preflightMinimal`         | compile plus the import/export walk. The narrowest view of compilation reachable from C++: no host, no instance              |
| `preflightRejects`         | the same on a module refused at the import walk. Against `preflightMinimal`, what refusing costs versus accepting            |
| `runMinimal`               | a whole run of a do-nothing contract — every stage. Minus `preflightMinimal`, the stages that are not compilation            |
| `compileScaling/N`         | compilation against module size: `N` unreachable filler functions, preflight only                                            |
| `runScaling/N`             | the same modules through a whole run. Paired with `compileScaling` per size, separates size-dependent stages from fixed ones |
| `instantiateScaling/pages` | declared memory at constant module size, so what moves is the host allocating and zeroing pages                              |

Two of those pairings are the point of the file. `runMinimal − preflightMinimal` gives the fixed
cost of everything that is not compilation; `runScaling` against `compileScaling` shows compilation
appearing a second time, because the transactor validates and executes with no module cache between
them. The size sweeps matter more than the floor: a fixed cost is only a griefing concern if it is
large, but a slope against attacker-chosen module size is one at any height.

Two caveats when reading a sweep. `gas_per_byte` is an **average** carrying the case's fixed cost,
not a marginal rate — it overestimates, and falls toward the true slope as the module grows, so read
the convergence rather than any single row. And the `/4096` points get few iterations and go noisy
first; compare `rel_error` across the sweep before quoting the largest one.

The sweep stops at 4096 functions for want of a real cap to stop at — no maximum contract size is
enforced anywhere yet, the transactor not being wired.

The linker rebuild and the fuel-metering overhead are **not** separable from here — C++ sees only
`runEscrowWasm` and `preflightEscrowWasm`. Both need benchmarks inside `xrpl-wasm-vm`, where
`compile` and `wasm_engine` are `pub(crate)`.

### Compiling leaks — pin your iteration counts

`wasm_engine()` is a process-global `LazyLock<Engine>`, and what `Module::new` adds to it is never
released. Repeatedly preflighting one **60-byte** module:

| `--benchmark_repetitions` | peak RSS |
| ------------------------- | -------- |
| 1                         | 0.41 GB  |
| 5                         | 1.46 GB  |
| 15                        | 4.20 GB  |

Linear, at roughly **800 bytes per compile**. Within the suite this is why every `Vm.cpp` case pins
`->Iterations(...)`: automatic sizing ran `preflightMinimal` ~348k times per repetition, reaching
7.9 GB at 25 repetitions, after which every later case in the binary failed to compile — 720 errored
rows, all blaming cases that were innocent.

**Outside the suite it is worth a look.** A validator compiles twice per programmable-escrow
transaction against that same static engine. Whether that is unbounded growth in production depends
on wasmi internals not checked here — this is the C++-visible symptom, not a diagnosis. wasmi is
pinned at `2.0.0-beta.10`, a beta, so try a version bump first.

## Gotchas, each of which has already cost someone an afternoon

- **The wasm ABI is not the trait's argument order.** `float_add(x, y, mode, out)` in Rust is
  `(x_ptr, x_len, y_ptr, y_len, out_ptr, out_len, mode)` on the wire — scalars move _after_ the
  output region. Check `register.rs`, not `lib.rs`, when writing WAT.
- **A soft host error still "succeeds".** The run completes and gas is charged _before_ the body, so
  a wrong-argument case reports a plausible, confidently wrong number. The harness requires the
  contract's result to be `>= 0`; the tell is a `ThroughVm` case coming out _faster_ than its `Impl`.
- **A host serves exactly one run** (`checkSelf` in `WasmVM.cpp`), so a benchmark builds a fresh host
  per run and cannot pre-cache a slot.
- **`MAX_FIELD_BYTES` is 1024** — nothing crosses the boundary above 1 KiB, so size sweeps stop there.
- **Do not compare timings across builds at the cheap end.** Two builds whose timed regions were
  byte-identical measured 2.18 ns and 2.52 ns for the same case — ~15% apart, from code layout alone.
  Use `price_ratio` within one run, and reason from the code when judging whether a change costs
  anything.
- **Every case pins `->Iterations(...)`, for two different reasons.** Host-function cases must
  because with `UseManualTime` automatic sizing reads only the tiny reported residue and would ask
  for millions of iterations; `Vm.cpp` cases must because compiling leaks.

## Layout

One `.cpp` per host function under `host_functions/`, mirroring
`src/tests/libxrpl/tx/wasm/host_functions/`, so adding a host function is a two-file checklist
rather than a judgement call. `Crossing.cpp` holds the harness's own reference points, `Vm.cpp` the
per-run overhead around them, `WasmBench.*` the measurement machinery, `BenchFixtures.*` the shared
ledger (one ledger, funded once, for the whole binary).

The ledger and real host come from `xrpl.testkit.wasm` — a framework-free library built alongside
the tests — so this target links **no GTest and no GMock**. See
`src/tests/libxrpl/tx/wasm/README.md` for how that library is split, and why its setup steps throw
rather than using `EXPECT_`.

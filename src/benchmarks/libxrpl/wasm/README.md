# WASM host functions — gas calibration

These are **not tests**: nothing asserts, and a number moving is not a build failure. They answer
the question the tests cannot — whether each `#[gas = N]` in
`crates/xrpl-host-functions/src/lib.rs` matches what the function actually costs.

Shared ledger setup is the `Fixtures` type: one ledger, funded once, for the whole binary.

```bash
cmake --build build --target xrpl.bench.wasm
./build/xrpl.bench.wasm # everything (~2 min)
./build/xrpl.bench.wasm --benchmark_filter=sha512Half
```

**Build Release first.** Debug inflates the crossing far more than the impls. Ratios between
`Impl` cases survive Debug; `suggested_gas` does not.

## Reading the output

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

## How `suggested_gas` is measured

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

## Gotchas, each of which has already cost someone an afternoon

- **The wasm ABI is not the trait's argument order.** `float_add(x, y, mode, out)` in Rust is
  `(x_ptr, x_len, y_ptr, y_len, out_ptr, out_len, mode)` on the wire — scalars move _after_ the
  output region. Check `register.rs`, not `lib.rs`, when writing WAT.
- **A soft host error still "succeeds".** The run completes and gas is charged _before_ the body,
  so a wrong-argument case reports a plausible, confidently wrong number. The harness requires the
  contract's result to be `>= 0`; the tell is a `ThroughVm` case coming out _faster_ than its `Impl`.
- **A host serves exactly one run** (`checkSelf` in `WasmVM.cpp`), so a benchmark builds a fresh
  host per run and cannot pre-cache a slot.
- **`MAX_FIELD_BYTES` is 1024** — nothing crosses the boundary above 1 KiB, so size sweeps stop there.
- **Do not compare timings across builds at the cheap end.** Two builds whose timed regions were
  byte-identical measured 2.18 ns and 2.52 ns for the same case — ~15% apart, from code layout
  alone. At ~2 ns the cheapest calls are below the resolution of a cross-build comparison; use
  `price_ratio` within one run, and reason from the code when deciding whether a change costs
  anything.
- Cases pin `->Iterations(...)`: with `UseManualTime`, automatic sizing reads only the tiny reported
  residue and would ask for millions of iterations.

## Layout

One `.cpp` per host function under `host_functions/`, mirroring
`src/tests/libxrpl/tx/wasm/host_functions/`, so adding a host function is a two-file checklist
rather than a judgement call. `Crossing.cpp` holds the harness's own reference points,
`WasmBench.*` the measurement machinery, `BenchFixtures.*` the shared ledger.

The ledger and real host come from `xrpl.testkit.wasm` — a framework-free library built alongside
the tests — so this target links **no GTest and no GMock**. See
`src/tests/libxrpl/tx/wasm/README.md` for how that library is split, and why its setup steps throw
rather than using `EXPECT_`.

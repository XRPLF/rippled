# WASM host-function tests — layering

These tests are deliberately **layered**: each isolates one thing, so a failure points at one
place instead of "somewhere in the stack." If a folder looks thin, the breadth it seems to be
missing lives in a sibling layer.

## The layers

| Layer                                 | Location                                            | host | VM  | ledger | Answers                                                                                  |
| ------------------------------------- | --------------------------------------------------- | ---- | --- | ------ | ---------------------------------------------------------------------------------------- |
| Engine / gas / limits / ABI           | `crates/xrpl-wasm-vm`, `crates/xrpl-host-functions` | mock | ✓   | ✗      | gas, transfer budget, memory/field limits, preflight, VM limits, generated ABI           |
| `host_context/` (`HostContextTest`)   | `.../host_context`                                  | mock | ✗   | ✗      | the `HostContext` marshalling shim alone (byte order, buffer sizing, `SField` xlat)      |
| `host_calls/` (`HostCallTest`)        | `.../host_calls`                                    | mock | ✓   | ✗      | per-function **wire contract** — what the host was asked, what came back                 |
| `host_functions/` (`RealHostFixture`) | `.../host_functions`                                | real | ✗   | real   | each function's **actual answer** vs. a real `TxTest` ledger                             |
| `e2e/` (`RealVmTest`)                 | `.../e2e`                                           | real | ✓   | real   | **full-stack integration** — VM + `HostContext` + real impl + real ledger                |
| `transactor/` (`TxTest`)              | `.../transactor`                                    | real | ✓   | real   | the **transactor** around a contract — fees, reserves, limits, what each failure reports |

Run the C++ side with:

```bash
./build/xrpl_tests --gtest_filter='*Impl.*:*Call.*:*E2e.*:WasmVMTest.*:WasmVMDeathTest.*:PreflightTest.*:BytecodeSize.*:BytecodePreflight.*:FinishFailures.*:BytecodeRun.*:GasFees.*:DataOnReject.*'
```

(744 tests, 142 suites.) The engine-level coverage is Rust: `cd crates && cargo test`.

## `fixtures/` — split by whether it needs a test framework

|                                                |                                                                                                                                                                                                                                                                                    |
| ---------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **No GTest** — the `xrpl.testkit.wasm` library | `WasmLedger` (real genesis ledger + the real host over it), `WasmRun` (WAT assembler), `NftSetup`, `FloatConstants`                                                                                                                                                                |
| **GTest** → `xrpl_tests`                       | `RealHostFixture` (`: testing::Test, WasmLedger` + `expectValue`/`expectError`/`expectKeyletMatches`), `FloatFixture`, `NFTFixture`, `MockHostFunctions`, `WasmFixture`, `RealVmTest`, `HostContextFixture`, `EscrowWasm` (transactor contracts + fee arithmetic), `ModuleBuilder` |

The split exists because a benchmark wants a ledger and a host, not GTest's lifecycle:
`xrpl.bench.wasm` links no GTest and no GMock at all.

Setup steps in `WasmLedger` and `NftSetup` **throw** (`fixtureFailed`) rather than using `EXPECT_`.
An `EXPECT_` outside a running test is recorded and discarded, so a benchmark whose escrow was
never created would still run its host call, take the not-found path, and report a cheap,
plausible, completely wrong price. **If you add a setup step that can fail, throw.**

## Gas calibration

The benchmarks pricing these host functions live in `src/benchmarks/libxrpl/wasm/`, mirroring this
tree one file per function, with their own README.

## What `e2e/` covers — the rule

**`e2e/` covers every marshalling shape and cross-call convention exactly once. It does not cover
every function.** That is a completeness claim on the axis e2e uniquely tests, not a sample.

`host_calls` pins what the bridge _asks_ with a canned answer; `host_functions` pins what the real
impl _answers_. The type system guarantees they agree on signatures, but nothing guarantees they
agree on **conventions** — units, endianness, buffer layout — because in neither test does a real
guest write bytes a real host reads. That is the `seq`-as-little-endian-region bug: every internal
test passed, and it was caught by cross-checking the guest SDK.

Convention mismatch is a property of a call's **shape**, not of the function — all 19 keylets share
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

**The guest SDK** (`xrpl-std` / `xrpl-escrow`, external `xrpl-wasm-stdlib` repo) is the SDK repo's
own suite. These tests hand-write the ABI in WAT (raw imports, literal field codes, hand-built byte
layouts), deliberately bypassing all SDK code. Agreement is verified _transitively_: the SDK repo
tests the SDK against the ABI spec, this repo tests the host against the same spec. That would not
catch a drift where both diverge on an ambiguous point; closing it needs a **cross-repo integration
test** (compiled guests against a real host) in CI, where the Rust→wasm toolchain exists.

## Adding to `transactor/`

Things to know:

- **Fees live in two places and must agree.** A transactor reads its limits from the service
  registry (`ctx.registry.get().getFees()`), while `calculateBaseFee` reads `view.fees()`. Pass a
  `Fees` to `TxTest`'s constructor to set both; reach for `getServiceRegistry().setFees` only when
  a limit has to change _after_ setup.

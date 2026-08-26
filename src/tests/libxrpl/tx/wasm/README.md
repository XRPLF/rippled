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

## Why `e2e/` is a thin integration smoke (and not a per-function tour)

The old Beast suite (`src/test/app/Wasm_test.cpp`, now retired) had guest programs that toured
_many_ host functions in one run (`all_host_functions`, `codecov_tests`). The new design keeps
that breadth but **decomposes** it:

- **Per-function breadth** — "does host fn X return the right value / marshal correctly?" — lives
  in `host_functions/` (answers) and `host_calls/` (marshalling), one case per function.
- **Integration** — "do the layers _agree_ when wired together?" — is what `e2e/` uniquely adds.
  That integration machinery (VM → `HostContext` → impl → ledger) is **shared** across host
  functions; what varies per function (field code, byte layout) is already pinned by
  `host_calls`/`host_functions`. So a few representative **shapes** exercise every integration
  path: a scalar/header read, a ledger-object field read, a transaction read, a write, and one
  multi-call **tour** (`HostFunctionTourE2e`) in the recognizable shape of the old
  `all_host_functions` guest.

Testing all ~40 host functions e2e would re-drive the same shared machinery for little added
signal, at heavy per-test ledger-setup cost. The residual risk — a bug that manifests _only_
through the full stack in a way unique to one function — is small because the machinery is
shared; add a targeted e2e for any function where that risk is real.

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
where the SDK and host diverge on an ambiguous point. Closing that gap is **not** a xrpld unit
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

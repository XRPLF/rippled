# `Simulate.cpp` — Dry-Run Transaction Execution

## Role and Motivation

This file implements the `simulate` RPC command, which allows a client to test a transaction against the current ledger state without submitting it to the network. The primary use case is development and debugging: callers can verify that a transaction would succeed, inspect the metadata it would produce, and examine computed fields — all without spending XRP or affecting any persisted ledger state. Architecturally, `Simulate.cpp` occupies the same handler layer as `Submit.cpp`, but deliberately avoids every side effect real submission triggers: no peer broadcast, no hash-router entry, no queue insertion.

The mechanism that makes this work is `tapDRY_RUN`, a bit flag in the `ApplyFlags` enum (defined in `ApplyView.h`) passed to `TxQ::apply()`. Inside `Transactor.cpp`, this flag bypasses signature checking, routes the transaction through the full engine execution path (including metadata generation via `ApplyContext::apply()`), and then forces `applied = false` before returning — so the `OpenView` snapshot is discarded without ever being committed. This reuses the exact same code path as a real submission, which is what gives the simulation its fidelity.

## Input Handling: `getTxJsonFromParams()`

The command accepts either a pre-serialized blob (`tx_blob`) or a structured JSON object (`tx_json`), but not both. The mutual exclusion is checked first and is fatal — passing both triggers an explicit parameter error. When a blob is supplied, it is hex-decoded and deserialized into an `STObject` via `SerialIter`, then immediately serialized back to JSON. This round-trip through the canonical binary format normalizes the input into a consistent representation before downstream logic touches it.

Two minimal sanity checks follow: the presence of `TransactionType` and `Account`. These are required for the subsequent autofill logic to function at all.

## Autofill Pipeline: `autofillTx()`, `autofillSignature()`, `getAutofillSequence()`

Simulation is designed to work with an incompletely constructed transaction. The autofill pipeline synthesizes fields that would normally be mandatory for signing.

**Fee** is the first thing handled in `autofillTx()`. It calls `RPC::getCurrentNetworkFee()`, which consults the live fee-track and transaction queue to compute a realistic fee. The code comment explicitly notes this must run *after* all other autofills, because fee calculation may depend on other fields (such as transaction type) already being in place.

**Sequence** is derived from `TxQ::nextQueuableSeq()` queried against the live open ledger's account SLE. If the account does not exist in the current ledger and no `TicketSequence` is provided, the function returns `rpcSRC_ACT_NOT_FOUND`. When `TicketSequence` is present, the sequence is set to 0 — matching the wire-format rule that ticket-based transactions carry a zero sequence number.

**NetworkID** is injected only for networks with an ID greater than 1024, consistent with the XRPL protocol rule that mainnet and other networks with IDs ≤ 1024 omit the field entirely.

**Signature fields** deserve careful attention. `autofillSignature()` fills `SigningPubKey` with an empty string and `TxnSignature` with an empty string — the canonical representation of an unsigned transaction in XRPL binary. The `tapDRY_RUN` flag tells `Transactor::checkSign` to skip signature validation when these are empty. However, if the caller supplies a *non-empty* `TxnSignature`, the function immediately returns `rpcTX_SIGNED`. The same check applies per-element for multi-signed transactions in the `Signers` array. This enforcement prevents a confusing scenario where simulate silently executes an already-signed transaction, which might mislead the caller into thinking signature validation passed. The `Transactor.cpp` code includes a defensive comment: "This code should never be hit because it's checked in the `simulate` RPC" — the RPC layer is the first line of defense.

## Simulation Execution: `simulateTxn()`

Once the transaction is validated and autofilled, `doSimulate()` parses the JSON into an `STTx` and wraps it in a `Transaction`. The `ttBATCH` transaction type is explicitly rejected with `rpcNOT_IMPL` before this point — batch transactions involve composite execution semantics the dry-run path cannot faithfully replicate.

`simulateTxn()` takes a snapshot copy of the current `OpenView` by value, then calls `TxQ::apply()` with `tapDRY_RUN`. The result is an `ApplyResult` struct (from `applySteps.h`) containing a `TER` result code, an `applied` boolean, and an `std::optional<TxMeta>` metadata object. Because `tapDRY_RUN` forces `applied = false` at the end of `Transactor::apply()`, the ledger snapshot is never committed.

The response is assembled as follows:
- `applied` and `ledger_index` are set unconditionally.
- `engine_result`, `engine_result_code`, and `engine_result_message` translate the `TER` into human-readable form. For `tesSUCCESS`, the message is overridden to read "The simulated transaction would have been applied" — preventing the generic success string from implying the transaction was actually committed.
- If metadata is present, it is either serialized as hex (`meta_blob`) or rendered as JSON (`meta`) depending on the `binary` parameter. In JSON mode, three enrichment functions run: `RPC::insertDeliveredAmount()`, `RPC::insertNFTSyntheticInJson()`, and `RPC::insertMPTokenIssuanceID()`. These are the same post-processing calls used in the real transaction response pipeline, ensuring simulated results are structurally identical to what a live submission response would contain.
- The transaction itself is echoed back as either `tx_blob` or `tx_json`, now containing the autofilled fields, giving the caller a complete view of what would have been submitted.

## Security and Resource Considerations

`doSimulate()` enforces an explicit blocklist of credential fields before any other processing: if the request includes `secret`, `seed`, `seed_hex`, or `passphrase`, it returns `invalid_field_error` immediately. This is a deliberate design choice — the simulate endpoint should never be a channel through which signing keys are transmitted to the server, even accidentally.

The outer `try/catch` around `simulateTxn()` is marked `LCOV_EXCL` — it is a crash guard against unexpected exceptions and is not expected to fire under any known condition. XRPL's RPC handler convention is to never let an uncaught exception propagate.

Resource cost is set to `feeMediumBurdenRPC`. A full dry-run execution is meaningfully more expensive than a read-only query because it runs the transaction engine, but less expensive than a real submission that triggers peer propagation and queue management.
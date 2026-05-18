# `src/xrpld/rpc/handlers/transaction/Submit.cpp`

## Purpose and Role

This file implements `doSubmit`, the RPC handler for the `submit` command — the primary entry point through which clients inject transactions into the XRP Ledger network. It lives alongside the other transaction-focused handlers (`SubmitMultiSigned`, `Simulate`, `Tx`, etc.) in the `transaction/` subdirectory and is one of the most heavily exercised handlers in normal ledger operation.

The handler supports two mutually exclusive submission modes, selected by the presence or absence of the `tx_blob` field in the request parameters. This bifurcation reflects an architectural decision made during the evolution of the protocol: server-side signing was an early convenience feature that became a security liability, and the `tx_blob` path (pre-signed binary) is now the only mode considered production-safe.

## Two Submission Paths

### tx_blob (Primary Path)

When `tx_blob` is present, the handler executes a strict, layered validation pipeline before dispatching to the network:

1. **Hex decode** — `strUnHex` converts the hex string to raw bytes. An empty or malformed result returns `rpcINVALID_PARAMS` immediately.
2. **Deserialization** — A `SerialIter` over the raw bytes is fed to the `STTx` constructor. Any malformed binary structure throws, caught into an `invalidTransaction` error response.
3. **Signature and local-rule validation** — `checkValidity` consults the `HashRouter` cache to avoid re-verifying the same transaction repeatedly, then checks the cryptographic signature and whether the transaction satisfies the current ledger's rule set. There is one deliberate performance escape hatch here: when `context.app.checkSigs()` returns false (a configuration option for trusted internal submissions), `forceValidity` pre-marks the transaction as `SigGoodOnly` in the router cache, skipping the expensive signature check. This is done with care — the cache can only be raised toward validity, not degraded, so a legitimately bad signature can still be caught by other layers.
4. **Transaction wrapper construction** — The validated `STTx` is wrapped in a `Transaction` object. The `Transaction` constructor performs additional local checks (e.g., fee sanity, account fields), and the status must be `NEW` (`TransStatus::NEW = 0`) to proceed. Any other status — `INVALID`, `HELD`, `OBSOLETE`, etc. — produces an early rejection.
5. **Network dispatch** — `context.netOps.processTransaction` is called with `bLocal = true` and the `fail_hard` flag, handing the transaction to `NetworkOPs` for application to the open ledger, queuing, and P2P broadcast.

### tx_json (Deprecated Path)

When `tx_blob` is absent, the handler falls back to server-side signing via `RPC::transactionSubmit`. This path requires the caller to supply a private key (`secret`) alongside the transaction JSON, which the server uses to sign the transaction before forwarding it. Two access controls guard this path: it is only permitted if the caller has `ADMIN` role, or if the server is explicitly configured with `canSign()` enabled. A `deprecated` warning is injected into every response from this path, signalling that it will eventually be removed.

The `RPC::transactionSubmit` function in `detail/TransactionSign.h` encapsulates signing, fee auto-fill, and the same `ProcessTransactionFn` callback used for the blob path. This design — passing a `std::function<>` wrapping `netOps.processTransaction` — decouples the signing logic from the live networking layer and makes it testable in isolation.

## The `fail_hard` Flag

`getFailHard` reads the optional `fail_hard` boolean parameter and converts it to a `NetworkOPs::FailHard` enum. When set, the network layer will reject the transaction rather than queue it if it cannot be applied immediately to the open ledger. This is useful for submitters who need definitive acceptance or rejection rather than an ambiguous queued state, but it reduces the chances of eventual inclusion under load. Omitting the flag produces the default lenient behavior.

## Response Enrichment

After `processTransaction` returns, the handler assembles a rich JSON response. When the `TER` result is anything other than `temUNCERTAIN` (meaning the network made a deterministic decision), the response includes:

- `engine_result` / `engine_result_code` / `engine_result_message`: human-readable and machine-readable TER classification.
- `accepted`, `applied`, `broadcast`, `queued`, `kept`: individual flags drawn from `Transaction::SubmitResult`, reflecting exactly what the network layer did with the transaction — applied to the open ledger, queued for a future ledger, broadcast to peers, or held in local memory.
- `account_sequence_next`, `account_sequence_available`, `open_ledger_cost`, `validated_ledger_index`: advisory fields from `Transaction::CurrentLedgerState`, populated by `NetworkOPs` during processing, that help clients decide whether to resubmit, bump fees, or wait.

This granular decomposition of the submit outcome — rather than a simple success/failure — is deliberate. Because XRPL is a distributed consensus system, a transaction can be locally applied but not yet broadcast, or broadcast but lost before validation; the individual flags let clients reason about the transaction's propagation state without having to poll the ledger.

## Error Handling Strategy

The handler uses three distinct error-reporting mechanisms appropriate to where failures occur. Pre-dispatch failures (bad hex, deserialization errors, local check failures) return early with structured JSON error objects rather than exceptions, since they represent expected invalid-input scenarios. The `processTransaction` call and the final JSON serialization are each wrapped in their own `try/catch`, distinguishing internal submission failures (`internalSubmit`) from JSON encoding failures (`internalJson`) — a practical diagnostic distinction when debugging node-side issues. The resource cost is registered as `feeMediumBurdenRPC` at entry, ensuring the request accounting subsystem tracks submit calls appropriately regardless of outcome.
# `src/xrpld/rpc/detail/TransactionSign.h`

## Role in the System

This header is the public interface of the RPC transaction pipeline — the boundary between the JSON-speaking RPC handler layer and the signing/submission machinery inside the node. It declares the six functions and one type alias that together implement the `sign`, `submit`, `sign_for`, and `submit_multisigned` RPC methods exposed to clients.

The file lives in `rpc/detail/`, which is the private implementation layer of the RPC subsystem. Callers are the RPC command handlers in `xrpld/rpc/handlers/`; the implementations are entirely in the companion `TransactionSign.cpp`.

## The Four Entry Points

The four transaction-handling functions split along two axes: single vs. multi-signature, and sign-only vs. sign-and-submit.

**`transactionSign`** prepares and cryptographically signs a transaction on the server side, returning the signed `tx_blob` and `tx_json` to the caller without broadcasting it. This is the `sign` RPC method: the caller typically takes the result and submits it independently.

**`transactionSubmit`** does everything `transactionSign` does, then calls into the P2P layer to broadcast the transaction. This is the `submit` RPC method.

**`transactionSignFor`** handles a single participant in a multi-signature quorum. A multi-signed transaction requires signatures from several accounts; each signer calls `sign_for` in turn, adding their `Signer` entry to the `Signers` array in `tx_json`. The server signs on behalf of the specified `account` using that account's supplied secret.

**`transactionSubmitMultiSigned`** accepts a fully assembled multi-signed transaction (the `Signers` array already populated) and submits it. This corresponds to the `submit_multisigned` RPC method.

All four functions take `params` by value rather than const reference. This is intentional: the implementation mutates the JSON freely during preprocessing — auto-filling `Fee`, `Sequence`, `SigningPubKey`, resolving payment paths, and normalizing `DeliverMax` to `Amount` — without touching the caller's original object.

## Fee Infrastructure

The two fee functions exist because fee calculation is a non-trivial multi-step process that also needs to be exposed separately.

**`getCurrentNetworkFee`** computes the recommended fee for a specific transaction against the current open ledger. It takes the *maximum* of two fee estimates: the load-scaled base fee (from `LoadFeeTrack`, which rises under heavy CPU/IO load) and the open-ledger escalated fee (from `TxQ`, which rises as the transaction queue fills). Administrative and identified roles bypass the load-scaling component, paying only the escalation-based fee. The result is capped against a `mult/div` ratio to prevent the auto-fill from injecting excessively high fees.

**`checkFee`** is the higher-level gatekeeper called during preprocessing. If `Fee` is already present in the transaction JSON it returns immediately; if `doAutoFill` is false and `Fee` is absent it returns an error; otherwise it reads the optional `fee_mult_max` / `fee_div_max` fields from the outer request and delegates to `getCurrentNetworkFee`. The defaults — `defaultAutoFillFeeMultiplier = 10` and `defaultAutoFillFeeDivisor = 1` from `Tuning.h` — mean the server will autofill any fee up to 10× the ledger's base reference fee before refusing.

## `ProcessTransactionFn` and the Dependency-Injection Pattern

```cpp
using ProcessTransactionFn = std::function<void(
    std::shared_ptr<Transaction>& transaction,
    bool bUnlimited,
    bool bLocal,
    NetworkOPs::FailHard failType)>;
```

The submit functions do not call `NetworkOPs::processTransaction` directly. Instead they accept a `ProcessTransactionFn` callback. `getProcessTxnFn(NetworkOPs&)` is the inline factory that produces the real implementation by capturing `netOPs` in a lambda.

This indirection serves testability. Unit tests can supply a mock `ProcessTransactionFn` that records what was submitted without spinning up a full `NetworkOPs`. The pattern is deliberate: sign-only functions (`transactionSign`, `transactionSignFor`) don't accept a `ProcessTransactionFn` at all — the absence of the parameter makes it impossible for them to accidentally submit.

## Role and Access Control

Every entry point accepts a `Role` parameter (from `xrpld/rpc/Role.h`), which is an enum with values `GUEST`, `USER`, `IDENTIFIED`, `ADMIN`, `PROXY`, `FORBID`. Within the implementation, `isUnlimited(role)` is checked in two places: once inside `checkTxJsonFields` to gate out requests when the cluster is overloaded (`feeTrack.isLoadedCluster()`), and again in `getCurrentNetworkFee` to exempt administrative callers from the load-scaled fee component. Unlimited roles also pass `bUnlimited = true` to `processTransaction`, which controls queue priority.

## `validatedLedgerAge` Guard

Every entry point also takes a `std::chrono::seconds validatedLedgerAge`. If the node's most recent validated ledger is older than `Tuning::maxValidatedLedgerAge` (2 minutes), the function rejects the request with `rpcNO_CURRENT` (API v1) or `rpcNOT_SYNCED` (API v2+). This prevents clients from signing transactions against a stale fee schedule or sequence number state. The version-gated error code difference is an example of the API versioning strategy used throughout the RPC layer: old error names are preserved for v1 compatibility while newer names are more descriptive.

## Relationship to the Implementation

The heavy lifting is all in `TransactionSign.cpp`, which defines several internal `detail`-namespace helpers invisible to callers: `SigningForParams` (a move-only state object tracking single vs. multi-sign mode, the signer's public key, and the computed signature buffer), `transactionPreProcessImpl` (keypair extraction, field validation, `STTx` construction, cryptographic signing), `transactionConstructImpl` (wraps the `STTx` in a `Transaction`, then round-trip serializes and deserializes it to "sterilize" it — catching any internal inconsistencies before the transaction is accepted into the queue), and `transactionFormatResultImpl` (converts the finished `Transaction` into the JSON response with `tx_blob`, `tx_json`, and optional engine result fields).
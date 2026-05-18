# `SignFor.cpp` — RPC Handler for Multi-Signature Contribution

## Role in the System

`SignFor.cpp` implements `doSignFor`, the entry-point handler for the `sign_for` RPC command. In the XRPL multi-signature workflow, a transaction requires signatures from multiple authorized co-signers before it can be submitted. Rather than gathering those signatures out-of-band, `sign_for` lets one party contribute their single cryptographic signature to a partially-built transaction's `Signers` array. This file is the thin HTTP/RPC boundary that authorizes the call and delegates all real work to `RPC::transactionSignFor` in `detail/TransactionSign.cpp`.

The handler is registered in `Handler.cpp` with `Role::USER`, meaning any connected client can issue the command over the RPC interface. However the file immediately applies a stricter secondary gate: the caller must either be `Role::ADMIN` or the node must be started with `[signing]` enabled in its configuration (`canSign()`). This two-level design intentionally distinguishes between *routing permission* (handled by the RPC dispatch table) and *feature permission* (handled locally in the handler). Nodes that aren't explicitly opt-in signing servers—such as public validators and gateways—return `rpcNOT_SUPPORTED` before touching any cryptographic material.

## Design and Relationship to `Sign.cpp`

The `sign` and `sign_for` commands live as siblings in the same `admin/signing/` directory and share identical access-control logic:

```cpp
if (context.role != Role::ADMIN && !context.app.config().canSign())
    return RPC::make_error(rpcNOT_SUPPORTED, "Signing is not supported by this server.");
```

They diverge in their semantics: `doSign` creates a complete single-signature for a transaction and expects only `tx_json` and `secret`, while `doSignFor` targets multi-signature construction and requires an additional `account` field identifying whose signature slot is being filled. The delegation targets differ correspondingly — `transactionSign` vs. `transactionSignFor`.

There is a subtle difference in how `fail_hard` is read between the two siblings. `doSign` guards with `isMember` before calling `asBool()`, while `doSignFor` calls `asBool()` directly on the possibly-absent field, relying on `Json::Value`'s safe-default behavior to return `false` when the field is missing. Both are functionally correct, but the inconsistency is worth noting for future maintenance.

## Resource Classification

Before delegating, the handler sets:

```cpp
context.loadType = Resource::feeHeavyBurdenRPC;
```

This marks the request as computationally expensive for the server's load-tracking system. Signing involves elliptic-curve cryptography, transaction serialization, and ledger lookups. Classifying it as `feeHeavyBurdenRPC` allows the resource manager to throttle or penalize clients who issue many signing requests, which is especially important on public nodes where `canSign()` may be enabled.

## Delegation to `transactionSignFor`

The handler passes six arguments to `RPC::transactionSignFor`: the raw JSON params (by value so the callee can modify them freely), the API version, the `failType` enum derived from `fail_hard`, the caller's role, the age of the most recently validated ledger, and the application reference. Inside `transactionSignFor`, the real work happens:

1. The `account` field is decoded to an `AccountID`.
2. `checkMultiSignFields` verifies that `tx_json` carries the mandatory `Sequence` and `SigningPubKey` fields, which must be pre-filled by the caller for multi-signing (unlike single-signing where the server can auto-fill them).
3. The transaction is pre-processed through `transactionPreProcessImpl` with a `SigningForParams` context, which handles key derivation and signature computation.
4. The ledger is consulted to verify that the provided secret actually belongs to the claimed `account` via `acctMatchesPubKey`.
5. The resulting `STObject` signer entry (containing `Account`, `TxnSignature`, and `SigningPubKey`) is injected into the `Signers` array of the serialized transaction and returned as JSON.

None of this logic lives in `SignFor.cpp` itself — the file is purely an access-control and resource-accounting shim.

## Deprecation

The handler appends a `deprecated` field to every successful response:

> "This command has been deprecated and will be removed in a future version of the server. Please migrate to a standalone signing tool."

This mirrors identical deprecation warnings in `doSign` and reflects a deliberate architectural direction: server-side signing exposes private keys to the network layer and requires trusting the node operator, making it fundamentally unsafe for production use. XRPL clients are expected to sign locally using libraries such as `xrpl.js` or the C++ `xrpl` library, submitting only a pre-signed blob. The `sign_for` command remains available behind both the admin gate and the `canSign()` opt-in, but its presence in the response signals to callers that the migration clock is running.
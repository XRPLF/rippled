# `DepositAuthorized.cpp` — RPC Handler for Deposit Authorization Queries

## Purpose and Context

This file implements `doDepositAuthorized`, the RPC handler behind the `deposit_authorized` API call. Its sole job is to answer the question: *given a source account, a destination account, and an optional set of credential IDs, would a payment from source to destination currently be authorized under the destination's DepositAuth rules?* This is a pure read-only query — no ledger state is mutated.

The handler exists because the XRPL DepositAuth amendment (flag `lsfDepositAuth` on an account) lets destinations opt in to a gatekeeping model where incoming payments must be pre-approved. Before constructing and signing a transaction, a sender or an application can call this endpoint to check authorization without paying a transaction fee or risking a `tecNO_PERMISSION` error.

## Authorization Logic

After resolving the ledger and verifying both accounts exist, the handler computes a single `reqAuth` boolean:

```cpp
bool const reqAuth = ((sleDest->getFlags() & lsfDepositAuth) != 0u) && (srcAcct != dstAcct);
```

The self-deposit short-circuit (`srcAcct != dstAcct`) is intentional: an account sending to itself is always permitted regardless of the DepositAuth flag. This mirrors the rule enforced at transaction-apply time.

When `reqAuth` is true, authorization can be satisfied in one of two ways:

1. **Account-based preauthorization**: the destination has posted a `DepositPreauth` ledger object keyed by `keylet::depositPreauth(dstAcct, srcAcct)`, explicitly whitelisting the source account.

2. **Credential-based preauthorization**: the caller provides credential IDs, those credentials are valid for the source, and the destination has posted a `DepositPreauth` object keyed by `keylet::depositPreauth(dstAcct, sorted)`, where `sorted` is a canonical `std::set<std::pair<AccountID, Slice>>` of `(issuer, credentialType)` pairs derived from the supplied credentials.

These two checks are ORed together — if either passes, `deposit_authorized` is `true`.

## Credential Validation Pipeline

The optional `credentials` parameter accepts an array of `uint256` credential hashes (hex-encoded). For each credential, the handler:

1. Parses the hex string to a `uint256` key.
2. Looks up the `Credential` SLE via `keylet::credential(credH)`.
3. Confirms the credential has the `lsfAccepted` flag set — unaccepted (pending) credentials are invalid.
4. Calls `credentials::checkExpired` against the ledger's `parentCloseTime`, rejecting credentials whose `sfExpiration` has passed.
5. Confirms `sfSubject` matches `srcAcct` — credentials must belong to the source account, not some other party.
6. Inserts `(sfIssuer, sfCredentialType)` into `sorted`; a duplicate insertion means the caller supplied redundant credentials, which is an error.

The sorted-set representation is not incidental. The on-ledger `DepositPreauth` object for credential-based authorization is keyed using precisely this sorted canonical form, so building the same structure here enables a deterministic, single-lookup existence check against the ledger.

## The `lifeExtender` Pattern

The `sorted` set stores `Slice` values — non-owning, pointer-based views into the `sfCredentialType` field data inside each `SLE`. The SLEs themselves are reference-counted `shared_ptr<SLE const>` objects managed by the ledger cache. Inserting a `Slice` into the set and then allowing the `shared_ptr` to drop out of scope would leave a dangling reference.

`lifeExtender` is a `std::vector<std::shared_ptr<SLE const>>` that does nothing except hold extra references to keep those SLEs alive for exactly as long as `sorted` is in scope. Both variables live on the same stack frame, so the SLEs are guaranteed alive through the `keylet::depositPreauth(dstAcct, sorted)` call. This is a deliberate RAII lifetime extension, not incidental storage.

## Validation Architecture

Input validation is layered to provide precise error codes:

- **Presence and type**: `isMember` + `isString` → `rpcINVALID_PARAMS`.
- **Format**: `parseBase58<AccountID>` → `rpcACT_MALFORMED` for invalid account addresses.
- **Ledger resolution**: `RPC::lookupLedger` handles `ledger_hash`/`ledger_index` with its own error propagation.
- **Ledger existence**: `rpcSRC_ACT_NOT_FOUND` / `rpcDST_ACT_NOT_FOUND` for accounts that don't exist in the chosen ledger.
- **Credential semantics**: `rpcBAD_CREDENTIALS` with specific reason strings for each failure mode (not found, not accepted, expired, wrong subject, duplicates).
- **Array bounds**: the credential array is capped at `maxCredentialsArraySize` (8, from `Protocol.h`) before iteration, preventing unbounded ledger reads.

The destination account is read with `ledger->read` (not just `ledger->exists`) because the SLE flags must be inspected. The source account only needs an existence check, so the cheaper `ledger->exists` is used there.

## Relationship to Transaction Processing

This handler mirrors the authorization checks performed at transaction-apply time by `verifyDepositPreauth` (declared in `CredentialHelpers.h`). The key difference is that `verifyDepositPreauth` operates on an `ApplyView` and can mutate state (deleting expired credentials as a side effect), while this handler uses a read-only `ReadView`. As a consequence, the RPC query may return `deposit_authorized = true` for a credential that is currently valid but becomes expired before the transaction is submitted — the handler deliberately uses `parentCloseTime` (the most recently validated close time) rather than any speculative future time, so results are as current as the selected ledger allows.
# `src/xrpld/rpc/detail/TransactionSign.cpp`

This file implements the complete server-side pipeline for the four XRPL RPC operations that involve transaction signing: `sign`, `submit`, `sign_for`, and `submit_multisigned`. It lives in `namespace xrpl::RPC` with internal helpers tucked into `namespace xrpl::RPC::detail`, and its public surface is declared in the companion header `TransactionSign.h`.

## Architectural Role

The file acts as the bridge between raw JSON arriving over an RPC connection and a fully validated, signed, serialized `STTx` that can be broadcast to the network. Nothing here touches consensus or the ledger engine directly; it converts, validates, signs, and hands off to `NetworkOPs::processTransaction`. The same signing infrastructure supports both single-key and multi-party (threshold) signing, unified through a single pre-processing path.

## `SigningForParams` — Mode Discriminator

The file-private class `SigningForParams` is the linchpin that allows `transactionPreProcessImpl` to serve both single-signing and multi-signing without code duplication. It holds a raw `const*` to an `AccountID` (the signer's account in multi-signing mode, `nullptr` for single-signing), an `std::optional<PublicKey>`, and a `Buffer` for the computed multi-signature.

The design choice to use a raw pointer rather than an `std::optional<AccountID>` is deliberate: the pointer's nullness is the mode discriminator (`isMultiSigning()` / `isSingleSigning()`), and the pointed-to value is always owned by the caller's stack frame. The copy constructor is explicitly deleted, making the intent clear that `SigningForParams` is not meant to escape the local call chain. Accessors `getSigner()` and `getPublicKey()` call `LogicError()` rather than UB-producing dereferences if invoked in the wrong mode, encoding the invariant that callers must check mode before accessing mode-specific data.

An additional optional field `signatureTarget_` carries an `SField` reference that routes the signature into a nested inner object rather than the transaction root — used by the `signature_target` RPC parameter for signing custom inner objects.

## `transactionPreProcessResult` — Poor Man's `std::expected`

The internal struct `transactionPreProcessResult` models a discriminated union: it carries either a `Json::Value` error or a `std::shared_ptr<STTx>`. Both fields are `const`, both implicit constructors are deleted, and only move semantics are permitted. Callers uniformly test `!preprocResult.second` to distinguish the two states. This predates `std::expected<T, E>` (C++23) and serves the same purpose: a typed, non-nullable return that cannot be accidentally ignored.

## `transactionPreProcessImpl` — The Core Pipeline

This static function is where all the heavy lifting happens for `transactionSign`, `transactionSubmit`, and `transactionSignFor`. Its pipeline in order:

1. **Key extraction** — `keypairForSignature()` resolves the key pair from the request's `secret`, `seed`, `seed_hex`, or `passphrase` fields.
2. **Signature target resolution** — If `signature_target` is present in the request, the code looks up the corresponding `SField` and its `SOTemplate` from `InnerObjectFormats`. An unknown target is rejected immediately.
3. **Basic field validation** — `checkTxJsonFields()` gates on `TransactionType`, `Account`, ledger staleness (using different error codes for API v1 vs. v2), and cluster load.
4. **Sequence auto-fill** — In online mode with single-signing, if `Sequence` is absent and no `TicketSequence` is present, the next queuable sequence is fetched from `TxQ`. Ticket-based transactions receive sequence 0. Multi-signing skips this entirely (`editFields()` returns false) because the transaction should already be fully formed before multi-signers add their contributions.
5. **NetworkID auto-fill** — Networks with ID > 1024 (sidechains, testnets) have their ID injected automatically to prevent cross-network replay.
6. **Fee check** — Delegated to `checkFee()`.
7. **Payment-specific validation** — Delegated to `checkPayment()`, which resolves the `DeliverMax`/`Amount` alias, validates destination, and optionally runs the `Pathfinder` for XRP/IOU paths (MPT-denominated amounts cannot use path-finding unless `featureMPTokensV2` is enabled).
8. **Signing mode exclusivity** — Enforces that a transaction cannot simultaneously have a `TxnSignature` field while multi-signing, and cannot have a `Signers` array while single-signing.
9. **Account–key binding** — For single-signing, `acctMatchesPubKey()` verifies the public key matches the account's master key (unless `lsfDisableMaster` is set) or its designated regular key. For delegated transactions, the binding check is performed against the delegate account's ledger entry rather than the transaction's `Account` field.
10. **STTx construction** — `STParsedJSONObject` converts the JSON to the binary serialized form. For multi-signing, `SigningPubKey` is set to an empty byte string (protocol requirement); for single-signing it receives the actual public key.
11. **Signing** — Multi-signing calls `buildMultiSigningData()` to hash the transaction with the signer's account, then signs and stores the result in `signingArgs`. Single-signing calls `stTx->sign()`.

## `acctMatchesPubKey` — Key Validation with Three Cases

This helper handles the nuance that a ledger account can be in one of three authentication states: (a) no ledger entry yet (unactivated account where only the master key is valid), (b) ledger entry with master key enabled (master or regular key accepted), (c) ledger entry with `lsfDisableMaster` set (only regular key accepted). The function encodes all three cases cleanly with early returns and produces typed error codes (`rpcBAD_SECRET`, `rpcMASTER_DISABLED`) rather than booleans, which are threaded up to the RPC response.

## `transactionConstructImpl` — Transaction Sterilization

After signing, this function performs a roundtrip serialization test: the `STTx` is serialized to bytes, deserialized into a fresh `STTx const`, and the two are compared for equivalence. If they differ — or if signature validation fails — the function returns an internal error. This is a defensive correctness invariant: it guarantees that what is broadcast to the P2P network is byte-for-byte identical to what was signed, ruling out any internal representation bug. If `app.checkSigs()` is false (configurable for testing or trusted environments), the hash router is pre-seeded with `Validity::SigGoodOnly` to skip the cryptographic signature check while still confirming structural correctness.

## Fee Pipeline

`getTxFee()` temporarily patches the incoming `tx_json` with placeholder values for `Fee`, `Sequence`, `SigningPubKey`, and `TxnSignature` (and per-signer placeholders for multi-signed transactions), then parses it into an `STTx` purely to call `calculateBaseFee()`. This is necessary because the protocol fee depends on transaction type and content (e.g., the number of signers), not just type alone. The result feeds into `getCurrentNetworkFee()`, which applies load scaling via `scaleFeeLoad()` and then takes the maximum with the TxQ's current escalated fee level. The caller-specified `fee_mult_max`/`fee_div_max` ceiling is enforced last with a `mulDiv()` overflow-safe computation.

## Public API Surface

`transactionSign` and `transactionSubmit` differ only in whether they call `processTransaction` at the end. Both use a default-constructed `SigningForParams()` (single-signing mode).

`transactionSignFor` is the incremental multi-signing endpoint. It adds one signer's contribution to an in-progress multi-signed transaction. The function parses the `account` field, constructs `SigningForParams` with that account ID, calls `transactionPreProcessImpl` (which deposits the computed signature into `signForParams`), then injects a new `STObject` Signer entry into the `sfSigners` array. After each injection, `sortAndValidateSigners()` sorts the array by `AccountID` (a protocol requirement for signature aggregation) and rejects duplicates or self-signing.

`transactionSubmitMultiSigned` handles the final submission once all signers have contributed. It does not re-sign; it validates structural correctness (empty `SigningPubKey`, no `TxnSignature`, non-zero XRP fee), validates and sorts the existing Signers array, sterilizes via `transactionConstructImpl`, and submits.

## Offline Mode

The `offline: true` request parameter bypasses all online checks: ledger staleness, account existence, and field auto-fill. In offline mode, the caller must supply `Sequence` themselves. This enables air-gapped signing workflows where the private key material never touches a connected machine.
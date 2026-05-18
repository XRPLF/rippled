# `MPTokenAuthorize.h` — Auto-Generated Transaction Wrapper for MPT Authorization

## Role in the System

This file lives in `include/xrpl/protocol_autogen/transactions/` and is part of the code-generated layer that wraps the XRPL ledger's serialized transaction types (`STTx`) into ergonomic, type-safe C++ classes. It covers transaction type `ttMPTOKEN_AUTHORIZE` (opcode 57), one of four transaction types introduced by the `featureMPTokensV1` amendment for Multi-Purpose Tokens (MPTs) — XRPL's fungible token primitive intended to supersede trust-line-based IOUs in certain use cases.

The header is explicitly annotated `// This file is auto-generated. Do not edit.`, meaning the canonical source of truth is an upstream template or spec-file; hand-edits would be overwritten on regeneration.

## The MPTokenAuthorize Transaction: Dual-Role Semantics

Before understanding the wrapper, it helps to understand what the transaction itself does. As seen in `MPTokenAuthorize.cpp`, this single transaction type serves two distinct actors depending on the presence of the optional `sfHolder` field:

- **Holder path** (no `sfHolder`): The submitting account is opting itself in or out of holding a specific MPT issuance. Submitting without the `tfMPTUnauthorize` flag creates an `MPToken` ledger object for the holder; submitting with it deletes that object (requiring a zero balance).
- **Issuer path** (`sfHolder` present): The issuer is granting or revoking allowlist authorization for a specific holder account. This path only applies when the issuance has the `lsfMPTRequireAuth` flag set, meaning the issuer controls who may hold the token at all.

This dual-role pattern is unusual — a single transaction type encoding two different authorization directions — but it keeps the MPT authorization surface compact.

## `MPTokenAuthorize`: The Immutable Read Wrapper

`MPTokenAuthorize` extends `TransactionBase`, which holds a `std::shared_ptr<STTx const>` and provides read-only accessors for all common fields (account, sequence, fee, flags, memos, signers, etc.). The derived class adds two MPT-specific accessors:

- `getMPTokenIssuanceID()` returns the 192-bit issuance identifier (`SF_UINT192`) unconditionally. This field is `soeREQUIRED`, so direct access via `tx_->at(sfMPTokenIssuanceID)` is always safe.
- `getHolder()` returns `protocol_autogen::Optional<SF_ACCOUNT::type::value_type>` — a `std::optional` wrapping — only when `hasHolder()` confirms the field is present. This mirrors the transaction's own conditional semantics: the presence of `sfHolder` is what shifts the transaction into issuer-authorization mode.

The constructor enforces type safety by calling `tx_->getTxnType()` and throwing `std::runtime_error` if it does not match `ttMPTOKEN_AUTHORIZE`. This guard is intentionally the first thing after delegating to `TransactionBase`, ensuring no caller can accidentally wrap a `Payment` or `OfferCreate` in an `MPTokenAuthorize` shell and read garbage field data.

All getters are `[[nodiscard]]` and `const`, reinforcing the immutability contract of the wrapper.

## `MPTokenAuthorizeBuilder`: CRTP Fluent Builder

`MPTokenAuthorizeBuilder` follows the Curiously Recurring Template Pattern (CRTP) through `TransactionBuilderBase<MPTokenAuthorizeBuilder>`. The base class holds a mutable `STObject object_{sfTransaction}` and exposes chainable setters for all standard fields (`setFee`, `setSequence`, `setFlags`, `setLastLedgerSequence`, etc.), each returning `Derived&` so call chains compose across both base and derived setters without narrowing the type.

The primary constructor takes the mandatory `account` and `mPTokenIssuanceID` arguments, forwarding to the base and immediately calling `setMPTokenIssuanceID()`. Sequence and fee are optional at construction time, acknowledging real-world workflows where these are filled in later (e.g., from a fee estimate RPC or after fetching account state). `setHolder()` is offered as an optional setter for the issuer-authorization case.

A secondary constructor accepts an existing `std::shared_ptr<STTx const>` and copies it into `object_`, enabling round-trip editing: deserialize a transaction from the network, wrap it in a builder, modify fields, and re-sign. The same type guard applies here.

`build(publicKey, secretKey)` finalizes construction by calling the protected `sign()` helper in `TransactionBuilderBase`. That method serializes the `STObject` without signing fields, prepends the `HashPrefix::txSign` prefix, signs with the provided key pair, and embeds both `sfSigningPubKey` and `sfTxnSignature` into the object before it is moved into a freshly constructed `STTx` and wrapped in the immutable `MPTokenAuthorize` type.

## Design Decisions Worth Noting

**Why `std::decay_t<typename SF_UINT192::type::value_type>`?** The setter signatures use `std::decay_t` to strip reference and cv-qualifiers from the field's native value type. This prevents accidental binding of temporaries by value and keeps the setter signature consistent regardless of whether the underlying `STField` value type is itself a reference type. Passing by `const&` after decay is the canonical pattern across all generated setters in this directory.

**Why not expose `sfHolder` as required in the constructor?** Because the two use-cases — holder opt-in and issuer allowlist — are fundamentally different roles. Forcing `sfHolder` in the constructor would either make the holder opt-in case awkward (passing a dummy value) or require two separate builder types. The design accepts the mild ambiguity of an optional field in exchange for a unified builder surface.

**Auto-generation rationale**: The `protocol_autogen` directory contains one file per transaction type, all following the same structural template. Rather than maintaining 70+ near-identical wrapper classes by hand, the codebase generates them from a specification. Any deviation (field name, optionality, field type) is centrally controlled and reflected everywhere at once. The `// This file is auto-generated. Do not edit.` guard communicates this to contributors at a glance.
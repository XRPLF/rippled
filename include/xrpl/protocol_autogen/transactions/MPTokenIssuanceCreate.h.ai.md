# `MPTokenIssuanceCreate.h` — Auto-generated MPT Issuance Creation Transaction

## Role and Context

This file is part of the `protocol_autogen` layer — a code-generated set of strongly-typed transaction wrappers and builders that sit on top of the XRPL core `STTx` serialized-type system. It defines the typed interface for `MPTokenIssuanceCreate`, the transaction that bootstraps a Multi-Purpose Token (MPT) class on the XRP Ledger. MPTs are the `featureMPTokensV1` amendment's token standard — distinct from XLS-20 NFTs — providing an issuer-controlled fungible token with configurable supply caps, transfer fees, and metadata. Before any account can hold an MPT, its issuance object must be created on-ledger by this transaction type (`ttMPTOKEN_ISSUANCE_CREATE`, type code 54).

The header contains two classes: `MPTokenIssuanceCreate`, a read-only view of an already-constructed `STTx`, and `MPTokenIssuanceCreateBuilder`, a fluent builder that assembles and signs new transactions. This split is the same pattern repeated across all ~70 autogen transaction headers in this directory, including the companion `MPTokenIssuanceDestroy.h` and `MPTokenIssuanceSet.h`.

## `MPTokenIssuanceCreate` — Immutable Wrapper

`MPTokenIssuanceCreate` inherits from `TransactionBase`, which owns a `shared_ptr<STTx const>` named `tx_`. Immutability is enforced by the `const`-qualified pointer — no field can be changed through this class. The constructor takes an existing `shared_ptr<STTx const>` and immediately validates that `tx_->getTxnType() == ttMPTOKEN_ISSUANCE_CREATE`, throwing `std::runtime_error` on mismatch. This fail-fast guard ensures the wrapper never silently misrepresents a different transaction type, which matters because `STTx` fields are accessed by index without further type checking.

All six transaction-specific fields are **optional** (`soeOPTIONAL`). This is architecturally significant: unlike sibling operations `MPTokenIssuanceDestroy` and `MPTokenIssuanceSet`, which require an `sfMPTokenIssuanceID` to identify an existing issuance, the create transaction identifies the target by the sender's account and a derived ledger object ID — so no field is strictly required beyond the common account/sequence/fee fields inherited from `TransactionBase`.

Each field follows a paired accessor pattern: a `has*()` predicate backed by `isFieldPresent()`, and a `get*()` that returns `protocol_autogen::Optional<T>`. The `Optional<T>` alias (defined in `Utils.h`) handles a subtlety: for value types it is `std::optional<T>`, but for reference types it wraps in `std::optional<std::reference_wrapper<...>>` to avoid returning dangling references to temporaries. All getters guard the field access through the corresponding `has*()` check before calling `tx_->at(...)`, preventing `STObject` from throwing on absent optional fields.

The six MPT-specific fields control the economics and governance of the token class:

- **`sfAssetScale`** (`uint8`) — decimal precision, analogous to ERC-20 `decimals()`. Determines how raw integer amounts map to human-readable values.
- **`sfTransferFee`** (`uint16`) — basis points deducted on every transfer and credited back to the issuer. Encoded as an integer (e.g., `1000` = 1%).
- **`sfMaximumAmount`** (`uint64`) — hard cap on total outstanding supply. If absent, the supply is unlimited.
- **`sfMPTokenMetadata`** (`VL` blob) — arbitrary byte payload, typically a URI or a hash pointing to off-ledger metadata.
- **`sfDomainID`** (`uint256`) — a reference to a `PermissionedDomain` object that governs who may hold this token, enabling compliance-constrained tokens.
- **`sfMutableFlags`** (`uint32`) — a subset of issuance flags that the issuer retains the right to change after creation, as opposed to the immutable `sfFlags` set at creation time.

## `MPTokenIssuanceCreateBuilder` — Fluent Builder

The builder inherits from `TransactionBuilderBase<MPTokenIssuanceCreateBuilder>` using CRTP, which causes all common setter methods (`setFee()`, `setSequence()`, `setFlags()`, etc.) to return `MPTokenIssuanceCreateBuilder&` rather than the base type, enabling unbroken fluent chains without casts at the call site.

Internally the builder holds a plain `STObject object_{sfTransaction}` — importantly, the `TransactionBuilderBase` constructor deliberately does **not** call `object_.set(soTemplate)`. This avoids creating `STBase` placeholders for `soeDEFAULT` fields, which would cause `applyTemplate()` to throw "may not be explicitly set to default" when the `STTx` constructor later enforces the schema. The builder accumulates fields freely into the bare `STObject`, and the `STTx` constructor applies the template at construction time.

The builder has two construction paths: starting fresh from account/sequence/fee, or round-tripping from an existing `shared_ptr<STTx const>` by copying `*tx` into `object_`. The copy path is guarded by the same type check as the read-only wrapper, ensuring consistency. The `build()` method calls `sign()` (which computes `sfSigningPubKey` and `sfTxnSignature` over a `HashPrefix::txSign`-prefixed serialization), then wraps the result in a `shared_ptr<STTx>` and hands it to the `MPTokenIssuanceCreate` constructor.

## Relationship to the Autogen Layer

The file opens with `// This file is auto-generated. Do not edit.` — it is produced from a schema description of the XRPL transaction format, not written by hand. Every transaction type in the `protocol_autogen/transactions/` directory follows the same structural template, making the layer a machine-maintainable typing layer over the dynamically-typed `STTx` core. The absence of required fields here (versus mandatory `sfMPTokenIssuanceID` on destroy/set operations) is a direct consequence of the schema: issuance creation targets an account, not a pre-existing ledger object.
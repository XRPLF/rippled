# `MPTokenIssuanceSet.h` — Auto-Generated Transaction Wrapper

## Role and Context

This header defines the type-safe C++ interface for the `MPTokenIssuanceSet` transaction (`ttMPTOKEN_ISSUANCE_SET`, type code 56), one of the four transaction types that constitute the MPTokens lifecycle under the `featureMPTokensV1` amendment. Where `MPTokenIssuanceCreate` brings a new multi-purpose token issuance into existence, `MPTokenIssuanceSet` modifies the mutable properties of an already-created issuance — changing its transfer fee, associating it with a compliance domain, updating its metadata, or toggling its `sfMutableFlags`. The file is auto-generated and must not be edited by hand; it follows the same structural template as every other transaction header in `include/xrpl/protocol_autogen/transactions/`.

## Dual-Class Design

The file exposes two cooperating classes in the `xrpl::transactions` namespace:

**`MPTokenIssuanceSet`** is a read-only, immutable wrapper around a `std::shared_ptr<STTx const>`. It inherits from `TransactionBase`, which holds the shared pointer and exposes accessors for universal transaction fields (`sfAccount`, `sfFee`, `sfSequence`, `sfDelegate`, etc.). `MPTokenIssuanceSet` adds getters specific to this transaction type and enforces the type invariant in its constructor by calling `tx_->getTxnType()` and throwing `std::runtime_error` if the discriminant doesn't match `ttMPTOKEN_ISSUANCE_SET`. This eager validation means any code that holds an `MPTokenIssuanceSet` value is statically guaranteed to be looking at the right transaction type — no further runtime checks needed in consuming code.

**`MPTokenIssuanceSetBuilder`** is a mutable builder that extends `TransactionBuilderBase<MPTokenIssuanceSetBuilder>` via CRTP. The base class holds an `STObject object_{sfTransaction}` and provides setters for all common fields, each returning `Derived&` so that call sites can chain `.setFee(...).setLastLedgerSequence(...)`. The derived builder adds `MPTokenIssuanceSet`-specific setters on top. Critically, `build(PublicKey, SecretKey)` is the only path to producing a signed `MPTokenIssuanceSet` value: it calls the inherited `sign()` method (which serializes the object with `HashPrefix::txSign`, signs it, and embeds `sfSigningPubKey` and `sfTxnSignature`), then wraps the moved `STObject` into a `shared_ptr<STTx>` and constructs the immutable wrapper. There is no way to accidentally skip signing while using this API.

## Field Inventory

The single required field is `sfMPTokenIssuanceID`, typed as `SF_UINT192::type::value_type` — a 192-bit identifier that uniquely addresses an MPT issuance on the ledger. Because it is required, the primary builder constructor takes it as a mandatory argument and immediately calls `setMPTokenIssuanceID()`, ensuring no builder can exist in a state where the target issuance is unspecified.

All remaining fields are optional and carry `has*` / `get*` paired accessors on the read side:

- **`sfHolder`** (`SF_ACCOUNT`) — when present, scopes the operation to a specific token holder's balance object rather than the issuance itself. This enables the issuer to lock or unlock a specific holder's position, which is the per-holder authorization flow distinct from setting global issuance properties.
- **`sfDomainID`** (`SF_UINT256`) — associates or re-associates the issuance with a permissioned domain for compliance purposes.
- **`sfMPTokenMetadata`** (`SF_VL`) — arbitrary variable-length blob for off-chain metadata (e.g., a URI or hash). The `SF_VL` type means the getter returns `protocol_autogen::Optional<SF_VL::type::value_type>`; if the field value type were a reference, the `protocol_autogen::Optional<T>` alias would transparently wrap it in `std::reference_wrapper` to keep it storable in `std::optional`.
- **`sfTransferFee`** (`SF_UINT16`) — the per-transfer fee rate in units of 1/100,000 of the transferred amount. This field appearing on both `MPTokenIssuanceCreate` and `MPTokenIssuanceSet` reflects that transfer fees are a mutable property of an issuance — the issuer can adjust them after creation.
- **`sfMutableFlags`** (`SF_UINT32`) — a bitmask of flags that are permitted to change post-creation (in contrast to the immutable flags baked in at creation time). Allowing a dedicated mutable flags field rather than reusing `sfFlags` avoids ambiguity between transaction control flags and issuance-state flags.

## Optional Getter Pattern

Every optional field follows the same three-line pattern: `has*()` calls `tx_->isFieldPresent(sf*)`, and `get*()` delegates to `has*()` before calling `tx_->at(sf*)`, returning `std::nullopt` when absent. All getters are `[[nodiscard]]` and `const`. This guards against raw `at()` calls that would throw on missing optional fields while keeping the interface self-documenting about which fields are required versus conditional.

## Delegability

The transaction is marked `Delegation::delegable`, meaning a token holder or issuer can authorize another account to submit this transaction on their behalf by setting `sfDelegate`. The `getDelegate()` accessor for that field lives in `TransactionBase`, so it is uniformly available on all delegable transaction types without any per-type boilerplate.

## Relationship to Sibling Files

`MPTokenIssuanceSet.h` sits alongside `MPTokenIssuanceCreate.h`, `MPTokenIssuanceDestroy.h`, and `MPTokenAuthorize.h` as the full MPToken transaction family. The builder second-constructor overload — `MPTokenIssuanceSetBuilder(std::shared_ptr<STTx const> tx)` — exists so that a transaction received from the network (e.g., fetched from ledger history) can be deserialized, copied into the mutable `STObject`, and re-signed with different parameters. This round-trip pattern is shared by all auto-generated builders in the directory and enables tooling that needs to repackage or replay existing transactions.
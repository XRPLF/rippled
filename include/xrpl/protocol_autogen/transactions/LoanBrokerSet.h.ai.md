# `LoanBrokerSet.h` — Auto-generated Transaction Wrapper for the Lending Protocol

## Role and Context

This file is part of the `protocol_autogen` layer — a set of machine-generated C++ headers that wrap XRPL's generic `STTx`/`STObject` machinery in type-safe, self-documenting classes. It resides in `include/xrpl/protocol_autogen/transactions/` alongside analogous files for every other transaction kind in the ledger.

`LoanBrokerSet` is transaction type `ttLOAN_BROKER_SET` (74), introduced under the `featureLendingProtocol` amendment. Its job is twofold: it both **creates** a new `LoanBroker` ledger object and **updates** an existing one, with the mode determined by the presence or absence of the optional `sfLoanBrokerID` field. A `LoanBroker` acts as a managed entity that sits between a `Vault` (a pooled-asset lending vault) and individual borrowers, controlling per-broker debt limits, fee rates, and collateral coverage thresholds. Because it requires the ability to spawn a pseudo-account and may authorize MPToken issuances, the transaction carries `createPseudoAcct | mayAuthorizeMPT` privileges, and it is explicitly marked `notDelegable`.

## Class Structure

The file follows the standard pattern used across all `protocol_autogen` transaction headers: a paired `LoanBrokerSet` (read-only wrapper) and `LoanBrokerSetBuilder` (fluent construction).

### `LoanBrokerSet : TransactionBase`

`TransactionBase` holds a `std::shared_ptr<STTx const>` called `tx_` and exposes accessors for every common transaction field (`sfAccount`, `sfSequence`, `sfFee`, `sfFlags`, `sfSigners`, etc.). `LoanBrokerSet` extends this with accessors for the six fields specific to this transaction type.

The constructor takes a pre-existing `shared_ptr<STTx const>` and immediately validates `tx_->getTxnType() == ttLOAN_BROKER_SET`, throwing `std::runtime_error` on mismatch. This is necessary because `STTx` is a single, type-erased container for every transaction variant — the check converts a latent runtime hazard into an immediate, diagnosable failure at the construction site.

Each field is exposed as a `[[nodiscard]]` `get*()` method and, for optional fields, a companion `has*()` predicate:

- **`getVaultID()`** — Returns `SF_UINT256::type::value_type` (a `uint256`). This field is `soeREQUIRED` in the schema; every `LoanBrokerSet` must target a specific vault.
- **`getLoanBrokerID()` / `hasLoanBrokerID()`** — Optional `uint256`. When present, identifies the existing `LoanBroker` object to modify. When absent, the transaction is in *create* mode.
- **`getData()` / `hasData()`** — Optional `SF_VL` blob. Arbitrary metadata the operator can attach to the broker (subject to a maximum byte length enforced in `preflight`).
- **`getManagementFeeRate()` / `hasManagementFeeRate()`** — Optional `uint16`. A percentage-style rate the broker charges; valid only during creation — the transactor's `preflight` rejects this field if `sfLoanBrokerID` is also present, making `sfManagementFeeRate` effectively immutable after creation.
- **`getDebtMaximum()` / `hasDebtMaximum()`** — Optional `SF_NUMBER`. A ceiling on the aggregate outstanding debt this broker can hold. Unlike the rate fields, `sfDebtMaximum` is updatable but constrained: `preclaim` in the transactor prevents lowering it below the current `sfDebtTotal`.
- **`getCoverRateMinimum()` / `hasCoverRateMinimum()`** — Optional `uint32`. The minimum collateral-to-debt ratio before new loans may be issued. Creation-only, like the management fee.
- **`getCoverRateLiquidation()` / `hasCoverRateLiquidation()`** — Optional `uint32`. The collateral ratio at which positions become eligible for liquidation. Must always be specified alongside `sfCoverRateMinimum` (both zero or both non-zero), also creation-only.

The `[[nodiscard]]` attribute on every accessor is deliberate: callers that ignore a returned optional silently lose the field value, which is almost always a logic bug.

### `LoanBrokerSetBuilder : TransactionBuilderBase<LoanBrokerSetBuilder>`

`TransactionBuilderBase` is a CRTP template that holds a mutable `STObject object_{sfTransaction}` and returns `Derived&` from every setter, enabling fluent chaining. Deliberately, the base constructor does *not* call `object_.set(soTemplate)` — keeping the `STObject` in "free" mode avoids pre-populating default-valued `STBase` placeholders that would later trigger `applyTemplate()`'s "may not be explicitly set to default" assertion when the `STTx` constructor processes the object.

`LoanBrokerSetBuilder` requires `account` and `vaultID` at construction time (enforcing the schema's `soeREQUIRED` constraint at the C++ level) while accepting optional `sequence` and `fee`. A second constructor accepts an existing `shared_ptr<STTx const>` and copies its `STObject` representation, enabling round-trip editing of a transaction that was previously built or deserialized.

The `build(publicKey, secretKey)` method finalizes construction: it calls the protected `sign()` method inherited from `TransactionBuilderBase`, which serializes the `STObject` (excluding signing fields) with the `HashPrefix::txSign` prefix, computes the ECDSA/Ed25519 signature, embeds both `sfSigningPubKey` and `sfTxnSignature` into the object, then moves the signed `STObject` into a freshly allocated `STTx`, wraps it in a `shared_ptr`, and constructs the immutable `LoanBrokerSet` wrapper.

## Dual-Mode Semantics and Immutability Design

The create-vs-update duality encoded in `sfLoanBrokerID` is a notable design choice. An alternative would be separate `LoanBrokerCreate` and `LoanBrokerUpdate` transactions, but the single-transaction approach saves a transaction type slot and keeps the related logic co-located in the transactor. The price is a slightly more complex preflight — the transactor must explicitly check which fields are legal in each mode and reject cross-mode combinations.

The immutability of `sfManagementFeeRate`, `sfCoverRateMinimum`, and `sfCoverRateLiquidation` after creation is enforced only at the transactor layer (`LoanBrokerSet.cpp`), not at the `STTx` schema layer. The `LoanBrokerSetBuilder` will happily accept these fields in update mode; it is the `preflight` check that rejects them. This is a standard XRPL pattern — the `protocol_autogen` wrappers describe *what* can be present structurally, while the transactor enforces *when* it is permissible.

## Relationship to Other Files

`LoanBrokerSet` integrates into a family of Lending Protocol transactions: `LoanBrokerDelete`, `LoanBrokerCoverDeposit`, `LoanBrokerCoverWithdraw`, and `LoanBrokerCoverClawback`. The corresponding `LoanBroker` ledger entry (in `ledger_entries/LoanBroker.h`) holds the fields that `LoanBrokerSet` initializes or updates, including `sfDebtTotal`, `sfCoverAvailable`, `sfLoanSequence`, and the pseudo-account's `sfAccount` field — none of which appear in the transaction schema because they are managed exclusively by the ledger engine, not by the submitting account.
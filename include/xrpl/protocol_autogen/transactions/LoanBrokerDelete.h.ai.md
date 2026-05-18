# `LoanBrokerDelete.h` — Auto-Generated Transaction Wrapper for `ttLOAN_BROKER_DELETE`

## Role and Context

This file lives in `include/xrpl/protocol_autogen/transactions/` alongside a family of similarly structured headers — one per XRPL transaction type. It is machine-generated (the opening comment warns against manual edits) and exposes two cooperating classes: `LoanBrokerDelete`, a read-only wrapper around an existing `STTx`, and `LoanBrokerDeleteBuilder`, a fluent builder that constructs and signs new transactions of this type.

`LoanBrokerDelete` represents transaction type `ttLOAN_BROKER_DELETE` (numeric code 75), introduced under the `featureLendingProtocol` amendment. Its purpose within the lending protocol is to remove an existing LoanBroker ledger object, identified by its 256-bit hash (`sfLoanBrokerID`). Comparing it to the sibling `LoanBrokerSet` (type 74), which creates a broker and carries a rich set of optional configuration fields (`sfVaultID`, `sfDebtMaximum`, `sfManagementFeeRate`, etc.), the delete transaction is intentionally minimal: the only transaction-specific field is the broker's ID. This mirrors the general XRPL pattern where creation transactions are richer and deletion transactions carry just enough to identify the object to remove.

## Design: Two-Class Split (Wrapper + Builder)

The file enforces a sharp read/write boundary through two separate classes rather than a single mutable type:

`LoanBrokerDelete` is immutable. It holds a `std::shared_ptr<STTx const>` inherited from `TransactionBase` and exposes only `[[nodiscard]]` const accessors. Callers cannot modify a `LoanBrokerDelete` after construction. The constructor validates the transaction type immediately and throws `std::runtime_error` if it doesn't match `ttLOAN_BROKER_DELETE` — this is the first line of defense against accidentally wrapping the wrong wire-format object.

`LoanBrokerDeleteBuilder` is mutable. It inherits from `TransactionBuilderBase<LoanBrokerDeleteBuilder>` via the Curiously Recurring Template Pattern (CRTP), which lets the base class's common setters (`setFee()`, `setSequence()`, `setMemos()`, etc.) return a `LoanBrokerDeleteBuilder&` instead of a base-class reference, preserving the fluent-chaining interface. The builder holds a plain `STObject object_{sfTransaction}` internally — not wrapped in `const` — so fields can be mutated freely until `build()` is called.

The terminal step `build(publicKey, secretKey)` calls `sign()` (inherited from the base), which serializes the object with `HashPrefix::txSign`, signs it with the secret key, embeds the signature and public key into `object_`, then moves `object_` into a freshly constructed `STTx` and wraps it in a `LoanBrokerDelete`. At that point the signed transaction becomes immutable and the builder's `object_` is moved away.

## Key Design Decisions

**Required field enforced at construction.** `sfLoanBrokerID` is `soeREQUIRED` in the schema. The builder constructor takes it as a mandatory parameter (not `std::optional`), so a `LoanBrokerDeleteBuilder` cannot exist in a state where the field is absent. In contrast, `sequence` and `fee` are optional at construction time because they may be auto-populated by server-side logic in some submission paths.

**`std::decay_t` for the 256-bit ID parameter.** The setter and constructor accept `std::decay_t<typename SF_UINT256::type::value_type> const&` rather than the raw `value_type` directly. This strips reference and cv-qualifiers from the template argument, ensuring a consistent value-category regardless of how the underlying `SF_UINT256` type resolves — a defensive measure against implicit reference collisions in generic code.

**`mustDeleteAcct` privilege flag.** Unlike `LoanBrokerSet`, which carries `createPseudoAcct | mayAuthorizeMPT`, `LoanBrokerDelete` is marked `mustDeleteAcct | mayAuthorizeMPT`. The `mustDeleteAcct` constraint means the transaction must reclaim the owner reserve held by the broker's ledger object — a restriction enforced at the transaction-application layer, not here. The wrapper layer is agnostic to these semantic rules; they exist in the amendment's apply logic.

**Not delegable.** The `Delegation::notDelegable` annotation means this transaction cannot be submitted through the `sfDelegate` field mechanism. The `TransactionBase` base class still exposes `getDelegate()` and `hasDelegate()` for completeness (they are part of every transaction's potential field set), but the ledger will reject any `LoanBrokerDelete` that includes a non-empty delegate.

## Relationship to Sibling Classes

The five `LoanBroker*` transaction headers form the full lifecycle of a loan broker:

- `LoanBrokerSet.h` (type 74) — create or modify a broker configuration
- `LoanBrokerDelete.h` (type 75) — remove a broker
- `LoanBrokerCoverDeposit.h`, `LoanBrokerCoverWithdraw.h`, `LoanBrokerCoverClawback.h` — manage collateral cover positions associated with a broker

All five follow the same two-class pattern and share the same `TransactionBase` / `TransactionBuilderBase` infrastructure. The auto-generated nature of these files is architecturally significant: any change to the lending protocol schema (a new field, a changed optionality) is reflected by regenerating the file rather than requiring hand-edits, keeping the C++ API in lock-step with the protocol definition.

## Error Handling

Both constructors (on `LoanBrokerDelete` and `LoanBrokerDeleteBuilder`) perform an immediate type check and throw `std::runtime_error` on mismatch. `TransactionBase::validate()` can be called on the wrapper after construction to run the full schema check via `validateSTObject()` and `passesLocalChecks()`, catching field-level constraint violations that the constructor does not inspect.
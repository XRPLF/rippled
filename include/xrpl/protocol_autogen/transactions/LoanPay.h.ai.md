# `LoanPay.h` — Loan Payment Transaction for the XRPL Lending Protocol

This file is an **auto-generated** header (do not edit manually) that defines the `LoanPay` transaction type within the `xrpl::transactions` namespace. It is one of a suite of transaction types introduced by the `featureLendingProtocol` amendment, providing the on-ledger DeFi lending lifecycle on XRPL. `LoanPay` represents the payment side of that lifecycle: a borrower directing funds toward an outstanding loan.

## Role in the Lending Protocol

The `featureLendingProtocol` amendment introduces at minimum five related transaction types grouped in the `protocol_autogen/transactions/` directory: `LoanSet` (tt 80, loan origination), `LoanManage` (tt 82, lifecycle management), `LoanPay` (tt 84, repayment), `LoanDelete` (loan closure), and a family of `LoanBroker*` transactions for the broker entity. `LoanPay` sits specifically at transaction type `ttLOAN_PAY` (code 84) and carries two system-level privilege flags — `mayAuthorizeMPT | mustModifyVault` — reflecting that a repayment both requires authorization to move Multi-Purpose Tokens and must atomically update the lending vault's state. This same privilege combination appears on `LoanSet` but not on the simpler `LoanManage`, which only holds `mayModifyVault`. That asymmetry makes sense: creating a loan and repaying one both move tokens through vault accounting, while managing loan terms does not.

The `Delegation::notDelegable` marker means no third party can submit this transaction on behalf of the originating account via the delegation mechanism — the borrower must sign directly.

## Class Structure: Wrapper and Builder

The file follows the pattern used uniformly across all `protocol_autogen` transaction types: a paired **immutable read-only wrapper** (`LoanPay`) and a **fluent mutable builder** (`LoanPayBuilder`). This separation is architecturally deliberate. Transactions on XRPL are value objects once serialized — immutability at the wrapper layer prevents accidental mutation after signing, which would invalidate the signature. The builder, by contrast, accumulates field assignments into an `STObject` before the transaction is finalized and signed.

## `LoanPay` — The Immutable Wrapper

`LoanPay` inherits from `TransactionBase`, which holds a `std::shared_ptr<STTx const>` and provides accessors for all universal transaction fields (`sfAccount`, `sfSequence`, `sfFee`, `sfFlags`, `sfMemos`, multi-signing fields, etc.). `LoanPay` adds only two transaction-specific accessors:

- `getLoanID()` — returns the `sfLoanID` as a `uint256` value, identifying which on-ledger loan object this payment targets.
- `getAmount()` — returns the `sfAmount` field, annotated explicitly as supporting MPT (Multi-Purpose Token) amounts. This is significant: the lending protocol is built around MPTs, XRPL's newer extensible token standard, so repayment amounts may be denominated in an MPT rather than XRP or an IOU.

The constructor performs an eager type-check: if the wrapped `STTx` does not carry `ttLOAN_PAY`, it throws `std::runtime_error` immediately. This guards against misuse of a generic `STTx` pointer to construct a `LoanPay` from a different transaction type — a defensive pattern applied identically across the entire `protocol_autogen` family.

Both getters are marked `[[nodiscard]]` and `const`, reinforcing the immutable contract.

## `LoanPayBuilder` — The Fluent Builder

`LoanPayBuilder` inherits from `TransactionBuilderBase<LoanPayBuilder>`, which uses CRTP (Curiously Recurring Template Pattern) so that setters inherited from the base return a `LoanPayBuilder&` rather than a `TransactionBuilderBase&`, preserving the fluent method-chaining interface without virtual dispatch overhead.

The builder has two construction paths:

1. **From scratch**: Takes `account`, `loanID`, `amount`, and optionally `sequence` and `fee`. The sequence and fee are optional because they may be auto-populated by a node on submission. Internally, the constructor delegates to `TransactionBuilderBase` (which sets `sfTransactionType`, `sfAccount`, and optionally `sfSequence`/`sfFee`), then calls `setLoanID()` and `setAmount()` immediately to satisfy the required-field invariant before `build()` is called.

2. **From an existing `STTx`**: Takes a `std::shared_ptr<STTx const>` and copies its `STObject` content into `object_`. This path enables round-tripping — reconstructing a builder from a previously signed or deserialized transaction, for example when resubmitting or inspecting a failed transaction. It performs the same type-check guard and throws on mismatch.

The `build()` method calls the protected `sign()` from `TransactionBuilderBase`, which serializes the accumulated `STObject` with `HashPrefix::txSign`, signs the result with the provided `PublicKey`/`SecretKey` pair, sets both `sfSigningPubKey` and `sfTxnSignature` on the object, then constructs a `std::shared_ptr<STTx>` and wraps it in a `LoanPay` instance. The `STObject` is moved into the `STTx`, so the builder is left in a valid-but-consumed state after `build()`.

## Design Notes

The use of `std::decay_t<typename SF_UINT256::type::value_type>` for `loanID` rather than a plain `uint256` is defensive: it strips reference and cv-qualifiers from whatever the `SField` type alias exposes, ensuring no accidental implicit conversions or dangling references when the value is stored into the `STObject`. This pattern appears consistently across every autogenerated builder in the module.

The comment in `TransactionBuilderBase` explicitly warns against calling `object_.set(soTemplate)` to pre-populate the `STObject` with default field placeholders, because doing so would cause `STTx::applyTemplate()` to throw on fields with `soeDEFAULT` cardinality. The builder deliberately leaves `object_` as a free `STObject{sfTransaction}` and lets the `STTx` constructor handle template application — a non-obvious but critical design constraint that this generated file inherits without needing to document locally.
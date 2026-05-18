# `include/xrpl/protocol_autogen/transactions/LoanManage.h`

This file is part of the auto-generated transaction layer introduced by the `featureLendingProtocol` amendment. It defines two cooperating classes — `LoanManage` and `LoanManageBuilder` — that together provide the complete lifecycle interface for the `ttLOAN_MANAGE` (type 82) transaction: a read-only, type-safe wrapper for inspection and a fluent builder for construction and signing.

## Role in the LendingProtocol System

`LoanManage` is the administrative lifecycle transaction for loans originated through the XLS-66 Lending Protocol. While `LoanSet` creates a loan and `LoanPay` services it, `LoanManage` is the mechanism through which a Loan Broker formally changes the *health status* of a loan — marking it as impaired, unimpaired, or in default. This transaction may only be submitted by the account that owns the `LoanBroker` object associated with the loan; neither the borrower nor any third party can submit it, and it is explicitly marked `notDelegable`, preventing delegation to other accounts even through the `sfDelegate` mechanism.

The minimal transaction schema — a single required field, `sfLoanID` — is deliberate. The actual operation is encoded entirely in the transaction's flags (`tfLoanDefault`, `tfLoanImpair`, `tfLoanUnimpair`), which are mutually exclusive. This design keeps the ledger-level record slim while making the intent unambiguous. A `LoanManage` submitted with no flags is a valid no-op.

## `LoanManage` — Immutable Wrapper

`LoanManage` inherits from `TransactionBase`, which holds a `std::shared_ptr<STTx const>` and exposes type-safe getters for all universal transaction fields (`sfAccount`, `sfSequence`, `sfFee`, `sfFlags`, `sfDelegate`, etc.). The subclass adds only `getLoanID()`, which returns the `uint256` value of `sfLoanID` directly from the underlying `STTx` via `tx_->at(sfLoanID)`.

Construction validates the transaction type at runtime: if the passed `STTx` is not a `ttLOAN_MANAGE`, the constructor throws `std::runtime_error`. This check is intentional because `STTx` objects travel the system as type-erased shared pointers, and the wrapper must guarantee its static type assumption before exposing typed accessors. The `[[nodiscard]]` attribute on `getLoanID()` follows the pattern used uniformly across this layer to discourage silently discarded values.

## `LoanManageBuilder` — Fluent Construction

`LoanManageBuilder` inherits from `TransactionBuilderBase<LoanManageBuilder>`, a CRTP template that supplies setters for all universal fields and stores state in an `STObject object_` member. Returning `Derived&` from every setter enables method chaining without virtual dispatch overhead.

The primary constructor takes `account` and `loanID` as required arguments, with `sequence` and `fee` as `std::optional` parameters defaulting to `std::nullopt`. This reflects real-world usage where sequence and fee are often provided separately (e.g., resolved from account state by a client SDK). `setLoanID()` uses `std::decay_t<typename SF_UINT256::type::value_type>` to strip references and cv-qualifiers from the value type, ensuring the stored `object_` field receives a copy rather than a dangling reference.

A secondary constructor accepts an existing `std::shared_ptr<STTx const>` to allow round-tripping: copying a previously serialized transaction back into a builder for modification. It performs the same type guard as the `LoanManage` wrapper's constructor.

The `build()` method calls the protected `sign()` helper from `TransactionBuilderBase`, which serializes the `STObject` without signing fields, prepends the `HashPrefix::txSign` tag, computes the signature, and stores it in `sfTxnSignature`. The object is then moved into an `STTx` (whose constructor calls `applyTemplate()` to validate the field set against the registered `TxFormats` schema) and wrapped in an immutable `LoanManage`. The fact that the builder's `object_` is **not** pre-initialized with an `SOTemplate` is a deliberate choice documented in `TransactionBuilderBase`: pre-initializing would create `soeDEFAULT` placeholder fields that cause `applyTemplate()` to throw when it encounters them as explicitly set.

## Transactor Behavior (from `LoanManage.cpp`)

The header's thin interface belies the financial complexity of the underlying transactor. `preflight()` rejects a zero-valued `sfLoanID` and enforces mutual exclusivity of the three flags via a bitmask check (`flags & (flags - 1)` is non-zero if more than one bit is set). `preclaim()` verifies the loan exists, looks up the associated `LoanBroker`, and confirms the submitting account is the broker owner. It also enforces a one-way state machine: a defaulted loan can never be modified; an impaired loan cannot be impaired again; an unimpaired loan cannot be unimpaired.

In `doApply()`, the flag determines which of three internal methods executes:

- **`impairLoan()`** books an unrealized loss on the `Vault` (`sfLossUnrealized`), sets `lsfLoanImpaired` on the Loan ledger object, and advances `sfNextPaymentDueDate` to now if the payment isn't already overdue — effectively starting the grace-period clock.
- **`unimpairLoan()`** reverses the unrealized loss, clears `lsfLoanImpaired`, and recalculates the next payment due date based on the payment interval.
- **`defaultLoan()`** applies the Loan Broker's first-loss capital (bounded by `sfCoverRateLiquidation` and `sfCoverRateMinimum`) against the amount owed to the vault, transfers those funds from the broker's pseudo-account back to the vault's pseudo-account, zeroes out all outstanding amounts on the Loan object, and sets `lsfLoanDefault` — a terminal state.

## Design Observations

The separation between the header-level wrapper/builder pair and the transactor implementation is characteristic of the whole `protocol_autogen` layer: the generated files handle field access and construction in a type-safe, schema-driven way, while the `tx/transactors/lending/` implementation handles business rules. The auto-generated comment (`// This file is auto-generated. Do not edit.`) signals that the field layout, accessor signatures, and builder constructors are derived from a schema definition rather than hand-authored — edits must be made upstream in that schema.

The `mayModifyVault` privilege annotation in the class Doxygen is meaningful at the amendment-enforcement layer: it allows the transaction to write to `Vault` objects it does not own, which is necessary because impairment and default must update the vault's `sfLossUnrealized` and `sfAssetsTotal` accounting fields even though the vault's owner is a separate account from the Loan Broker submitting the transaction.
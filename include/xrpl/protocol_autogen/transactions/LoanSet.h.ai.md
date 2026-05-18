# `LoanSet.h` — Lending Protocol Loan Origination Transaction

## Role in the System

This auto-generated header is part of the `featureLendingProtocol` amendment on the XRP Ledger and defines the `ttLOAN_SET` transaction (type code 80). A `LoanSet` transaction originates a new loan, binding a borrower to a registered loan broker and encoding the full economic terms of the loan — principal, interest rates, fee schedules, and repayment cadence — as on-ledger, consensus-validated data.

The file is one of roughly seventy transaction wrappers in `include/xrpl/protocol_autogen/transactions/`. All of them follow the same machine-generated structure: an immutable wrapper class and a companion builder class, produced to enforce a consistent, type-safe API across the protocol layer.

## Class Structure

### `LoanSet` — Immutable Accessor

`LoanSet` inherits from `TransactionBase`, which holds a `std::shared_ptr<STTx const>` and provides read-only accessors for fields common to all transactions (`sfAccount`, `sfSequence`, `sfFee`, `sfSigners`, `sfDelegate`, etc.). `LoanSet` extends this with accessors for the fourteen transaction-specific fields it declares.

The constructor performs a runtime type guard:

```cpp
if (tx_->getTxnType() != txType)
    throw std::runtime_error("Invalid transaction type for LoanSet");
```

This check is the sole defensive boundary at the `LoanSet` layer. Because `STTx` is a generic container, the wrong transaction could be passed in without a compile-time error; the runtime throw makes construction fail-fast rather than silently serving garbage field values.

Two fields are declared `soeREQUIRED` and therefore return values directly without `std::nullopt`:

- `getLoanBrokerID()` — a `uint256` identifying the registered `LoanBroker` ledger object that governs this loan's terms.
- `getPrincipalRequested()` — an `SF_NUMBER` (the ledger's arbitrary-precision numeric type) representing the loan amount.

Every other field is `soeOPTIONAL` and is accessed through paired `getX()` / `hasX()` methods. The getters return `protocol_autogen::Optional<T>`, which is a `std::optional<T>` for value types and `std::optional<std::reference_wrapper<T>>` for reference types, defined in `Utils.h`. The `hasX()` companion avoids the cost of constructing an optional when the caller only needs a presence check.

`sfCounterpartySignature` is the one outlier: it returns `std::optional<STObject>` directly rather than `protocol_autogen::Optional<...>` because it is an untyped nested object with no corresponding `SF_` template instantiation. The getter calls `getFieldObject()` rather than `at()`.

### `LoanSetBuilder` — Fluent Construction

`LoanSetBuilder` inherits from `TransactionBuilderBase<LoanSetBuilder>`, a CRTP base that holds a mutable `STObject object_{sfTransaction}` and provides setters for all standard transaction fields (`setAccount`, `setFee`, `setSequence`, `setFlags`, `setDelegate`, etc.). Each base-class setter returns `Derived&` so calls chain cleanly across both the base and derived layers without breaking the fluent interface.

The primary constructor enforces the two required fields immediately:

```cpp
LoanSetBuilder(account, loanBrokerID, principalRequested, sequence, fee)
    : TransactionBuilderBase<LoanSetBuilder>(ttLOAN_SET, account, sequence, fee)
{
    setLoanBrokerID(loanBrokerID);
    setPrincipalRequested(principalRequested);
}
```

An alternative constructor accepts an existing `STTx` and copies it into `object_` via `object_ = *tx`, enabling round-trip editing: deserialize a transaction from the network, wrap it in a builder, modify fields, and rebuild. The same type guard as the wrapper class applies here.

All setter parameters are taken as `std::decay_t<typename SF_xxx::type::value_type> const&`. The `std::decay_t` strips reference and cv-qualifiers before binding, so callers never have to worry about value-category mismatches between the field's natural storage type and what they pass in.

`setCounterpartySignature` is again the exception — it takes `STObject const&` directly and uses `setFieldObject()` on the underlying `STObject` rather than the field-indexed `operator[]`.

`build()` calls the base `sign()` method, which serializes the object with `HashPrefix::txSign` prepended, computes the signature, and stores it into `sfTxnSignature`. It then moves `object_` into a freshly constructed `STTx`, wraps it in a `shared_ptr`, and returns a `LoanSet` wrapper. After `build()` the builder's internal state has been moved out; it should not be used again.

## Economic Fields and Their Design

The optional field set encodes a structured loan product with penalty differentiation across several lifecycle states:

- **Fees** (all `SF_NUMBER`): `sfLoanOriginationFee`, `sfLoanServiceFee`, `sfLatePaymentFee`, `sfClosePaymentFee`, `sfOverpaymentFee` — covering origination costs, ongoing servicing, delinquency penalties, early-close charges, and excess-payment penalties.
- **Interest rates** (all `SF_UINT32`): `sfInterestRate`, `sfLateInterestRate`, `sfCloseInterestRate`, `sfOverpaymentInterestRate` — allowing different rate tiers depending on payment status.
- **Repayment schedule** (`SF_UINT32`): `sfPaymentTotal` (total number of installments), `sfPaymentInterval` (time between payments), `sfGracePeriod` (allowable delay before a payment is considered late).

The separation of fees from rates — and the mirroring of each penalty category in both a fee and a rate field — reflects a design where each economic scenario (normal, late, close-out, overpayment) can be parameterized independently, giving broker implementations fine-grained control over loan product terms.

## Counterparty Signature Pattern

The presence of both `sfCounterparty` and `sfCounterpartySignature` indicates a bilateral origination flow. When a loan requires counterparty authorization — for instance, if an institutional lender must pre-approve the terms — the borrower includes the counterparty's account address and an `STObject` carrying their cryptographic signature. The ledger can then verify bilateral consent before applying the transaction. This is consistent with the `mayAuthorizeMPT | mustModifyVault` privilege set, which indicates the transaction interacts with both MPToken authorization and an associated vault ledger object managed through the `LoanBroker`.

## Amendment Gating and Delegation

The `featureLendingProtocol` amendment gate means this transaction type does not exist on the ledger until the amendment is enabled. The `notDelegable` flag on `Delegation` means a `sfDelegate` account cannot submit a `LoanSet` on behalf of another account — the borrower must sign directly. This restriction is enforced at the privilege level; the `TransactionBase::getDelegate()` accessor is still present on the wrapper class (inherited from the common base), but the transaction processor will reject any `LoanSet` that includes `sfDelegate`.
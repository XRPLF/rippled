# `LoanBrokerCoverWithdraw.h` — Auto-generated Transaction Wrapper

## Role and Context

This file is part of the auto-generated `protocol_autogen` layer for the XRPL LendingProtocol amendment (`featureLendingProtocol`). It defines the C++ interface for transaction type `ttLOAN_BROKER_COVER_WITHDRAW` (numeric type 77), which withdraws First Loss Capital (FLC) from a Loan Broker ledger object.

As its comment warns — *"This file is auto-generated. Do not edit."* — the canonical source of truth is the TRANSACTION macro entry in `include/xrpl/protocol/detail/transactions.macro`, which declares the field schema. The generated wrapper and builder in this file are derived mechanically from that schema, ensuring the two are always in sync.

Within the LendingProtocol family of transactions, `LoanBrokerCoverWithdraw` is the counterpart to `LoanBrokerCoverDeposit` (type 76). The deposit adds cover capital to a broker; the withdraw reclaims it. The withdraw variant carries two additional optional fields (`sfDestination`, `sfDestinationTag`) that the deposit transaction lacks, allowing withdrawn funds to be directed to an account other than the transaction sender.

## Class Design: Wrapper + Builder Pattern

The file defines two classes in `namespace xrpl::transactions`:

**`LoanBrokerCoverWithdraw`** inherits from `TransactionBase` and acts as an immutable, type-safe view over an existing `STTx`. It holds the transaction via a `std::shared_ptr<STTx const>` promoted from the base class and enforces the correct transaction type at construction:

```cpp
if (tx_->getTxnType() != txType)
    throw std::runtime_error("Invalid transaction type for LoanBrokerCoverWithdraw");
```

This fail-fast check ensures that code receiving a `LoanBrokerCoverWithdraw` object can trust the underlying `STTx` without further type checks — a defensive design that protects callers from accidentally wrapping the wrong transaction type.

**`LoanBrokerCoverWithdrawBuilder`** inherits from the CRTP base `TransactionBuilderBase<LoanBrokerCoverWithdrawBuilder>`. The Curiously Recurring Template Pattern is used so that common setter methods (like `setFee()`, `setSequence()`, `setMemos()`) defined in the base class return `Derived&` — the concrete builder type — enabling method chaining without virtual dispatch. The builder holds an `STObject object_{sfTransaction}` (declared in the base) as its mutable staging area. Calling `build(publicKey, secretKey)` invokes `sign()` on that object, moves it into a freshly constructed `STTx`, and wraps it in the immutable `LoanBrokerCoverWithdraw` type.

A second constructor `LoanBrokerCoverWithdrawBuilder(std::shared_ptr<STTx const> tx)` reconstructs a mutable builder from an existing signed transaction by copying the `STTx` into the `object_` staging area. This supports round-trip scenarios where an existing transaction must be rebuilt or re-signed.

## Field Schema

The transaction carries four fields derived from the macro schema:

| Field | Requirement | Type | Notes |
|---|---|---|---|
| `sfLoanBrokerID` | Required | `uint256` | Identifies the Loan Broker ledger object |
| `sfAmount` | Required | `STAmount` | Supports MPT; the amount of cover capital to withdraw |
| `sfDestination` | Optional | `AccountID` | Destination account for withdrawn funds |
| `sfDestinationTag` | Optional | `uint32_t` | Routing tag for destination account |

For required fields the wrapper getters return values directly (`getLoanBrokerID()`, `getAmount()`). For optional fields the getter checks `isFieldPresent()` first and returns `protocol_autogen::Optional<T>` — an alias for `std::optional<T>` — with paired `has*()` predicates. This avoids an unguarded `at()` call that would throw if the field is absent, providing a safe access pattern.

## The `mayAuthorizeMPT` Privilege

The macro entry marks this transaction with the `mayAuthorizeMPT` privilege, unlike the deposit counterpart which carries `noPriv`. This matters because `sfAmount` uses the `soeMPTSupported` annotation: the amount field can carry a Multi-Purpose Token (MPT) quantity rather than an XRP or IOU amount. The `mayAuthorizeMPT` flag signals to the engine's trust-line and MPT authorization machinery that this transaction may modify MPT authorization state as a side effect of moving MPT amounts out of the broker's cover pool.

The transaction is also marked `Delegation::notDelegable`, meaning no other account can be granted delegated authority to submit it. This restriction — shared with all LoanBroker transactions — reflects the sensitive nature of cover capital management.

## Relationship to the Transactor

The actual ledger logic for this transaction lives in `include/xrpl/tx/transactors/lending/LoanBrokerCoverWithdraw.h` (and its `.cpp` implementation). That file defines a `Transactor` subclass with `preflight`, `preclaim`, and `doApply` hooks. The auto-generated wrapper in this file is consumed by the transactor's `doApply` to access transaction fields in a type-safe way, and by test code and external tooling to construct well-formed transactions for submission. The separation keeps the protocol field schema (auto-generated, locked to the macro) cleanly distinct from the application logic (hand-written, in the transactor).

## Code Quality Notes

All getter methods are annotated `[[nodiscard]]`, preventing callers from silently discarding return values. Builder setters use `std::decay_t<typename SF_UINT256::type::value_type>` to accept values without imposing reference or const qualification concerns on callers. The constructor for the builder deliberately avoids calling `object_.set(soTemplate)` on the underlying `STObject` — a comment in `TransactionBuilderBase` explains this is intentional: setting a template before construction causes `applyTemplate()` inside the `STTx` constructor to reject fields that were left at their default values, so the base stays as a free `STObject` and the `STTx` constructor handles template application cleanly.
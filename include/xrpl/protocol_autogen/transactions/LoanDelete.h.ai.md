# `LoanDelete.h` — Auto-generated LendingProtocol Transaction Wrapper

`LoanDelete.h` is part of the `protocol_autogen` subsystem under `include/xrpl/protocol_autogen/transactions/`. The entire file is code-generated and carries a "Do not edit" comment at the top, which signals that the source of truth is an upstream schema or DSL rather than this C++ text itself. It defines the `ttLOAN_DELETE` transaction (type value 81), the teardown operation for the `featureLendingProtocol` amendment, and it follows the same structural template used throughout the autogen layer: a paired immutable wrapper class plus a fluent builder.

## Role in the Lending Protocol

Loans on the XRP Ledger are first-class ledger objects created by `LoanSet` (type 80). Once created, a loan has a unique 256-bit identifier stored in `sfLoanID`. `LoanDelete` is the mechanism for removing that ledger object. Compared to `LoanSet`, which carries over a dozen optional fields (interest rates, fees, payment schedules, counterparty signatures), `LoanDelete` is deliberately minimal: the only transaction-specific field is `sfLoanID`, and it is required. Nothing else is needed to unambiguously target a loan for deletion, which makes the schema intentionally narrow.

The transaction is marked `notDelegable` — meaning no account may submit it on behalf of another via the XRPL delegation mechanism. It also carries `noPriv`, distinguishing it from `LoanSet` which declares `mayAuthorizeMPT | mustModifyVault` privileges. The absence of privilege flags in `LoanDelete` means the transaction engine applies no vault or MPT authorization logic beyond ordinary account validation, consistent with the idea that closing a loan primarily affects the account that holds it.

## Class Design: Wrapper vs. Builder

The file contains two complementary classes that deliberately separate reading from writing.

`LoanDelete` extends `TransactionBase` and wraps a `std::shared_ptr<STTx const>` — the `const` qualifier in the pointee type is load-bearing. It is physically impossible to mutate the underlying serialized transaction through this class. All getter methods are marked `[[nodiscard]]` to prevent accidental ignored-return bugs at call sites. The class exposes exactly one transaction-specific accessor, `getLoanID()`, which directly calls `tx_->at(sfLoanID)` without an optional wrapper because the field is `soeREQUIRED` in the schema — if the field is absent the underlying `STTx` access would throw, which is the correct behavior for a required field.

`LoanDeleteBuilder` extends the CRTP base `TransactionBuilderBase<LoanDeleteBuilder>`. The CRTP pattern allows the base class's fluent setters (`setAccount`, `setFee`, `setSequence`, `setFlags`, etc.) to return `Derived&` rather than `TransactionBuilderBase&`, preserving the concrete type across method chains without virtual dispatch or replication of setter code. The builder accumulates state in an `STObject object_` member (declared in the base as `STObject object_{sfTransaction}`). This is a free `STObject`, not bound to any `SOTemplate`, which is intentional: binding too early would cause `applyTemplate()` to throw when `soeDEFAULT` fields are absent. The `STTx` constructor receives this free object and applies the template during construction.

## Construction Paths

The builder offers two entry points. The primary constructor takes the required fields by value — `account` as `SF_ACCOUNT::type::value_type` and `loanID` as `std::decay_t<typename SF_UINT256::type::value_type> const&`. The `std::decay_t` wrapper strips references and cv-qualifiers from the CRTP-deduced template type, preventing reference collapsing issues that arise in generated code that needs to accept both value and reference arguments uniformly. The optional `sequence` and `fee` parameters are forwarded to the base and set only when present, consistent with the builder pattern of deferring optional fields.

The second constructor takes an existing `std::shared_ptr<STTx const>` and copies the raw `STTx` into `object_`. This path exists for modification workflows where a caller deserializes an on-ledger transaction and wants to re-sign a modified version. Both constructors guard against type mismatches by checking `getTxnType() != ttLOAN_DELETE` and throwing `std::runtime_error`. This eagerly surfaces misuse rather than allowing a builder or wrapper to silently operate on the wrong transaction type, which could otherwise produce a malformed signed transaction that would be rejected by network validators.

## Build and Sign

`build(PublicKey const& publicKey, SecretKey const& secretKey)` finalizes the transaction. It calls the protected `sign()` method inherited from `TransactionBuilderBase`, which sets `sfSigningPubKey`, serializes the object without signing fields, prepends `HashPrefix::txSign`, signs with the provided secret key, and stores the resulting signature in `sfTxnSignature`. After signing, `build()` moves `object_` into a new `STTx` and wraps it in a `LoanDelete` instance. The move semantics here are deliberate: the builder is consumed, preventing any further mutation after the transaction has been signed and crystallized into the immutable wrapper.

## Relationship to Other Files

`TransactionBase.h` provides the common read-only field accessors shared across all transaction wrappers (account, sequence, fee, flags, memos, signers, etc.), so `LoanDelete` only needs to implement `getLoanID()` itself. `TransactionBuilderBase.h` provides all common builder setters in the same spirit. This means `LoanDelete.h` is genuinely minimal — the autogenerator only emits what is unique to this transaction type. Sibling files like `LoanManage.h`, `LoanPay.h`, and `LoanBrokerSet.h` follow the identical structural pattern, making the entire lending transaction family consistent and easy to navigate once you understand this template.
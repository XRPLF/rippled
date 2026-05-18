# `LoanBrokerCoverDeposit.h` — Auto-generated Transaction Wrapper

## Role in the System

This file is part of the `protocol_autogen` layer — a code-generated family of transaction-specific wrappers and builders that live under `include/xrpl/protocol_autogen/transactions/`. Its purpose is to give callers a type-safe, self-documenting C++ interface to the `ttLOAN_BROKER_COVER_DEPOSIT` transaction (numeric type 76), which is part of the `featureLendingProtocol` amendment family.

The transaction itself represents a deposit of *First Loss Capital* into a LoanBroker object on the XRP Ledger. In the lending protocol, a LoanBroker operator must post cover (collateral) into the broker's pseudo-account before loans can be extended; this transaction is how that cover moves from the owner's balance into the broker's available reserve (`sfCoverAvailable`). The underlying transactor logic in `LoanBrokerCoverDeposit.cpp` performs the `accountSend` from the submitting account to the broker pseudo-account and increments the broker's cover balance.

## Two-Class Autogen Pattern

Every transaction in the autogen layer follows the same dual-class pattern defined by this file's base types:

**`LoanBrokerCoverDeposit`** inherits from `TransactionBase`, which holds a `std::shared_ptr<STTx const>`. The `const` in the shared pointer is the critical design choice — the wrapper is permanently immutable. The type-check in the constructor (`tx_->getTxnType() != txType`) enforces the invariant that you cannot accidentally wrap the wrong transaction type; a mismatched `STTx` throws `std::runtime_error` at construction time rather than silently returning garbage from a field getter.

`getLoanBrokerID()` and `getAmount()` are the only transaction-specific accessors exposed here. Everything else — account, sequence, fee, flags, memos, signers, network ID, delegate, etc. — is inherited from `TransactionBase`, which covers the full set of common XRPL transaction fields.

**`LoanBrokerCoverDepositBuilder`** inherits from `TransactionBuilderBase<LoanBrokerCoverDepositBuilder>`, a CRTP template. This curiously recurring template pattern is why every setter in `TransactionBuilderBase` can return `Derived&` (the concrete builder type) rather than a base-class reference, preserving the fluent method-chaining interface across the inheritance boundary without virtual dispatch or casting at the call site.

The builder works with a mutable `STObject object_{sfTransaction}` inherited from the base. The comment in `TransactionBuilderBase` explains a subtle point: the object is left "free" (not initialized from the SOTemplate) intentionally. Initializing it from the template would inject `STBase` placeholders for `soeDEFAULT` fields, which then cause `applyTemplate()` to throw `"may not be explicitly set to default"` when the `STTx` constructor is called. The fix is to populate only the fields you actually set, and let the `STTx` constructor's own `applyTemplate()` handle defaults cleanly.

## Required Fields

The transaction macro in `transactions.macro` declares exactly two required fields:

- `sfLoanBrokerID` (`soeREQUIRED`): A `uint256` identifying the target LoanBroker ledger object. The transactor rejects a zero value with `temINVALID`.
- `sfAmount` (`soeREQUIRED`, `soeMPTSupported`): The amount of cover to deposit. The `soeMPTSupported` annotation means the field accepts both traditional XRP/IOU amounts and Multi-Purpose Token (MPT) amounts — the `getAmount()` and `setAmount()` methods reflect this through `SF_AMOUNT::type::value_type`.

Both fields are required in the builder constructor, so the fluent interface cannot be used to accidentally leave them unset before calling `build()`.

## Build and Sign Flow

`LoanBrokerCoverDepositBuilder::build(publicKey, secretKey)` calls the protected `sign()` method inherited from `TransactionBuilderBase`. That method serializes the `STObject` without its signing fields, prepends `HashPrefix::txSign`, computes the cryptographic signature, embeds it as `sfTxnSignature`, and sets `sfSigningPubKey`. The resulting signed `STObject` is then moved into a freshly constructed `STTx`, which is wrapped in a `shared_ptr<STTx const>` and handed to the `LoanBrokerCoverDeposit` constructor.

The builder also provides a second constructor that takes an existing `std::shared_ptr<STTx const>`. This enables a round-trip workflow: decode a transaction from the wire, wrap it in a builder via `*tx` assignment into `object_`, modify fields, re-sign, and build a new wrapper. The type-check in that constructor mirrors the one in `LoanBrokerCoverDeposit`'s constructor for the same reason.

## Amendment and Delegation

The transaction is gated by `featureLendingProtocol` and is explicitly `Delegation::notDelegable` — an account cannot authorize a delegate to submit cover deposits on its behalf. This is enforced at the transactor layer (`checkLendingProtocolDependencies` and the delegation flag check in `Transactor`), but the `txType` constant and the amendment annotation in the class doc also make these constraints visible directly from the header to any reader or code generator consuming it.
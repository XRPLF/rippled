# `AMMDeposit.h` — Auto-generated AMM Deposit Transaction Wrapper

## Role and Context

This file is part of the `protocol_autogen` layer — a code-generated API that sits on top of XRPL's low-level `STTx`/`STObject` serialization infrastructure and exposes each transaction type as a pair of purpose-built C++ classes. The file is declared auto-generated (`// This file is auto-generated. Do not edit.`) and lives alongside analogous headers for every other XRPL transaction in `include/xrpl/protocol_autogen/transactions/`.

`AMMDeposit` implements transaction type `ttAMM_DEPOSIT` (integer code 36), introduced by the `featureAMM` amendment. It allows an account to contribute liquidity to an existing Automated Market Maker pool, receiving LP (Liquidity Provider) tokens in return. The transaction is marked *delegable*, meaning it can be submitted on behalf of another account using the `sfDelegate` field inherited from `TransactionBase`. Its sibling `AMMWithdraw.h` mirrors this structure for the reverse operation.

## Class Structure: Wrapper + Builder

The file defines exactly two classes in the `xrpl::transactions` namespace:

**`AMMDeposit`** is an immutable, read-only view of a fully-formed `STTx`. It extends `TransactionBase`, which holds a `std::shared_ptr<STTx const>` in its protected `tx_` member. Once constructed, the underlying transaction cannot be modified through this interface. The type-safety guarantee is enforced in the constructor: if the wrapped `STTx` is not of type `ttAMM_DEPOSIT`, a `std::runtime_error` is thrown immediately. A static `constexpr txType` enables compile-time dispatch when needed.

**`AMMDepositBuilder`** inherits from `TransactionBuilderBase<AMMDepositBuilder>` via CRTP. This base holds a mutable `STObject object_{sfTransaction}` — notably not initialized from a schema template. The comment inside `TransactionBuilderBase` explains why: pre-applying the template inserts `soeDEFAULT` placeholders that cause `applyTemplate()` to throw when the final `STTx` is constructed. Instead, only explicitly-set fields are added to the object, and the `STTx` constructor's own `applyTemplate()` call handles missing optional fields correctly.

## AMM Deposit Fields and Deposit Modes

The AMM protocol supports several deposit modes, controlled by which optional fields are present. The field schema reflects this flexibility:

- **`sfAsset` / `sfAsset2`** (required, `SF_ISSUE`): Identify the AMM pool by its two constituent token types. These are *issue identifiers* (currency + issuer), not amounts — hence `SF_ISSUE::type::value_type`, not `SF_AMOUNT`. In `setAsset()` and `setAsset2()`, the builder wraps values in `STIssue(sfAsset, value)` before assignment, whereas the amount setters assign directly — a subtle but important difference in how the serialization layer handles typed fields.

- **`sfAmount` / `sfAmount2`** (optional, `SF_AMOUNT`): The actual token quantities being deposited. Providing both enables a proportional dual-asset deposit. Providing only `sfAmount` triggers a single-asset deposit, which typically incurs a swap fee on the unbalanced side.

- **`sfLPTokenOut`** (optional, `SF_AMOUNT`): Specifies the exact quantity of LP tokens the depositor wants to receive, letting the protocol back-calculate the required input amounts. This is mutually exclusive with certain other optional fields in practice, though that constraint is enforced by ledger validation logic, not in this header.

- **`sfEPrice`** (optional, `SF_AMOUNT`): Caps the effective price at which the single-asset deposit executes, serving as a slippage guard similar to a limit price on a DEX order.

- **`sfTradingFee`** (optional, `SF_UINT16`): Allows the depositor to specify or influence the AMM's trading fee in certain deposit modes. Being a 16-bit integer (basis points), it differs in type from the amount fields — the corresponding getter returns `protocol_autogen::Optional<SF_UINT16::type::value_type>`.

## Optional Field Handling Pattern

Every optional field follows the same dual-method pattern: `hasX()` returns `bool` via `STTx::isFieldPresent()`, and `getX()` returns `protocol_autogen::Optional<T>` — calling `hasX()` internally before dereferencing the field. This guards against accessing absent fields, which would throw in the underlying `STTx::at()` call.

`protocol_autogen::Optional<T>` from `Utils.h` is a type alias that transparently handles reference-type fields: if `T` is a reference type, the optional wraps a `std::reference_wrapper`; otherwise it wraps by value. For `AMMDeposit`'s fields, all returned types are value types, so this resolves to plain `std::optional<T>`.

## Builder Construction and Signing

`AMMDepositBuilder` provides two constructors. The primary one takes the required fields — account, `sfAsset`, `sfAsset2` — plus optional sequence and fee, reflecting the transaction's actual required/optional schema. The secondary constructor reconstructs a builder from an existing `STTx` (copying its `STObject`) after type-checking, useful for re-signing or modifying a pre-built transaction.

All `setX()` methods return `AMMDepositBuilder&`, enabling fluent chaining. The inherited `sign()` method in `TransactionBuilderBase` materializes the signature: it prepends `HashPrefix::txSign` to the serialized fields (excluding signing fields themselves), signs with the provided key pair, and stores both the public key and signature in the object. The terminal `build()` method calls `sign()` then promotes the `STObject` into an `STTx` via move construction, producing an `AMMDeposit` wrapper — at which point the transaction becomes immutable.

## Design Rationale

The strict separation between the read-only `AMMDeposit` and the mutable `AMMDepositBuilder` is a deliberate invariant: a signed transaction should never be mutated after the fact, because doing so would invalidate the signature and violate ledger integrity. By making `AMMDeposit` wrap a `std::shared_ptr<STTx const>`, the type system makes post-signing mutation impossible. The CRTP builder pattern allows `TransactionBuilderBase` to provide strongly-typed common setters (account, fee, flags, memos, delegate, etc.) that return the concrete derived type rather than the base, preserving the fluent interface without virtual dispatch overhead.
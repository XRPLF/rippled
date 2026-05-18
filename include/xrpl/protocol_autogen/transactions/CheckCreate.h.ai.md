# `CheckCreate.h` — Auto-Generated CheckCreate Transaction Wrapper

## Role in the System

This file lives in `include/xrpl/protocol_autogen/transactions/` and is part of a code-generated layer that exposes every XRPL transaction type as a pair of C++ classes: an immutable read-only wrapper and a fluent builder. The header is explicitly marked `// This file is auto-generated. Do not edit.` — meaning the actual source of truth is a code-generation pipeline, not this file itself. The purpose of this entire layer is to eliminate the stringly-typed, field-name-lookup patterns scattered across the rippled codebase and replace them with compile-time-verified accessors.

`CheckCreate` represents XRPL transaction type `ttCHECK_CREATE` (numeric type 16). In the XRPL protocol, a `CheckCreate` authorizes a potential payment: the sender writes a "check" specifying a destination and a maximum debit amount (`SendMax`). The check sits as a ledger object until the recipient cashes it via `CheckCash`, or it expires, or either party cancels it with `CheckCancel`. This file lives alongside `CheckCash.h` and `CheckCancel.h` in the same directory, forming the complete check lifecycle.

## `CheckCreate` — Immutable Read Wrapper

`CheckCreate` inherits from `TransactionBase`, which itself is a thin wrapper holding a `std::shared_ptr<STTx const>`. The `const` qualifier on `STTx` is the cornerstone of the design: once a `CheckCreate` is constructed, the underlying transaction data is frozen. Every getter is `[[nodiscard]]` and `const`, reinforcing the read-only contract.

Construction takes a `std::shared_ptr<STTx const>` and immediately validates that `tx_->getTxnType() == ttCHECK_CREATE`, throwing `std::runtime_error` on mismatch. This guard is critical because `STTx` objects circulate throughout the ledger engine as type-erased pointers; the constructor enforces the narrowing.

The transaction-specific field accessors split cleanly into two patterns driven by the XRPL field optionality model:

**Required fields** — `getDestination()` and `getSendMax()` — call `tx_->at(sfField)` directly and return a value. There is no nullability; the underlying `STTx` schema validation guarantees these fields are present.

**Optional fields** — `getExpiration()`, `getDestinationTag()`, and `getInvoiceID()` — each has a corresponding `hasXxx()` predicate that calls `tx_->isFieldPresent(sfField)`. The getter then returns `protocol_autogen::Optional<T>`, a template alias defined in `Utils.h`. The alias resolves to `std::optional<T>` for value types and `std::optional<std::reference_wrapper<T>>` for reference types, handling STL's inability to store references directly in `std::optional`. All three optional getters check `hasXxx()` before calling `tx_->at(...)`, returning `std::nullopt` otherwise.

Notable field semantics: `sfSendMax` is annotated as supporting MPT (Multi-Purpose Token) amounts — it uses `SF_AMOUNT::type::value_type`, which in XRPL can represent XRP drops, IOU amounts, or an MPT amount depending on the ledger feature state. `sfInvoiceID` is a 256-bit hash (`SF_UINT256`) that lets the check sender embed an application-level reference for invoice reconciliation. `sfDestinationTag` is the standard XRPL routing tag that identifies the final recipient when the destination account is a hosted wallet aggregator.

Common fields (`sfAccount`, `sfSequence`, `sfFee`, `sfMemos`, `sfSigners`, `sfDelegate`, etc.) are inherited from `TransactionBase` and are not repeated here.

## `CheckCreateBuilder` — Fluent Construction

`CheckCreateBuilder` uses the Curiously Recurring Template Pattern (CRTP) via `TransactionBuilderBase<CheckCreateBuilder>`. The base class holds a mutable `STObject object_{sfTransaction}` and returns `Derived&` (i.e., `CheckCreateBuilder&`) from every setter, enabling method chaining without slicing. The base intentionally does *not* call `object_.set(soTemplate)` on the `STObject`, sidestepping a subtle trap: calling `applyTemplate()` on a partial object would insert `soeDEFAULT` placeholders that later cause the `STTx` constructor to throw "may not be explicitly set to default". Instead, the `STTx` constructor's own `applyTemplate()` pass handles field validation and defaults when `build()` is called.

The primary constructor takes the two required fields (`destination` and `sendMax`) alongside the initiating account and optional `sequence`/`fee`. Requiring these at construction time prevents the builder from emitting a structurally invalid transaction — you cannot call `build()` without a destination and maximum send amount. The secondary constructor accepts an existing `std::shared_ptr<STTx const>` and copies its `STObject` into `object_`, enabling round-trip edit workflows where an already-constructed transaction needs modification.

The `build(PublicKey, SecretKey)` method calls the protected `sign()` helper from the base, which serializes the object with `HashPrefix::txSign` prepended (the XRPL canonical signing prefix), signs the hash with the provided key pair, embeds the `sfSigningPubKey` and `sfTxnSignature` fields, and then wraps the result in a freshly constructed `STTx`. The returned `CheckCreate` wrapper thus carries a fully signed, immutable transaction ready for submission.

## Design Tradeoffs

The immutable-wrapper / mutable-builder split is a deliberate separation of concerns: code that inspects or relays transactions (validators, ledger processors, RPC handlers) works exclusively with `CheckCreate`, gaining compile-time guarantees that it cannot accidentally mutate live transaction state. Code that constructs transactions works exclusively with `CheckCreateBuilder`. The cost of this cleanliness is a memory copy at `build()` time — the `STObject` is moved into the `STTx`, which is then heap-allocated and wrapped in a `shared_ptr`. For a transaction that is constructed once and read many times, this is a sound trade.

The `Delegation::delegable` annotation in the class-level comment indicates that `CheckCreate` participates in the XRPL delegation feature, meaning a delegate account (`sfDelegate`) may submit this transaction type on behalf of another account. The zero-value `Amendment` field (`uint256{}`) means `CheckCreate` requires no feature amendment — it is baseline protocol behavior.
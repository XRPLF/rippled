# `CheckCash.h` — Auto-Generated CheckCash Transaction Wrapper

## File Role and Context

This file is part of the `xrpl/protocol_autogen/transactions/` layer — a collection of auto-generated C++ headers (one per XRPL transaction type) that provide strongly typed wrappers and builders over the ledger's underlying `STTx` serialization format. The file must never be edited by hand; it is regenerated from a schema definition whenever the protocol changes.

`CheckCash.h` implements transaction type `ttCHECK_CASH` (type code 17), the second step in the XRPL Checks lifecycle. A sender first issues a `CheckCreate` transaction, creating a ledger object that resembles a paper check: it identifies a destination account and a maximum `SendMax` amount. The destination account then submits a `CheckCash` transaction — the subject of this file — to actually pull funds from the sender. The check is deleted from the ledger upon successful cashing. Either party may destroy an uncashed check via `CheckCancel`.

## Two-Class Design: Wrapper + Builder

The file declares exactly two classes: `CheckCash` (an immutable read-only view) and `CheckCashBuilder` (a mutable construction aid). This split is deliberate and consistent across every transaction type in the autogen layer.

### `CheckCash` — the Immutable Wrapper

`CheckCash` extends `TransactionBase`, which stores a `std::shared_ptr<STTx const>` as its sole data member. Sharing ownership of an immutable `STTx` is cheap: multiple observers can hold the same transaction without copying or locking. The `const`-qualified pointer ensures no code path can modify the underlying bytes after construction.

The constructor enforces an immediate type invariant:

```cpp
if (tx_->getTxnType() != txType)
    throw std::runtime_error("Invalid transaction type for CheckCash");
```

This prevents a `CheckCreate`'s `STTx` from being accidentally wrapped in a `CheckCash`, a mistake that would otherwise compile and silently misinterpret fields at runtime. The base class does not perform this check — each subclass is responsible for its own type assertion.

### Transaction-Specific Fields

`CheckCash` exposes three transaction-specific fields beyond what `TransactionBase` provides:

**`sfCheckID` (required)**: A 256-bit hash that identifies the Check ledger object to cash. `getCheckID()` returns it directly; there is no `hasCheckID()` because the field is mandatory and `STTx::at()` would throw on absence.

**`sfAmount` (optional)**: When present, specifies an exact amount the cashing account wishes to receive. The ledger enforces that the sender is debited exactly this value. Both `getAmount()` and `hasAmount()` are provided, following the pattern used throughout the autogen layer for optional fields.

**`sfDeliverMin` (optional)**: When present, specifies the minimum acceptable delivery amount. Unlike `sfAmount`, the ledger may deliver more than this floor (matching payment path logic). This is useful when the check's `SendMax` could yield variable amounts through order books.

The protocol requires exactly one of `sfAmount` or `sfDeliverMin` to be present — not both, not neither. That mutual-exclusion rule is enforced at the ledger transaction processing level (not in this auto-generated file, which only provides field access). The `mayCreateMPT` privilege annotation on `CheckCash` (absent on `CheckCreate`) reflects that receiving a cashed check can establish a new MPT balance for the destination account; both amount fields carry `@note This field supports MPT` annotations accordingly.

All getters are decorated with `[[nodiscard]]`, preventing callers from accidentally discarding return values. `getAmount()` and `getDeliverMin()` return `protocol_autogen::Optional<SF_AMOUNT::type::value_type>`, where `Optional<T>` is a conditional alias from `Utils.h` — if `T` is a reference type it wraps in `std::reference_wrapper`, otherwise it aliases `std::optional<T>` directly. This guards against dangling-reference bugs when `T` is deduced as a reference.

### `CheckCashBuilder` — the Fluent Builder

`CheckCashBuilder` inherits from `TransactionBuilderBase<CheckCashBuilder>` using CRTP. The template parameter lets `TransactionBuilderBase`'s common setters (account, fee, sequence, flags, memos, delegate, etc.) return `Derived&` — here `CheckCashBuilder&` — enabling uninterrupted method chaining without virtual dispatch or repeated casts in calling code.

Internally, the builder holds a mutable `STObject object_{sfTransaction}`. The base class constructor deliberately does *not* call `object_.set(soTemplate)`, avoiding the creation of `soeDEFAULT` placeholder fields that would cause `STTx`'s `applyTemplate()` to throw "may not be explicitly set to default". Fields are added on-demand as setters are called.

The primary constructor:

```cpp
CheckCashBuilder(SF_ACCOUNT::type::value_type account,
                 std::decay_t<typename SF_UINT256::type::value_type> const& checkID,
                 std::optional<SF_UINT32::type::value_type> sequence = std::nullopt,
                 std::optional<SF_AMOUNT::type::value_type> fee = std::nullopt)
```

requires `account` and `checkID` (the only mandatory `CheckCash`-specific field) at construction time. `sequence` and `fee` are optional at this stage to support workflows where those fields are determined or auto-filled later by signing infrastructure.

A secondary constructor accepts an `STTx const` directly, copying its `STObject` into `object_`. This allows round-tripping: deserializing an existing `CheckCash` transaction and modifying it before re-signing. It too performs a type-guard check.

`build()` calls the protected `sign()` helper from `TransactionBuilderBase`, which serializes `object_` with a `HashPrefix::txSign` prefix (excluding signing fields), signs the result with the supplied `PublicKey`/`SecretKey`, writes back `sfSigningPubKey` and `sfTxnSignature`, then constructs a new `STTx` from the completed `STObject`. It returns a fully validated `CheckCash` wrapper — at which point the transaction is immutable.

## Relationship to Siblings

All three Check transaction types (`CheckCreate.h`, `CheckCancel.h`, `CheckCash.h`) share the same structural boilerplate: a `TransactionBase` subclass with `[[nodiscard]]` getters, and a CRTP builder. `CheckCreate` carries `sfDestination` and `sfSendMax` as required fields plus optional `sfExpiration`, `sfDestinationTag`, and `sfInvoiceID`. `CheckCash` references the created check by its ledger object hash (`sfCheckID`) and adds the settlement semantics through `sfAmount`/`sfDeliverMin`. The autogeneration discipline ensures these files remain consistent across the full set of ~70 transaction types in the directory.
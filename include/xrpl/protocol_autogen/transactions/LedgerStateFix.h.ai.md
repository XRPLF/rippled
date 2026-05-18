# `LedgerStateFix.h` — Auto-generated Transaction Wrapper for `ttLEDGER_STATE_FIX`

## Role and Context

This header is part of the `protocol_autogen` subsystem — a layer of machine-generated code that wraps every XRPL transaction type in a pair of C++ classes: an immutable read-only accessor (`LedgerStateFix`) and a fluent builder (`LedgerStateFixBuilder`). It lives in `xrpl::transactions`, distinct from the identically named `xrpl::LedgerStateFix` transactor class that implements the actual on-ledger logic.

The transaction type it represents, `ttLEDGER_STATE_FIX` (numeric value 53), exists for a specific and narrow purpose: repairing corrupted state in the XRPL ledger itself. Its first and currently only fix variant (`nfTokenPageLink = 1`) addresses broken doubly-linked-list pointers in NFToken page chains — a class of corruption that can leave an account's NFTs inaccessible. This is an unusual type in the protocol: it is not about transferring value or creating objects, but about surgical correction of existing ledger state.

The transaction is guarded by the `fixNFTokenPageLinks` amendment. It cannot be submitted on the main network until that amendment reaches supermajority consensus. It is marked `delegable`, meaning it can be submitted by a delegate account on behalf of the owning account.

## Field Schema

The transaction has exactly two fields beyond the universal common fields inherited from all transactions:

- **`sfLedgerFixType`** (`UINT16`, required): Identifies which fix operation to perform. Currently only value `1` (`nfTokenPageLink`) is valid; any other value causes `preflight` to return `tefINVALID_LEDGER_FIX_TYPE`. This field acts as a discriminant that lets the protocol extend to new fix operations without introducing new transaction types.

- **`sfOwner`** (`ACCOUNT`, optional): The target account whose state is being repaired. It is required when `sfLedgerFixType == nfTokenPageLink` — the `preflight` check enforces this relationship. The optionality at the schema level allows future fix types that operate without a specific account target.

This two-field design is intentional: it keeps the transaction extensible. Adding a new kind of ledger repair only requires adding a new `FixType` enum value and handling it in the transactor's switch statement, not a new transaction type.

## Class Structure

### `LedgerStateFix` — Immutable Wrapper

`LedgerStateFix` extends `TransactionBase`, which holds a `std::shared_ptr<STTx const>` and provides accessors for universal fields (account, fee, sequence, flags, memos, signers, delegate, etc.). The subclass adds two transaction-specific accessors:

- `getLedgerFixType()` returns the `uint16_t` value directly, as the field is required and always present.
- `getOwner()` returns `protocol_autogen::Optional<AccountID>` — a thin alias for `std::optional<AccountID>` — paired with a `hasOwner()` predicate. This pattern is consistent across all optional fields in the autogen layer.

The constructor validates the `STTx` type at construction time, throwing `std::runtime_error` if the transaction type doesn't match `ttLEDGER_STATE_FIX`. This is an eager fail-fast check: it catches programming errors at the point of wrapping rather than producing silent misbehavior during field access.

All getters are marked `[[nodiscard]]`, preventing callers from accidentally discarding return values from predicate or value-returning functions.

### `LedgerStateFixBuilder` — Fluent Constructor

`LedgerStateFixBuilder` inherits from `TransactionBuilderBase<LedgerStateFixBuilder>` using the Curiously Recurring Template Pattern (CRTP). The base class uses `static_cast<Derived&>(*this)` in every setter to return the derived type, enabling method chaining without virtual dispatch and without losing type information. The result is that callers can chain `setLedgerFixType(...)`, `setOwner(...)`, `setFee(...)`, `setLastLedgerSequence(...)`, and any other common setter in a single expression, with the final `.build(publicKey, secretKey)` producing a signed `LedgerStateFix` wrapper.

The builder offers two construction paths:
1. **From scratch**: The primary constructor takes `account` and `ledgerFixType` as required parameters, with `sequence` and `fee` as optional. It immediately calls `setLedgerFixType()` in the constructor body, ensuring the required field is always present regardless of subsequent chaining.
2. **From an existing `STTx`**: A secondary constructor copies the serialized transaction into the internal `STObject`, useful for re-signing or modifying a previously constructed transaction.

The `build()` method calls the protected `sign()` helper from `TransactionBuilderBase`, which serializes the object with `HashPrefix::txSign` prepended, signs it with the provided key pair, and embeds the resulting signature. The signed `STTx` is then moved into a `shared_ptr<STTx const>` — making it immutable — and wrapped in the `LedgerStateFix` accessor type.

A notable detail in `TransactionBuilderBase`: the internal `STObject` is intentionally kept as a "free object" (not bound to an `SOTemplate`). This avoids pre-populating default-valued fields, which would later cause `STTx::applyTemplate()` to throw when encountering fields explicitly set to their default. The template is only applied by the `STTx` constructor at `build()` time.

## Fee Design

The fee for `LedgerStateFix` is set to one owner reserve (the same as `AccountDelete`). This is deliberately expensive. The transaction performs ledger repair, which requires iterating and relinking NFToken pages — potentially a non-trivial operation. The high fee discourages frivolous use while remaining accessible when a genuine fix is needed.

## Relationship to the Transactor

The `xrpl::LedgerStateFix` transactor (in `src/libxrpl/tx/transactors/system/LedgerStateFix.cpp`) consumes the signed `STTx` that this builder produces. The transactor's `doApply()` dispatches on `sfLedgerFixType` and calls `nft::repairNFTokenDirectoryLinks()` for the `nfTokenPageLink` variant. The wrapper classes in this autogen header are entirely separate from that execution path — they exist to provide a safe, ergonomic construction and inspection API at the application and test layers, while the raw `STTx` flows through the existing transaction pipeline unchanged.
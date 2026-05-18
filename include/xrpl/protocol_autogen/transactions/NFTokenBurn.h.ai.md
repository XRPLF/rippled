# `NFTokenBurn.h` — Auto-Generated NFTokenBurn Transaction Wrapper

This file is part of the `protocol_autogen` subsystem of the XRPL codebase and is auto-generated — the comment at line 1 makes this explicit. It lives in `include/xrpl/protocol_autogen/transactions/` alongside roughly seventy sibling files that each define one XRPL transaction type using the same structural template. It should never be edited by hand; the source of truth is `include/xrpl/protocol/detail/transactions.macro`, which declares `ttNFTOKEN_BURN` as type 26 with the field schema that drove this file's generation.

## Purpose

The file provides two things: a read-only, type-safe C++ wrapper around an already-serialized `NFTokenBurn` transaction (`NFTokenBurn` class), and a fluent builder for constructing new such transactions (`NFTokenBurnBuilder` class). Both live in the `xrpl::transactions` namespace.

`NFTokenBurn` is the XRPL transaction that permanently destroys an existing non-fungible token. The token is identified by the required `sfNFTokenID` (a `uint256` hash). Because issuer accounts sometimes need to reclaim tokens held by other accounts — for example, in regulated or permissioned NFT schemes — the transaction also accepts an optional `sfOwner` field (an `AccountID`): when present, it specifies a third-party account whose NFT page should be searched rather than the submitting account's own page.

## The `NFTokenBurn` Wrapper

`NFTokenBurn` extends `TransactionBase`, which holds a `shared_ptr<STTx const>` and exposes accessors for the universal transaction fields (`sfAccount`, `sfFee`, `sfSequence`, `sfDelegate`, etc.). The subclass adds only the NFT-specific fields:

- `getNFTokenID()` returns the required `uint256` token ID by calling `tx_->at(sfNFTokenID)`. Because the field is `soeREQUIRED` in the schema, accessing it without a presence check is safe.
- `getOwner()` guards the optional `sfOwner` access behind `hasOwner()`, returning `protocol_autogen::Optional<AccountID>` — a thin alias for `std::optional` used consistently across the autogen layer — rather than returning a default-constructed value or throwing.

The constructor enforces the type invariant eagerly: if the wrapped `STTx`'s `getTxnType()` does not equal `ttNFTOKEN_BURN`, it throws `std::runtime_error` immediately. This is a deliberate defensive choice over a compile-time guarantee, because the `STTx` type is not a template parameter — the check must happen at runtime when an arbitrary `shared_ptr<STTx const>` is handed in from deserialization or test code.

## The `NFTokenBurnBuilder`

`NFTokenBurnBuilder` extends `TransactionBuilderBase<NFTokenBurnBuilder>` using CRTP, which allows the base class to return `Derived&` from every common setter (`setFee`, `setSequence`, `setLastLedgerSequence`, `setDelegate`, etc.) without requiring virtual dispatch or down-casting at the call site. The derived class only adds the NFT-specific setters that call back into the underlying `STObject object_` held by the base.

The primary constructor takes `account` and `nFTokenID` as mandatory parameters — matching the `soeREQUIRED` schema constraint — plus `sequence` and `fee` as `std::optional` arguments with `nullopt` defaults. Passing them as optionals, rather than omitting them entirely, makes the builder usable in environments like unit tests where the sequence and fee are provided explicitly, while still deferring them for cases where the network will autofill. The required `nFTokenID` is set immediately via `setNFTokenID()` inside the constructor body, so the builder is always in a valid partial state.

The secondary constructor accepts an existing `shared_ptr<STTx const>` and copies its data into `object_`. This is a round-trip path: take a signed transaction off the wire, load it into a builder, modify a field, and re-sign. It performs the same type guard as the wrapper class.

`setOwner()` is available but entirely optional. A caller constructs without it when burning their own token; they call `.setOwner(someAccount)` when acting as an issuer burning a token from another holder's page.

`build(PublicKey, SecretKey)` finalizes construction: it delegates to the base class `sign()` method, which computes the serialization prefix (`HashPrefix::txSign`), serializes the `STObject` without signing fields, signs with `xrpl::sign`, and sets both `sfSigningPubKey` and `sfTxnSignature`. It then moves `object_` into a freshly constructed `STTx` (which calls `applyTemplate()` to fill any schema defaults) and returns an `NFTokenBurn` wrapper around it. Note that after `build()` is called `object_` has been moved-from, so the builder instance should not be reused.

## Design Notes

The use of `std::decay_t<typename SF_UINT256::type::value_type>` in setter signatures strips reference qualifiers from the SField type alias, ensuring the parameter is always taken as a value and avoiding ambiguity when the underlying type is itself a reference typedef. This pattern repeats across all autogen builders.

The transaction is annotated in `transactions.macro` as `Delegation::delegable`, meaning an `sfDelegate` field may be present and the network will validate delegated authority to burn NFTs on behalf of the account owner. It is also annotated with the `changeNFTCounts` privilege, reflecting that a successful burn decrements the NFToken page bookkeeping for the owning account — a ledger mutation that requires explicit privilege tracking in the XRPL permission model. The `uint256{}` amendment identifier (all zeros) signals that `NFTokenBurn` requires no specific amendment to be active; it is a base protocol transaction.
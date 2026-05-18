# `NFTokenCreateOffer.h` — Auto-generated Transaction Wrapper

This file is part of a large family of auto-generated headers in `include/xrpl/protocol_autogen/transactions/` — one per XRPL transaction type. It defines two classes for the `NFTokenCreateOffer` transaction (type ID 27 / `ttNFTOKEN_CREATE_OFFER`): a read-only wrapper for inspecting existing signed transactions, and a fluent builder for constructing and signing new ones. The file itself carries the `// This file is auto-generated. Do not edit.` guard, meaning its structure is maintained by tooling rather than hand-written, but it is worth understanding deeply because all NFT secondary-market tooling in the codebase flows through these types.

## The Wrapper/Builder Split

Every transaction in this layer follows the same two-class pattern found across all siblings (`NFTokenAcceptOffer.h`, `Payment.h`, etc.):

- **`NFTokenCreateOffer : TransactionBase`** — an immutable value object wrapping `std::shared_ptr<STTx const>`. All accessors are `const` and `[[nodiscard]]`. Once constructed it cannot be mutated.
- **`NFTokenCreateOfferBuilder : TransactionBuilderBase<NFTokenCreateOfferBuilder>`** — a mutable staging object that accumulates field assignments and produces a signed `NFTokenCreateOffer` via `build()`.

The separation enforces a clear lifecycle: build and sign once, then pass around only the immutable wrapper. There is no path to accidentally mutate a transaction after it leaves the builder.

## Fields and Their Semantics

The transaction carries two required fields and three optional ones, each reflecting a distinct aspect of the NFT offer protocol:

**Required fields** (`soeREQUIRED`) — accessed directly, no `std::nullopt` path:

- `sfNFTokenID` (`SF_UINT256`) — the 256-bit identifier of the NFToken being offered. This ID is the ledger object key for the token and encodes the issuer, taxon, and sequence in its bit layout.
- `sfAmount` (`SF_AMOUNT`) — the price being offered. `STAmount` on XRPL can represent either XRP drops or a fungible token IOU amount, so this single field covers both asset classes.

**Optional fields** (`soeOPTIONAL`) — accessed via `getX()` returning `protocol_autogen::Optional<T>`, guarded by a companion `hasX()`:

- `sfDestination` (`SF_ACCOUNT`) — restricts the offer to a single counterparty. If set, only that account can accept the offer. This is important for private OTC deals or royalty-bearing transfers where the seller wants to control who receives the token.
- `sfOwner` (`SF_ACCOUNT`) — identifies the current holder of the NFToken. This field is absent when the submitter is creating a *sell offer* (they own the token themselves). It is required when creating a *buy offer* (the submitter wants to purchase a token from someone else), because the ledger needs to know which account's `NFTokenOffer` object to create the offer under.
- `sfExpiration` (`SF_UINT32`) — a ledger-close-time value after which the offer becomes invalid. Expressed as seconds since the Ripple epoch; the ledger rejects attempts to accept an expired offer.

## `protocol_autogen::Optional<T>` Alias

Optional field getters return `protocol_autogen::Optional<T>` rather than `std::optional<T>` directly. The alias in `Utils.h` selects between `std::optional<T>` (for non-reference types) and `std::optional<std::reference_wrapper<std::remove_reference_t<T>>>` (for reference types). This matters because `STAmount` is returned by value from `STObject::at()`, so `getAmount()` yields a plain `STAmount` copy, while hypothetical reference-returning fields would yield a `reference_wrapper`. The alias handles this variance without requiring separate template specialisations at each call site.

## Type-Safety Invariant

Both the wrapper constructor and the STTx-deserialising builder constructor throw `std::runtime_error` immediately if `getTxnType() != ttNFTOKEN_CREATE_OFFER`. This fail-fast check means a mismatched STTx cannot silently masquerade as an `NFTokenCreateOffer`. The wrapper's `tx_` member is `const`-qualified via `shared_ptr<STTx const>`, so once the type check passes, the invariant holds for the lifetime of the object.

## Builder Design and CRTP

`NFTokenCreateOfferBuilder` inherits from `TransactionBuilderBase<NFTokenCreateOfferBuilder>` using the Curiously Recurring Template Pattern. All setter methods in the base class return `Derived&` (i.e., `NFTokenCreateOfferBuilder&`) instead of a base reference, so method chains never lose their concrete type. The builder stores its work in `STObject object_{sfTransaction}`, deliberately without calling `object_.set(soTemplate)`: the base class comment explains that pre-populating `soeDEFAULT` fields would cause `STTx::applyTemplate()` to throw "may not be explicitly set to default". The `STTx` constructor itself handles missing fields correctly.

The constructor enforces the two required fields at construction time — `nFTokenID` and `amount` are non-optional constructor parameters — while `sequence` and `fee` are `std::optional` to accommodate scenarios where the caller sets them later (e.g., when the ledger auto-fills `Sequence`).

The secondary constructor `NFTokenCreateOfferBuilder(std::shared_ptr<STTx const>)` copies an existing signed transaction back into the mutable `object_` via `object_ = *tx`, enabling re-signing or amendment of a previously built transaction without re-specifying every field from scratch.

## `build()` and Signing

`build(PublicKey, SecretKey)` delegates to the protected `sign()` method on `TransactionBuilderBase`, which serialises the `STObject` with `HashPrefix::txSign` prepended (per the XRPL signing protocol), computes an `ed25519`/`secp256k1` signature, and stores it in `sfTxnSignature`. It then constructs an `STTx` by moving `object_`, wraps it in a `shared_ptr<STTx const>`, and forwards it to the `NFTokenCreateOffer` constructor. At that point the transaction is immutable and the builder is spent — `object_` has been moved from.
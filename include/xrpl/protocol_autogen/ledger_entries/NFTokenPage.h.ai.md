# `NFTokenPage.h` — Auto-Generated Ledger Entry Wrapper

**File:** `include/xrpl/protocol_autogen/ledger_entries/NFTokenPage.h`  
**Namespace:** `xrpl::ledger_entries`  
**Generated type:** `ltNFTOKEN_PAGE` (0x0050), RPC name `nft_page`

## Role in the System

This file is part of the `protocol_autogen` layer — a collection of auto-generated C++ wrappers that provide type-safe, structured access to raw XRPL ledger entries. It encodes the `NFTokenPage` ledger object, which is the on-ledger storage unit for Non-Fungible Tokens (NFTs) owned by a single account. Because the broader XRPL ledger stores data as serialized binary objects (`SLE`), these generated wrappers exist to avoid scattered, unchecked field lookups spread across application code.

An account may own any number of NFTs, but the ledger packs them into pages of up to 32 tokens each. Multiple pages for the same account are linked together via their ledger keys, forming a doubly-linked list — which is exactly what `sfPreviousPageMin` and `sfNextPageMin` encode.

## NFTokenPage Page Linking Design

The `sfPreviousPageMin` and `sfNextPageMin` fields are both optional `uint256` values, and their semantics are subtler than their names suggest. Each NFTokenPage's ledger key is derived from the owner's account ID and the minimum NFToken ID that can legally reside in that page. The "min" suffix in the link fields refers to this minimum token ID boundary of the adjacent page — not a simple ordinal sequence number.

When these fields are absent, the page is either the first or last in the chain for that account. The `NFTokenHelpers.cpp` implementation reads and writes these fields extensively during token minting, burning, and merging operations to maintain the doubly-linked structure. The optional nature of both fields cleanly handles the boundary cases: the head page has no `sfPreviousPageMin`, and the tail page has no `sfNextPageMin`.

## Class: `NFTokenPage`

`NFTokenPage` is an immutable, read-only wrapper around a `std::shared_ptr<SLE const>`. It inherits common field accessors (`getKey()`, `getType()`, `getFlags()`, `validate()`) from `LedgerEntryBase` and adds the NFTokenPage-specific getters.

The constructor takes a `shared_ptr<SLE const>` and immediately verifies the entry type matches `ltNFTOKEN_PAGE`, throwing `std::runtime_error` if not. This eager validation is the key safety guarantee: once a `NFTokenPage` object exists, all subsequent field accesses are guaranteed to operate on a correctly-typed SLE, eliminating a whole class of field-mismatch bugs that could occur with raw SLE access.

**Field accessors:**

- `getNFTokens()` — returns `STArray const&` holding the packed NFT objects. This is a required field; there is always at least one token present in a live page.
- `getPreviousTxnID()` / `getPreviousTxnLgrSeq()` — standard audit fields required on all mutable ledger entries, recording the last transaction that modified this page.
- `getPreviousPageMin()` / `getNextPageMin()` — return `protocol_autogen::Optional<uint256>` (i.e., `std::optional<uint256>`). The `has*()` companions test presence before the `isFieldPresent()` call, avoiding the undefined behavior that raw SLE access would cause when reading an absent field.

## Class: `NFTokenPageBuilder`

`NFTokenPageBuilder` uses CRTP by inheriting from `LedgerEntryBuilderBase<NFTokenPageBuilder>`, giving it the common `setLedgerIndex()` and `setFlags()` setters via the base template while enabling method chaining that returns the concrete derived type. The internal state is a free `STObject` (not bound to an SOTemplate) — a deliberate choice documented in `LedgerEntryBuilderBase`: binding to the SOTemplate too early would create `soeDEFAULT` placeholders that cause `applyTemplate()` to throw when the SLE is finally constructed.

The builder has two construction paths:

1. **From required fields** — the primary constructor takes `sfNFTokens` (the token array), `sfPreviousTxnID`, and `sfPreviousTxnLgrSeq`. Optional link fields are set later via `setPreviousPageMin()` / `setNextPageMin()`.

2. **From an existing SLE** — allows constructing a builder by copying the state out of a live ledger entry, which `NFTokenHelpers.cpp` uses when merging or splitting pages during token operations. This path also verifies the entry type and throws on mismatch.

The `build(uint256 const& index)` method consumes the builder's internal `STObject` (via `std::move`) to construct a new `SLE`, then wraps it in a `NFTokenPage`. The caller provides the ledger key explicitly because NFTokenPage keys are computed by `keylet::nftpage()` from the owner AccountID and the minimum token ID boundary — the builder has no way to derive that key internally.

## Relationship to Other Files

- **`LedgerEntryBase.h`** — provides the `sle_` member and common read accessors. `NFTokenPage` holds no additional state beyond what the base class provides.
- **`LedgerEntryBuilderBase.h`** — provides the `object_` member (`STObject`), common setters, and the CRTP scaffolding. All builder-specific NFToken field setters delegate directly to `object_`'s field API.
- **`NFTokenHelpers.cpp`** — the primary consumer of NFTokenPage's structure. It directly manipulates `sfPreviousPageMin` / `sfNextPageMin` on raw SLEs to maintain the doubly-linked page chain. The generated wrappers here exist to provide a cleaner API layer for any code that needs read-only structured access rather than raw mutation.
- **`Indexes.cpp`** — defines `keylet::nftpage_min` and `keylet::nftpage_max` which compute the range of valid keys for a given account's NFT pages; these are the keys passed to `build()`.

## Error Handling and Invariants

Both classes enforce the entry-type invariant at construction time via `std::runtime_error`, ensuring type safety is checked once at the boundary rather than scattered through field-access code. The test suite (`NFTokenPageTests.cpp`) verifies this explicitly: constructing either `NFTokenPage` or `NFTokenPageBuilder` from a `Ticket` SLE throws as expected. The `validate()` method (inherited by both the wrapper and exposed on the builder) delegates to `protocol_autogen::validateSTObject()`, which checks the field set against the official `SOTemplate` for `ltNFTOKEN_PAGE`, providing a second layer of structural correctness checking before any constructed entry reaches the ledger.
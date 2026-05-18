# `Keylet.h` — Type-Safe Ledger Object Locator

## Role in the System

The XRPL ledger state is stored as a SHAMap — an ordered, hash-mapped tree where every entry is addressed by a raw 256-bit key. Accessing an entry by its raw `uint256` alone is type-unsafe: nothing prevents the caller from treating an `AccountRoot` object as an `Offer`, or from confusing the key of one object type with the key of another. `Keylet` exists to close that gap.

`Keylet` is a deliberately minimal struct — just a `uint256 key` and a `LedgerEntryType type` — that bundles together the SHAMap locator of a ledger object with the type that object is expected to have. The name is a portmanteau of "key" and "LET" (LedgerEntryType), coined explicitly to reflect this dual purpose.

## Design Rationale

The pairing of key and type is meaningful at every stage of ledger access. When code performs a lookup it constructs a `Keylet` (typically via one of the factory functions in the `keylet::` namespace defined in `Indexes.h`), then passes it to the ledger view. The view retrieves the raw entry at `key` and, if it finds one, can invoke `check()` to confirm that the entry's type matches the `Keylet`'s type before returning it. This makes it structurally impossible to accidentally retrieve the wrong kind of object through a correctly typed lookup path.

The alternative — passing a raw `uint256` and performing type checks at call sites — is more error-prone. Because different ledger object types use different key derivation formulas (account roots hash from account IDs, offers hash from account ID plus sequence, etc.), keys derived for one type are normally not valid for another, but this is an implicit convention rather than an enforced invariant. `Keylet` makes the convention explicit and checkable.

## `check()` — Type Validation Against a Live Entry

The implementation of `check()` in `Keylet.cpp` reveals the three-case semantics:

```cpp
bool Keylet::check(STLedgerEntry const& sle) const {
    XRPL_ASSERT(sle.getType() != ltANY && sle.getType() != ltCHILD, ...);

    if (type == ltANY)   return true;
    if (type == ltCHILD) return sle.getType() != ltDIR_NODE;
    return sle.getType() == type && sle.key() == key;
}
```

- **`ltANY` keylet**: Acts as a wildcard — any live entry passes. Used in contexts where type enforcement is deliberately relaxed (e.g., raw key lookups via `keylet::unchecked`).
- **`ltCHILD` keylet**: Matches any entry that is *not* a directory node (`ltDIR_NODE`). This reflects the semantics of owner directory children, which can be any ownable object type.
- **Concrete type**: Requires both an exact type match and an exact key match on the SLE. This is the common case and gives the strongest safety guarantee.

Notice that the assertion guards against the reverse confusion: a live `STLedgerEntry` should never carry `ltANY` or `ltCHILD` as its stored type — those are query-side sentinels, not real on-ledger types. The assert fires in debug builds if such a corrupted entry is ever passed.

## Relationship to `Indexes.h`

`Keylet.h` defines the *type*; `Indexes.h` provides the *factory functions* that produce correct, type-specific `Keylet` values. The `keylet::` namespace contains functions like `keylet::account(AccountID const&)`, `keylet::offer(AccountID const&, std::uint32_t seq)`, `keylet::amendments()`, and many others. Each function encapsulates the correct SHA-512Half derivation for its object type and returns a `Keylet` with the matching `LedgerEntryType`. This means callers never compute raw keys by hand — they call the appropriate factory, and the type annotation rides along automatically.

## LedgerEntryType Stability Requirement

`LedgerEntryType` values are stored inside on-ledger objects and are part of the consensus protocol. Changing a type's numeric value would cause nodes running different software versions to compute different hashes for the same ledger state, triggering a hard fork. This constraint is called out explicitly in `LedgerFormats.h`. `Keylet`'s reliance on `LedgerEntryType` means it participates in this stability contract — any code that constructs a `Keylet` with a given type is implicitly depending on that type's numeric identity remaining fixed.

## Summary

`Keylet` is a small but architecturally important type. Its simplicity is intentional: it carries exactly two fields and exposes exactly one non-trivial method. The complexity lives in `Indexes.h`'s factory layer and in the ledger view's lookup plumbing. `Keylet` itself is the stable, shared contract between those two halves — the structure that lets the lookup layer know what it should find before it opens the SHAMap, and verify that what it found matches expectations.
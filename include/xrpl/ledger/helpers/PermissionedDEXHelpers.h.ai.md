# `PermissionedDEXHelpers.h` — Domain Membership Gating for the Permissioned DEX

This header declares two authorization predicates within the `xrpl::permissioned_dex` namespace that enforce membership rules for XRPL's Permissioned DEX feature. The Permissioned DEX allows a domain owner to create a restricted order book accessible only to accounts holding specific, non-expired credentials. These two functions are the sole gatekeepers for that access check, called from both transaction preclaim logic and live order-book traversal.

## The Two Predicates

### `accountInDomain`

```cpp
[[nodiscard]] bool
accountInDomain(ReadView const& view, AccountID const& account, Domain const& domainID);
```

This function resolves a `PermissionedDomain` ledger object by its `domainID`, then tests whether `account` qualifies as a member. The logic has two tiers. First, the domain's `sfOwner` is always considered a member — this avoids a bootstrap problem where an owner couldn't trade in their own domain. Second, for all other accounts, the function iterates the domain's `sfAcceptedCredentials` array and searches for any credential issued to `account` that (a) bears the `lsfAccepted` flag and (b) has not expired according to `credentials::checkExpired` evaluated against the ledger's `parentCloseTime`. If any single credential satisfies both conditions, the account is in-domain. If the domain object itself doesn't exist, the function returns `false` immediately.

Using `parentCloseTime` (the close time of the parent ledger) rather than wall time is deliberate: all XRPL validation is deterministic, and the "current" time for expiry evaluation must be a consensus-agreed timestamp, not a local clock.

### `offerInDomain`

```cpp
[[nodiscard]] bool
offerInDomain(ReadView const& view, uint256 const& offerID, Domain const& domainID, beast::Journal j);
```

This variant checks whether a specific offer object is still legitimately part of a permissioned domain. It is called during order-book traversal in `OfferStream` — not at offer-creation time, but at the moment the order book is being walked to fill a trade. The function reads the offer SLE, verifies that `sfDomainID` is present and matches the expected domain, then delegates the ultimate membership check to `accountInDomain` for the offer's owner.

Several defensive checks guard against invariants that should never be violated by well-formed order books (offer missing, no `sfDomainID` field, mismatched domain ID). These paths are all marked `LCOV_EXCL_LINE` — the comment in the implementation explicitly labels them as impossible under normal operation but retained as safety nets. A separate check enforces consistency on hybrid offers: if `lsfHybrid` is set but `sfAdditionalBooks` is absent, it logs an error via `beast::Journal` and returns `false`.

## Why a Separate Namespace

Both functions live in `xrpl::permissioned_dex` rather than the broader `xrpl::credentials` namespace. The credential namespace handles the general-purpose credential lifecycle (expiry, deletion, DepositPreauth authorization). The permissioned DEX feature has a narrower, domain-centric contract: "is this account or offer currently valid within this specific domain?" Keeping that logic isolated reduces cognitive load for callers and makes it clear which subsystem is being invoked.

## Callers and Enforcement Points

The functions are used at two distinct enforcement stages:

**Transaction preclaim** (`OfferCreate.cpp`, `Payment.cpp`) calls `accountInDomain` to reject transactions before they are applied. If the sender places a domain-scoped offer (`sfDomainID` present), `OfferCreate` verifies they are in that domain and returns `tecNO_PERMISSION` if not. For domain-scoped payments, `Payment.cpp` checks both the sender and the destination, since a domain payment requires both parties to be members.

**Order-book consumption** (`OfferStream.cpp`) calls `offerInDomain` during path-finding traversal to handle the race between offer creation and credential expiry. An offer created by an account with valid credentials might still sit in the book after those credentials expire. When `OfferStream` encounters a domain-tagged offer, it calls `offerInDomain` to re-validate the owner's current membership. If the check fails, the offer is immediately removed from the book (`permRmOffer`) rather than matched. This is the critical correctness guarantee: domain boundaries remain enforced across the ledger's entire lifetime, not just at the moment of offer placement.

## `[[nodiscard]]` as a Safety Invariant

Both declarations carry `[[nodiscard]]`, making it a compile-time error to call either function without consuming the boolean result. Because these functions are security gates — silently ignoring a `false` would allow unauthorized trades — the attribute converts a class of potential audit failures into hard build failures.
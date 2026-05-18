# `PermissionedDEXInvariant.cpp`

## Role in the System

This file implements `ValidPermissionedDEX`, one of roughly twenty invariant checkers that form the last line of defense in XRPL's transaction processing pipeline. The invariant checker framework (assembled as the `InvariantChecks` tuple in `InvariantCheck.h`) runs every checker against every successfully-applied transaction before its ledger mutations become permanent. If any checker returns `false`, the transaction is rejected even if transaction processing logic declared it valid — preventing ledger corruption that a consensus bug or exploit might otherwise cause.

`ValidPermissionedDEX` specifically enforces the integrity of the Permissioned DEX amendment: a feature that allows currency exchanges to be scoped to a specific domain, where participation is gated by credentials. Offers, order-book directory nodes, and payments can carry a `sfDomainID` field (a 256-bit hash) that ties them to a particular permissioned domain. This invariant ensures that the ledger never ends up in a state where a domain-scoped transaction has silently crossed domain boundaries or left malformed objects behind.

## Two-Phase Design

The class follows the `visitEntry` / `finalize` protocol required of all XRPL invariants. The split is architecturally important: ledger entries are streamed past the checker one at a time (there is no random access during iteration), so `visitEntry` accumulates local state across all modified entries, and `finalize` evaluates the complete picture once all entries have been seen.

`visitEntry` inspects every modified `ltDIR_NODE` and `ltOFFER` ledger entry in the `after` snapshot (the post-transaction state). It collects any `sfDomainID` values it encounters into `domains_`, a `hash_set<uint256>`. If it encounters an offer _without_ a `sfDomainID`, it sets the `regularOffers_` flag. For offers marked with `lsfHybrid`, it performs a structural integrity check: a hybrid offer must have both a domain ID and an `sfAdditionalBooks` array of exactly one entry. Failing that constraint sets `badHybrids_`. No errors are raised during this phase — it is purely a data collection pass.

`finalize` then evaluates the accumulated flags and set against the transaction itself. The first thing it does is scope itself: only `ttPAYMENT` and `ttOFFER_CREATE` transactions that succeeded (`isTesSuccess`) are checked. All others return `true` immediately, since those transaction types cannot legitimately produce domain-scoped DEX state.

## What the Invariant Guarantees

Four concrete properties are enforced in `finalize`:

**Hybrid offer integrity.** For an `OfferCreate`, if `visitEntry` flagged any hybrid offer as structurally malformed, the invariant fails with a fatal log entry. A hybrid offer is one that participates simultaneously in both the standard DEX and a permissioned domain's DEX. Its `sfAdditionalBooks` array (with exactly one entry) records its secondary order-book placement. An `sfAdditionalBooks` array of size greater than one — or a hybrid offer missing either `sfDomainID` or `sfAdditionalBooks` — indicates that transaction logic produced an incoherent offer structure.

**Domain existence.** If the transaction itself carries an `sfDomainID`, the invariant verifies via `keylet::permissionedDomain(domain)` that the corresponding `ltPERMISSIONED_DOMAIN` object actually exists in the ledger. A domain-scoped transaction referencing a non-existent domain is categorically invalid.

**Domain isolation.** Every `ltDIR_NODE` and `ltOFFER` touched by the transaction must carry the _same_ domain ID as the transaction itself. If the `domains_` set collected any ID that does not match the transaction's declared domain, the invariant fails. This prevents a domain-scoped payment or order from silently consuming liquidity from a different permissioned domain.

**No contamination of regular offers.** If the transaction specifies a domain, it must not have touched any regular (non-domain) offers. Setting `regularOffers_ = true` during `visitEntry` for any offer lacking `sfDomainID` causes an invariant failure here. This ensures the permissioned and open markets remain strictly segregated at the ledger level.

## Design Tradeoffs and Notable Choices

The decision to skip checking for non-successful transactions is deliberate for domain-isolation logic, but it means the `badHybrids_` check is also silently bypassed for failed `OfferCreate` transactions. The reasoning is that a failed transaction should not have produced any offer objects at all; if it did, that would be caught by other invariants (such as `NoBadOffers`) or by the structural checks during offer-book maintenance. The Permissioned DEX invariant is focused on semantic correctness of the permissioned boundary, not general offer validity.

The use of a `hash_set<uint256>` for `domains_` rather than a single optional value is a deliberate defensive choice. A single domain ID field would fail silently if, for example, two different domain IDs appeared across modified entries. Using a set allows the invariant to detect any case where the transaction touched entries from _more than one_ domain, a condition that would be invisible to a simple equality check against the first observed value.

The `before` parameter in `visitEntry` is unused — the invariant only inspects post-transaction state. This is consistent with the invariant's purpose: it validates that what was written is correct, not what was overwritten.

## Relationship to Sibling Files

`ValidPermissionedDEX` sits alongside `ValidPermissionedDomain` (in `PermissionedDomainInvariant.cpp`) in the invariant suite. The two are complementary: `ValidPermissionedDomain` validates the well-formedness of `ltPERMISSIONED_DOMAIN` objects (credential arrays are sorted, unique, and within size limits), while `ValidPermissionedDEX` validates the integrity of the _usage_ of those domains in trading activity. Together they provide defense-in-depth for the Permissioned DEX feature at the protocol level.
# `PermissionedDEXInvariant.h` — Permissioned DEX Invariant Checker

## Role in the System

This header declares `ValidPermissionedDEX`, one entry in the `InvariantChecks` tuple registered in `InvariantCheck.h`. The XRPL transaction engine runs every invariant checker in that tuple as a post-processing safety net after applying each transaction. If any checker's `finalize()` returns `false`, the engine vetoes the transaction result and marks it with a fatal error, preventing corrupt state from being committed to the ledger.

`ValidPermissionedDEX` enforces the isolation contract of the Permissioned DEX feature: a transaction that specifies a `sfDomainID` must interact exclusively with offers and order book directories tied to that exact domain. It also validates the structural integrity of "hybrid" offers, which participate simultaneously in a domain-specific order book and the global book.

## Two-Phase Visitor Pattern

Like all invariant checkers, `ValidPermissionedDEX` follows the standard two-phase protocol defined by `InvariantChecker_PROTOTYPE`:

**Phase 1 — `visitEntry()`**: Called once for every `SLE` (Serialized Ledger Entry) touched by the transaction, providing snapshots before and after modification. The implementation accumulates three pieces of evidence:

- For any `ltDIR_NODE` or `ltOFFER` entry that carries `sfDomainID`, the domain hash is recorded into `domains_` — a `hash_set<uint256>`.
- For any `ltOFFER` without `sfDomainID`, `regularOffers_` is set to `true`.
- For any `ltOFFER` bearing the `lsfHybrid` flag, the offer is validated for structural completeness: a hybrid offer must carry both `sfDomainID` and `sfAdditionalBooks`, and `sfAdditionalBooks` must contain at most one entry. Violating any of these sets `badHybrids_` to `true`.

**Phase 2 — `finalize()`**: Runs once after all entries have been visited, with access to the full transaction and its result. The check only fires for successful (`isTesSuccess`) `ttPAYMENT` and `ttOFFER_CREATE` transactions. If the transaction itself carries no `sfDomainID`, the check passes immediately — normal (unpermissioned) transactions are not constrained here.

For domain-tagged transactions, three additional assertions are enforced:

1. **Hybrid integrity**: If `badHybrids_` was set during visiting, the invariant fails.
2. **Domain existence**: The domain referenced by the transaction must exist as a `keylet::permissionedDomain` ledger object. This guards against a scenario where a transaction successfully uses a domain that was deleted by the same batch.
3. **Domain isolation**: Every domain hash collected in `domains_` must equal the transaction's own domain. Any foreign domain appearing in touched entries means the transaction bled into the wrong order book.
4. **No regular-offer contamination**: If `regularOffers_` is true, a domain-tagged transaction illegally touched an unpermissioned offer, and the invariant fails.

## Design Decisions

**Why collect domains in a `hash_set` rather than a counter?** The check requires knowing the exact identity of each touched domain, not just a count. Collecting distinct hashes allows a single loop at finalize time to confirm all of them match the transaction's own domain — a domain mismatch is caught regardless of how many foreign domains appear.

**Why skip failed transactions?** Unlike some invariants that must fire on failure (as documented in `InvariantCheck.h`'s `InvariantChecker_PROTOTYPE`), permissioned DEX isolation is only meaningful for state changes that actually took effect. A failed payment cannot have modified any offers, so there is nothing to check. This is an intentional deviation from the general guidance, appropriate because the invariant's purpose is to verify the result of a committed mutation rather than the absence of one.

**Why is `sfAdditionalBooks` size capped at 1?** Hybrid offers bridge a domain-specific book and one additional book. Allowing more than one additional book entry would indicate a malformed or maliciously crafted offer that the transaction processor should never have produced; the invariant catches it as a last resort.

## State Members

| Member | Type | Purpose |
|---|---|---|
| `regularOffers_` | `bool` | Set if any visited `ltOFFER` lacks `sfDomainID` |
| `badHybrids_` | `bool` | Set if any visited hybrid offer is structurally malformed |
| `domains_` | `hash_set<uint256>` | All distinct domain IDs observed on touched offers and directory nodes |

## Relationship to Sibling Invariants

`ValidPermissionedDomain` (declared in the companion header `PermissionedDomainInvariant.h`) checks structural validity of the `PermissionedDomain` ledger object itself — credential list length, sorting, and uniqueness. `ValidPermissionedDEX` is complementary: it operates one layer up, ensuring that order-book activity respects domain boundaries during trade execution rather than checking the domain object's own structure.
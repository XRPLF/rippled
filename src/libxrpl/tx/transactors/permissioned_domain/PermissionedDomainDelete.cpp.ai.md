# PermissionedDomainDelete.cpp

## Role in the System

This file implements the `PermissionedDomainDelete` transactor, responsible for removing a permissioned domain object from the XRPL ledger. Permissioned domains are on-ledger constructs that associate a domain owner with a set of accepted credential types; the Delete transaction is the counterpart to `PermissionedDomainSet`, which creates and mutates those objects. The transactor follows the standard three-phase XRPL pipeline — `preflight`, `preclaim`, `doApply` — ensuring that progressively deeper checks gate each stage.

## The Three-Phase Pipeline

**`preflight`** operates on the raw transaction before any ledger state is consulted. Its only duty here is to reject a zero-valued `sfDomainID`, the 256-bit hash identifying the domain. A zero hash is structurally invalid (analogous to a null pointer), so the transaction is immediately rejected with `temMALFORMED`. This check is deliberately minimal: `preflight` is meant to be cheap and stateless, and the structural validity of the domain ID is the only thing that can be verified without hitting the ledger.

**`preclaim`** bridges the transaction against live ledger state while still being read-only. It resolves the `sfDomainID` to an actual ledger entry via `keylet::permissionedDomain(domain)`. If no such entry exists, `tecNO_ENTRY` is returned — the domain the sender wants to delete simply isn't there. If it does exist, an `XRPL_ASSERT` confirms that both `sfOwner` on the domain object and `sfAccount` on the transaction are present (these are invariants of well-formed objects; failure here indicates ledger or protocol corruption rather than a user error). The critical authorization check then follows: the domain's `sfOwner` must equal the transaction's `sfAccount`. Only the account that created the domain can delete it; any mismatch returns `tecNO_PERMISSION`.

**`doApply`** performs the actual mutation. It has three ordered responsibilities:

1. **Directory removal** — Every owner-controlled ledger object is tracked in the account's owner directory. The domain entry's `sfOwnerNode` field records which page of that directory holds its back-reference. `view().dirRemove()` removes that reference, passing `true` to also clean up the directory page if it becomes empty. Failure here cannot be triggered by a well-formed transaction — it indicates ledger structural corruption — so the block is annotated `LCOV_EXCL_START`/`LCOV_EXCL_STOP` to exclude it from coverage metrics, and it returns `tefBAD_LEDGER` if somehow reached.

2. **Owner count adjustment** — The ledger enforces a reserve requirement proportional to the number of objects an account owns (its `sfOwnerCount`). Creating a domain increments this count (as seen in `PermissionedDomainSet::doApply`), so deletion must decrement it via `adjustOwnerCount(..., -1, ...)`. An assertion guards against underflow: `sfOwnerCount` must be greater than zero before decrementing.

3. **Object erasure** — `view().erase(slePd)` removes the `SLE` (Serialized Ledger Entry) for the permissioned domain from the ledger view. This is the final and irreversible step.

## Design Observations

The separation between `preclaim` (read-only authorization) and `doApply` (mutation) is a deliberate architectural pattern in XRPL's transactor framework. It allows the engine to speculatively run preclaim across multiple transactions without committing side effects, and only advance to `doApply` once the full transaction set has been validated.

The ownership check in `preclaim` is intentionally strict: there is no admin override, no co-ownership, and no delegated authority modeled here. The domain owner is the sole account authorized to delete the domain, which keeps the authorization model simple and auditable.

The `ConsequencesFactory = Normal` setting on the class means this transaction consumes a standard fee and produces normal (non-blocking) consequences in the transaction queue — it will not hold up unrelated transactions from the same account.

Compared to `PermissionedDomainSet`, deletion is much simpler: there is no reserve check (reserves are freed, not consumed), no credential array validation, and no directory insertion. The only bookkeeping asymmetry is that deletion calls `dirRemove` and decrements the owner count, whereas creation calls `dirInsert` and increments it.
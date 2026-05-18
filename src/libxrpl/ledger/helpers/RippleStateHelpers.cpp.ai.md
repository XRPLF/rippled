# `RippleStateHelpers.cpp` — Trust Line Lifecycle and IOU Primitives

This file implements the core ledger primitives for the `ltRIPPLE_STATE` object type — the on-ledger representation of a trust line between two accounts. Everything from reading a balance to creating, mutating, and deleting trust lines flows through here. It also contains the IOU issuance and redemption engine, freeze enforcement, authorization checks, and AMM-specific cleanup operations. The file was assembled from earlier split files (notably `Credit.cpp`), as the inline section comments indicate.

## Trust Line Data Model

An `ltRIPPLE_STATE` entry is shared by exactly two accounts. The protocol resolves ambiguity by canonically assigning roles based on `AccountID` comparison: the account with the numerically lower ID is the "low" side and the one with the higher ID is the "high" side. This ordering is pervasive — every per-side field has a paired `sfLow*`/`sfHigh*` variant, and every flag has a `lsfLow*`/`lsfHigh*` bit. The balance stored in `sfBalance` is always expressed from the low account's perspective: positive means the low account holds the IOU, negative means the high account holds it. Callers must negate if they are the high side.

## Reading Balance and Limit

`creditLimit` fetches `sfLowLimit` or `sfHighLimit` from the trust line, then resets the issuer field of the returned `STAmount` to the querying account. The post-condition asserts confirm that this reattachment is correct. `creditBalance` does the same for `sfBalance`, but negates the result when the querying account is the high side. `creditLimit2` is a thin adapter that converts the result to `IOUAmount` for callers that prefer that type. The design of always returning amounts in "caller's perspective" coordinates — rather than raw ledger-perspective — keeps callers free of the low/high bookkeeping.

## Freeze Hierarchy

Three levels of IOU freeze are implemented, each checking progressively more restrictive conditions:

- **`isFrozen`** is the broadest check. It first reads the issuer's `AccountRoot` for `lsfGlobalFreeze` (the issuer has frozen all lines for this currency), then checks the per-line `lsfHighFreeze`/`lsfLowFreeze` flag set by the issuer side. XRP is always unfrozen.
- **`isIndividualFrozen`** skips the global freeze check and only looks at the per-line flag. This is used when the caller has already confirmed there is no global freeze, or when checking from the issuer's own perspective.
- **`isDeepFrozen`** checks `lsfHighDeepFreeze`/`lsfLowDeepFreeze`, a newer bilateral freeze. Unlike a regular freeze (which only prevents the frozen party from transacting), a deep freeze blocks both sides from moving funds through the line. It returns `false` for self-issued amounts (where `issuer == account`) because the issuer cannot freeze themselves out of their own obligation.

The header exposes `checkDeepFrozen()` as a convenience that returns a `TER` directly, avoiding if/then boilerplate at call sites.

## Trust Line Creation: `trustCreate`

`trustCreate` is the workhorse that writes a new `ltRIPPLE_STATE` SLE to the ledger. It inserts the new entry into both accounts' owner directories via `view.dirInsert()`, capturing the returned 64-bit directory node hints in `sfLowNode`/`sfHighNode` — these are required later by `trustDelete` to find and remove the entry without scanning the full directory. The `bSrcHigh` parameter tells the function which account is the requesting side, which determines which half of every paired field to populate. Importantly, when the peer account has `lsfDefaultRipple` cleared, the function automatically sets the `lsfHighNoRipple`/`lsfLowNoRipple` flag on that side — propagating the peer's preference into the new line without requiring a separate transaction. The deep freeze parameter `bDeepFreeze` was added alongside the feature, making `trustCreate` the single place where all line flags are initialized consistently.

`trustDelete` is the mirror operation. It removes the SLE from both owner directories using the stored node hints, then erases the SLE. Failure to remove from either directory returns `tefBAD_LEDGER`, signaling data corruption.

## IOU Issuance and Redemption

`issueIOU` handles the case where an issuer sends tokens to a holder. It expresses the balance in sender (issuer) perspective terms, subtracts the issued amount, then calls the static `updateTrustLine` to check whether the sender's reserve can be released. After `updateTrustLine` runs, `view.creditHookIOU()` is called — this hook is a no-op in most `ApplyView` subclasses but is intercepted by `PaymentSandbox` to record deferred credits during multi-path payment processing. If no trust line exists yet, `issueIOU` calls `trustCreate` to establish one — this is the only path through which issuance can implicitly create a trust line.

`redeemIOU` is structurally symmetric but asymmetric in its error handling: a missing trust line during redemption is an `tefINTERNAL` fatal error, not a recoverable condition. The invariant is that you cannot hold an IOU balance without an existing trust line; if the line is gone, ledger state is corrupt.

## Automatic Trust Line Cleanup: `updateTrustLine`

This static helper, called from both `issueIOU` and `redeemIOU`, determines whether the sender's side of the trust line has become so minimal that the owner count reserve can be released. The condition is conjunctive across seven criteria: the balance crossed from positive to zero/negative, the sender had a reserve set, the sender's NoRipple flag is inconsistent with the peer's default ripple setting (meaning neither side wants the line kept alive), there is no freeze on the sender's side, the trust limit is zero, and both quality fields are zero. When all are satisfied, `adjustOwnerCount` decrements the sender's `sfOwnerCount` and the reserve flag is cleared. The function returns `true` if the other side also has no reserve — meaning neither account needs the line anymore and the caller should delete it. The caller still sets the final balance on the SLE before deletion, ensuring that the ledger metadata at deletion time accurately reflects the final state even for a line being removed.

## Authorization and Transfer Checks

`requireAuth` implements a three-way `AuthType` distinction. `StrongAuth` requires the trust line to exist before performing any other check, returning `tecNO_LINE` if absent. `WeakAuth` and `Legacy` only enforce authentication if the issuer's `AccountRoot` carries `lsfRequireAuth`, and only when a trust line is present; if the issuer doesn't require auth, they succeed regardless of whether a line exists.

`canTransfer` enforces the rippling rules that govern whether an IOU can flow between two non-issuer parties. If the issuer is one of the endpoints, the transfer is direct and always allowed. Otherwise, it inspects both trust lines for the `lsfHighNoRipple`/`lsfLowNoRipple` flag (from the issuer's perspective), falling back to `lsfDefaultRipple` on the issuer's account when a line does not yet exist. If rippling is disabled on both sides, `terNO_RIPPLE` is returned. This covers the important edge case where a payment might create a new trust line — the default ripple flag provides the "intended" state before the line exists.

## Empty Holding Management

`addEmptyHolding` creates a zero-balance trust line so an account can receive an IOU it does not yet hold. It enforces several preconditions — the issuer must not have a global freeze, the issuer must have `lsfDefaultRipple` set (returning `tecINTERNAL` otherwise, enforcing a protocol invariant), the line must not already exist, and the recipient must have enough XRP balance to cover the incremented owner reserve. It always creates the line with `bNoRipple=true`, which is the conservative default for freshly created holding slots.

`removeEmptyHolding` tears down such a line. For XRP it's a no-op if the balance is non-zero. For IOUs it checks that the balance is zero (for non-issuers), then manually adjusts the owner count for both the low and high reserve holders before calling `trustDelete`. The reserve flags are cleared on the SLE before deletion specifically to make the resulting ledger metadata reflect accurate owner count state at the moment of removal, even though the SLE is about to vanish.

## AMM-Specific Deletions

`deleteAMMTrustLine` wraps `trustDelete` with validation that exactly one side of the trust line is an AMM account (detected by the presence of `sfAMMID` on the `AccountRoot`) and that, if a specific AMM account is required, it is actually a party to the line. This prevents accidentally deleting trust lines during AMM withdrawal operations that don't belong to the target pool. `deleteAMMMPToken` is a simpler companion that removes an MPToken SLE from an AMM account's owner directory during AMM teardown.
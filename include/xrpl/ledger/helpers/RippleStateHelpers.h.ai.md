# `RippleStateHelpers.h` — IOU Trustline Operations

## Role in the System

`RippleStateHelpers.h` is the IOU-specific half of the XRPL's token helper layer. It declares (and `RippleStateHelpers.cpp` implements) every ledger operation that touches a `RippleState` (trustline) object: reading credit limits and balances, checking freeze status, creating and deleting lines, issuing and redeeming IOUs, enforcing authorization, managing zero-balance "holding" lines, and cleaning up AMM-owned lines.

The file sits alongside `TokenHelpers.h` in `include/xrpl/ledger/helpers/`. `TokenHelpers.h` owns the asset-agnostic dispatcher layer — its `accountSend`, `isFrozen`, `requireAuth`, and related functions branch on whether the underlying asset is an `Issue` or an `MPTIssue` and then call into the IOU-specific functions declared here. `RippleStateHelpers.h` is therefore the leaf implementation for IOU paths; callers should generally go through the `TokenHelpers` dispatchers unless they are explicitly working at the IOU level.

## Credit Querying

`creditLimit` reads the `sfLowLimit` or `sfHighLimit` field of a `RippleState` SLE depending on whether `account < issuer` in raw account ID ordering. This low/high ordering is a ledger-wide invariant: the `RippleState` object always stores the "low" account's limit in `sfLowLimit`. Failure to apply this flip would silently return the wrong account's limit. The function normalises the result by rewriting the issuer field of the returned `STAmount` to `account`, making it safe to consume without knowing the binary ordering. `creditLimit2` is a thin convenience wrapper that casts the result to `IOUAmount`.

`creditBalance` applies the same low/high normalisation in reverse: `sfBalance` is always stored in "low account sends to high account" orientation, so when the queried `account` is the high side, the balance is negated before being returned. This means callers always receive a balance expressed as "how much of this currency does `account` hold", regardless of which side of the line they sit on.

## Freeze Semantics

Three distinct freeze levels are modelled:

- **`isIndividualFrozen`** checks only the issuer's side of the trustline flag (`lsfLowFreeze` or `lsfHighFreeze` depending on orientation). It does not check the issuer's global freeze flag, making it suitable for determining whether a specific line has been individually targeted.

- **`isFrozen`** extends this by also consulting the issuer's `AccountRoot` for `lsfGlobalFreeze`. A globally frozen issuer renders every one of their lines frozen, regardless of individual line flags. This is the function that payment paths use to block movement.

- **`isDeepFrozen`** checks `lsfHighDeepFreeze`/`lsfLowDeepFreeze` flags, which represent a stricter freeze that prevents the account from both sending and receiving the currency — a stronger condition than the ordinary freeze, which only prevents outbound transfers. Notably, it returns `false` if `issuer == account`, since an issuer cannot deep-freeze their own balance with themselves.

The `isFrozen` overloads accepting a `depth` parameter exist purely for interface uniformity with the MPT equivalents in `TokenHelpers.h`, where vault-level recursion can require checking multiple layers. For IOUs the depth is unconditionally ignored. Similarly, `isDeepFrozen` has a defaulted `int depth = 0` overload for the same reason — the `Asset`-based dispatcher in `TokenHelpers.h` can forward a depth without needing to know the concrete type.

`checkDeepFrozen` converts the boolean into a `TER` inline, returning `tecFROZEN` or `tesSUCCESS`, as a convenience for transactor preflight code.

## Trust Line Lifecycle

### `trustCreate`

This is the lowest-level entry point for creating a new `RippleState` object and is invoked both directly (e.g., from `TrustSet` transactors) and indirectly from `issueIOU` when a line doesn't already exist. The function takes a `bSrcHigh` flag that tells it which of the two accounts occupies the "high" slot, and it derives `uLowAccountID`/`uHighAccountID` from that. All fields — limits, quality in/out, and the initial balance — are written with side-aware field selectors (`sfLowLimit`/`sfHighLimit`, etc.).

A subtle design point: the caller supplies both `sleAccount` (the account that is being configured — i.e., whose limit and flags are being set) and the `uIndex` of the pre-calculated keylet. The function inserts the SLE into both accounts' owner directories and returns `tecDIR_FULL` if either directory is at capacity. It also sets the peer's `lsfNoRipple` flag on the new line if the peer account does not have `lsfDefaultRipple` enabled, enforcing the rule that the noRipple default is opt-in.

### `trustDelete`

Removes the RippleState SLE after removing directory backlinks from both the low and high owner directories via `view.dirRemove()`. The deletion hints (`sfLowNode`, `sfHighNode`) stored inside the SLE itself avoid a directory traversal lookup. Returns `tefBAD_LEDGER` if either removal fails, which would indicate a corrupt ledger.

## IOU Issuance and Redemption

`issueIOU` adjusts the trustline balance from the issuer's perspective and calls the internal `updateTrustLine` helper to handle the automatic cleanup case: if the sending side's balance crosses zero, its reserve was previously required but may now be unnecessary, and the line may be deleted entirely. If the trustline does not yet exist, `issueIOU` creates it via `trustCreate`, picking up the receiver's `lsfDefaultRipple` setting to determine the `noRipple` initial state.

`redeemIOU` mirrors `issueIOU` but is called when a holder sends currency back toward the issuer. The critical asymmetry: unlike `issueIOU`, `redeemIOU` treats a missing trust line as a fatal internal error (`tefINTERNAL`) because you cannot redeem a balance on a line that doesn't exist. Both functions call `view.creditHookIOU()` after mutating the balance, which allows the `ApplyView` layer to observe the credit event for accounting or hook purposes.

## Authorization and Transfer Checks

`requireAuth` implements three distinct authorization modes via `AuthType`:

- **`StrongAuth`**: If the trust line does not exist, return `tecNO_LINE` immediately. If it does exist and the issuer has `lsfRequireAuth` set, it must also have the corresponding auth flag on the line. This is used in contexts where you must guarantee the line exists before proceeding.

- **`WeakAuth`** and **`Legacy`** (equivalent for IOUs): If `lsfRequireAuth` is set on the issuer and the trust line exists but is not authorized, return `tecNO_AUTH`. If the line doesn't exist and auth is required, return `tecNO_LINE`. But if `lsfRequireAuth` is not set, always return `tesSUCCESS` even if no line exists — appropriate for read-side checks during payment path finding where a line might be created on the fly.

`canTransfer` enforces the rippling rule: a transfer between two non-issuer accounts is blocked only when *both* sides of their respective trust lines with the issuer have `lsfNoRipple` set. If either side allows rippling, or if either account is the issuer, the transfer is permitted. When a trust line doesn't exist for an account, the function falls back to the issuer's `lsfDefaultRipple` flag as the rippling preference.

## Empty Holdings

`addEmptyHolding` and `removeEmptyHolding` manage zero-balance trust lines, primarily used when a transactor (e.g., a DEX limit order) needs to guarantee a destination line exists before funds arrive, without any balance changing hands yet. The comment in the header makes the pairing contract explicit: any transactor calling `addEmptyHolding()` in `doApply` must call `canAddHolding()` (declared in `TokenHelpers.h`) in `preflight`. `addEmptyHolding` checks that the destination can afford the trust line reserve before calling `trustCreate`. `removeEmptyHolding` validates that the balance is actually zero before deleting, returning `tecHAS_OBLIGATIONS` if it is not.

## AMM-Specific Cleanup

`deleteAMMTrustLine` and `deleteAMMMPToken` are called during AMM pool withdrawal. `deleteAMMTrustLine` validates preconditions rigorously: the SLE must be an `ltRIPPLE_STATE`, exactly one of its two sides must be an AMM account (checked by the presence of `sfAMMID` in the `AccountRoot`), and if an `ammAccountID` is supplied, it must match one of the two sides. After calling `trustDelete`, it decrements the non-AMM side's owner count. `deleteAMMMPToken` handles the MPT-based equivalent by removing the `MPToken` SLE from the AMM account's owner directory and erasing it.
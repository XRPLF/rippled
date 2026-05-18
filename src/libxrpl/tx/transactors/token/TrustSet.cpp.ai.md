# `TrustSet.cpp` — TrustSet Transaction Transactor

`TrustSet.cpp` implements the `TrustSet` transaction type for the XRP Ledger, the mechanism by which accounts establish, modify, and implicitly destroy bilateral trust lines (`RippleState` ledger objects). A trust line tracks the credit limit, current balance, quality adjustments, noRipple preference, authorization state, and freeze state for a specific IOU currency between exactly two accounts. Without this transactor, non-XRP tokenized value could not flow through the ledger.

## Transactor Pipeline

`TrustSet` inherits from the `Transactor` base class and implements four static-or-virtual entry points that the framework dispatches in order: `getFlagsMask`, `preflight`, `checkPermission`, `preclaim`, and `doApply`. Each phase has a distinct contract.

**`preflight`** runs without any ledger state access, performing pure structural validation on the submitted transaction fields. It rejects `sfLimitAmount` values that are native (XRP), use the `badCurrency()` sentinel, are negative, or carry a missing/`noAccount()` issuer. It also checks that the `tfSetDeepFreeze` and `tfClearDeepFreeze` flags are only present when the `featureDeepFreeze` amendment has been activated — these flags are defined in `tfTrustSetMask` but their use is amendment-gated here rather than in the flag mask itself, which keeps the mask stable across upgrades.

**`checkPermission`** enforces the delegate authorization model. When the transaction carries an optional `sfDelegate` field, the transactor looks up the `Delegate` ledger entry for `(account, delegate)` and checks it against `checkTxPermission`. If the delegate only holds granular permissions (a subset defined in `Permissions.h`), the method restricts the transaction to operations that map to the three defined granular TrustSet capabilities: `TrustlineAuthorize` (maps to `tfSetfAuth`), `TrustlineFreeze` (maps to `tfSetFreeze`), and `TrustlineUnfreeze` (maps to `tfClearFreeze`). Any other flags in `tfTrustSetPermissionMask` — the complement of those three operations and universal flags — result in `terNO_DELEGATE_PERMISSION`. Crucially, delegates operating on granular permissions also cannot create new trust lines (they are blocked if the line does not yet exist) and cannot change the credit limit.

**`preclaim`** performs context-dependent validation that requires ledger state reads but makes no writes. Notable checks include:

- `tfSetfAuth` is only meaningful if the submitting account has `lsfRequireAuth` set; otherwise it returns `tefNO_AUTH_REQUIRED`.
- The `lsfDisallowIncomingTrustline` flag on the destination is honoured, but `fixDisallowIncomingV1` softens the original overly broad check by allowing the transaction to proceed if a trust line already exists. This was a targeted bug fix: the original implementation blocked issuers from modifying limits on already-live lines when the holder had enabled `DisallowIncoming`.
- Pseudo-accounts (AMM pools, single-asset vaults, loan brokers) are handled with explicit allow-listing. For AMM accounts, a new trust line is permitted only if the currency matches the pool's LP token and the AMM holds a non-zero balance. For vault and loan broker pseudo-accounts, only modifications to an existing line pass; new line creation is denied. All other pseudo-accounts return `tecPSEUDO_ACCOUNT`.
- The deep-freeze invariant is verified in `preclaim` by simulating what the trust line flags would look like after applying the transaction, using the shared `computeFreezeFlags()` helper. The invariant is: `deepFrozen → frozen` (you cannot deep-freeze a line that is not already normally frozen, and you cannot clear normal freeze while deep freeze remains). Trying to both set and clear a freeze flag in the same transaction also fails here.

## The `bHigh` Convention

`RippleState` ledger objects store both sides of a trust relationship in a single shared ledger entry. The low and high sides are determined solely by AccountID lexicographic order: the account whose ID bytes compare lower is the "low" side. Every per-side field — `sfLowLimit`/`sfHighLimit`, `sfLowQualityIn`/`sfHighQualityIn`, `lsfLowFreeze`/`lsfHighFreeze`, `lsfLowReserve`/`lsfHighReserve`, and so on — comes in symmetric pairs. The `bool bHigh = account_ > uDstAccountID` predicate threads through all of `doApply` to select the correct side of every field without duplicating logic. This design means a TrustSet operation by account A and a TrustSet operation by account B both write to the same ledger object but modify disjoint fields.

## Reserve Accounting

Trust lines consume one unit of the owner reserve for each side that holds non-default state. `doApply` computes `bLowReserveSet` and `bHighReserveSet` by inspecting whether any side-specific state deviates from defaults: a non-zero quality, a non-zero credit limit, a frozen flag, a positive balance, or a noRipple setting that differs from the account's `lsfDefaultRipple` flag. When the computed need for a reserve differs from the `lsfLowReserve`/`lsfHighReserve` flags currently stored in the trust line, `adjustOwnerCount` is called to increment or decrement the appropriate account's owner count.

A notable concession to onboarding ergonomics: the reserve requirement is waived when an account owns fewer than two total objects (line 352). This allows gateways to fund new user accounts and establish trust lines without forcing users to hold extra XRP beyond the bare account reserve.

## Auto-deletion of Trust Lines

When `bLowReserveClear && bHighReserveClear` (the `bDefault` flag) becomes true — meaning both sides have zeroed out all non-default state and the balance is zero — `doApply` calls `trustDelete` to remove the `RippleState` entry from the ledger and both accounts' owner directories. This prevents stale zero-balance objects from accumulating. The deletion is unconditional; there is no special user-visible "delete trust line" transaction type.

## `computeFreezeFlags()` Helper

The file-scope helper `computeFreezeFlags()` encapsulates the four-way conditional logic for applying normal and deep-freeze flags to a flag word. It is called identically from both `preclaim` (to simulate the post-transaction state for validation) and `doApply` (to compute the value to write). Sharing this helper guarantees the two phases agree on what the resulting flag state will be, preventing a class of subtle inconsistency bugs where validation passes based on one interpretation and the write produces another.

## Quality Normalization

Quality values encode the exchange rate as a fixed-point number scaled so that `QUALITY_ONE` (1,000,000,000) represents a 1:1 exchange ratio. `doApply` normalizes any quality set to exactly `QUALITY_ONE` back to zero before writing. Zero is the compact representation of "no custom quality" (the field is made absent via `makeFieldAbsent`), so this prevents a user from explicitly writing the default value and wasting 4 bytes of ledger storage per side. The `uQualityOut` normalization happens at line 357–358 during field parsing; the `uQualityIn`/`uQualityOut` values read back from the existing line are similarly normalized at lines 447–451 and 514–517 before the reserve decision is made.

## Interaction with `RippleStateHelpers`

`doApply` delegates the heavy lifting of ledger object construction to `trustCreate` and `trustDelete` from `RippleStateHelpers.cpp`. `trustCreate` inserts the new `ltRIPPLE_STATE` object, adds entries to both accounts' owner directories, initializes all flags from the parameters passed by `doApply`, and calls `adjustOwnerCount` for the creating account. `trustDelete` removes the line from both directories and erases the SLE. This keeps the mutation logic centralized and reusable by other transactors (such as `issueIOU`, which may implicitly create a trust line when issuing tokens to an account that has none).
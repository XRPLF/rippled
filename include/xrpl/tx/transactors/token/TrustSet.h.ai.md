# `TrustSet.h` — TrustSet Transaction Transactor

## Role in the System

`TrustSet` is the transactor that processes `TrustSet` transactions on the XRP Ledger, which are the mechanism by which accounts create, modify, or implicitly delete trust lines. A trust line is the bilateral credit relationship that allows two accounts to hold non-XRP balances denominated in a specific currency; without one, no IOU balance can exist between those parties. This header declares the class and its static dispatch surface; the full implementation lives in `TrustSet.cpp`.

## Inheritance and the Static Dispatch Pattern

`TrustSet` inherits from `Transactor`, which orchestrates the three-phase processing pipeline for all transaction types: `preflight` → `preclaim` → `doApply`. Importantly, `Transactor.h` documents that the static methods (`preflight`, `preclaim`, `getFlagsMask`, `checkPermission`) are **not virtual**. Instead, `Transactor::invokePreflight<T>` is a function template that calls `T::preflight`, `T::getFlagsMask`, etc. directly — compile-time name hiding substitutes for runtime polymorphism. This eliminates vtable overhead on the hot path while still allowing each transactor to override default behavior. Derived classes must not attempt to call `preflight1` or `preflight2` directly; the base template handles that sequencing automatically.

The `ConsequencesFactory` is set to `Normal`, meaning this transaction type neither blocks subsequent transactions from the same account nor requires custom consequence computation.

## `getFlagsMask` — Narrowing Allowed Flags

`getFlagsMask` returns `tfTrustSetMask`, which is the union of all TrustSet-specific flags: `tfSetfAuth`, `tfSetNoRipple`, `tfClearNoRipple`, `tfSetFreeze`, `tfClearFreeze`, `tfSetDeepFreeze`, and `tfClearDeepFreeze`. The base class `preflight1` uses this mask to reject transactions that set any flag not in the mask, catching client errors before any ledger state is touched. `tfSetDeepFreeze` and `tfClearDeepFreeze` are included in the mask at all times, but `preflight` itself returns `temINVALID_FLAG` if those bits are set without the `featureDeepFreeze` amendment being enabled — keeping the mask additive while still gate-checking new features at runtime.

## `preflight` — Stateless Sanity Checks

`preflight` has no ledger access; it operates only on the transaction object. It validates that `sfLimitAmount` is a non-native, non-negative IOU amount in a valid currency with a real issuer account ID. Returning here with a `tem` error code costs the sender nothing (the fee is not charged on `tem` results), so this is the right phase for cheap structural rejections. The deep-freeze flag guard is also here: the flags are parsed, and if `featureDeepFreeze` is not enabled, attempting to set or clear those bits yields `temINVALID_FLAG`.

## `checkPermission` — Delegate Authority Validation

`checkPermission` is called with a read-only `ReadView` and the transaction, allowing it to inspect the ledger without risking mutation. It exists as a separate method (rather than being folded into `preflight` or `preclaim`) because the base `Transactor::checkPermission` does nothing, and only transactors that support delegation need to override it.

When a `sfDelegate` field is present on the transaction, the method locates the `Delegate` ledger object and first checks full transaction-type permission via `checkTxPermission`. If that passes, everything is allowed. If not, it falls through to check **granular permissions** — a finer-grained delegation model:

- If any flag in `tfTrustSetPermissionMask` (everything except `tfSetfAuth`, `tfSetFreeze`, `tfClearFreeze`) is set, the delegate lacks permission.
- If `sfQualityIn` or `sfQualityOut` fields are present, permission is denied — delegates cannot adjust quality settings under granular grants.
- If the trust line does not yet exist, granular permission is insufficient to create a new one.
- `TrustlineAuthorize`, `TrustlineFreeze`, and `TrustlineUnfreeze` are the recognized granular types; each maps to exactly one of the allowed flags.
- Critically, if the transaction attempts to change the limit amount (comparing the submitted `sfLimitAmount` against the current stored limit), it is rejected — granular delegates cannot modify credit limits.

## `preclaim` — Ledger-Aware Pre-Application Checks

`preclaim` reads ledger state to make decisions that `preflight` cannot. Key checks in order:

1. **Auth flag guard**: If `tfSetfAuth` is set but the issuing account does not have `lsfRequireAuth` enabled on its account root, the transaction fails with `tefNO_AUTH_REQUIRED`. Authorization grants only make sense in the context of a permissioned issuance model.

2. **Self-trust rejection**: Setting a trust line to oneself is caught here (`temDST_IS_SRC`).

3. **Destination existence**: When AMM or `SingleAssetVault` features are enabled, the destination account must exist (`tecNO_DST`).

4. **`lsfDisallowIncomingTrustline`**: If the destination has opted out of incoming trust lines, the transaction is normally blocked. The `fixDisallowIncomingV1` amendment relaxes this: if the trust line already exists (the user has a pre-existing relationship), modification is still permitted regardless of the opt-out flag. This amendment corrects an overly restrictive original design.

5. **Pseudo-account restrictions**: AMM accounts, vault accounts, and loan broker accounts are pseudo-accounts that ordinary accounts should not freely create trust lines to. For AMM accounts, trust line creation is only permitted if the currency matches the AMM's LP token and the pool has non-zero liquidity; an existing trust line can always be modified. Vault and loan broker pseudo-accounts permit modification of existing lines but block creation of new ones. Any other pseudo-account type produces `tecPSEUDO_ACCOUNT`.

6. **Deep-freeze invariants**: When `featureDeepFreeze` is active, `preclaim` enforces several invariants: an account with `lsfNoFreeze` cannot set any freeze; simultaneously setting and clearing freeze flags is rejected; and most importantly, a trust line cannot be deep-frozen unless it is also normally frozen (the post-transaction state is simulated via `computeFreezeFlags` to check the constraint rather than relying on current state alone).

## `doApply` — Ledger Mutation

`doApply` is the only virtual (non-static) method and the only place that writes to the ledger view.

**Reserve policy**: The XRPL normally requires an incremental reserve for each ledger object an account owns, but `doApply` exempts accounts with fewer than two owned objects from the reserve requirement for trust line creation. The comment explains the rationale: gateways routinely fund new user accounts and immediately set up trust lines; if the full reserve were enforced, the gateway would need to send enough XRP to cover both the base reserve and the trust line reserve, and the user could pocket the surplus rather than use the gateway. The two-item exemption means a gateway only needs to fund the base reserve.

**Existing trust line path**: The trust line (internally a `RippleState` SLE) stores all attributes from both parties' perspectives. Account IDs are compared numerically to determine which party is the "low" and which is the "high" side, and all field accesses use the appropriate side (`sfLowLimit`/`sfHighLimit`, `sfLowQualityIn`/`sfHighQualityIn`, etc.). Quality values of exactly `QUALITY_ONE` are treated as zero (absent), maintaining a canonical representation. The owner reserve flags (`lsfLowReserve`/`lsfHighReserve`) are recomputed from scratch after every update by evaluating whether the account's side of the trust line is in a non-default state; `adjustOwnerCount` is called with ±1 whenever these flags transition, keeping owner counts consistent with ledger reality.

**Auto-deletion**: If both sides of the trust line end up in fully default state (zero limit, zero quality, no special flags, no balance owed), `trustDelete` is called to remove the `RippleState` SLE and decrement both parties' owner counts. This is an important garbage-collection mechanism that keeps the ledger size in check.

**New trust line path**: If no `RippleState` exists and the transaction carries non-default parameters, `trustCreate` initializes the SLE. If the account's XRP balance falls below the reserve threshold needed to accommodate the new object, the transaction fails with `tecNO_LINE_INSUF_RESERVE`; trying to set default values on a non-existent line is short-circuited as `tecNO_LINE_REDUNDANT`.
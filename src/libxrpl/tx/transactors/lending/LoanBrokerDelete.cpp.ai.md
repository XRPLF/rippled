# `LoanBrokerDelete.cpp` — Tear-down of a LoanBroker and Its Pseudo-Account

## Role in the System

This file implements the `LoanBrokerDelete` transactor, which removes a `LoanBroker` ledger object from the XRPL ledger. A `LoanBroker` is created by `LoanBrokerSet` on behalf of the vault owner; it manages the terms under which loans are issued against a vault, holds a cover-capital buffer in a dedicated pseudo-account, and tracks aggregate debt. Because this object and its associated pseudo-account both consume owner reserves and hold real assets, deletion must go through a careful multi-phase process rather than a simple erase.

`LoanBrokerDelete` is one of the lower-level teardown transactors in the lending protocol — alongside `LoanDelete` — and mirrors the construction logic in `LoanBrokerSet`. Both use the standard XRPL transactor pipeline: `checkExtraFeatures` → `preflight` → `preclaim` → `doApply`.

## Validation Phases

### `checkExtraFeatures`

Delegates immediately to `checkLendingProtocolDependencies`, which verifies that the required feature flags for the lending protocol are all enabled. This gate is shared across every lending transactor and prevents these transactions from being processed on network builds where the protocol is not fully active.

### `preflight`

The only stateless check here is confirming that `sfLoanBrokerID` is not zero. The field identifies the target broker object by its on-ledger key; a zero value means the transaction was malformed before it even reached the network, so `temINVALID` is returned immediately. The thinness of this phase is intentional — most semantic constraints require reading ledger state and belong in `preclaim`.

### `preclaim`

This phase reads the current ledger (without mutating it) and enforces six ordered invariants:

1. **Existence check** — The broker keyed by `sfLoanBrokerID` must exist (`tecNO_ENTRY` otherwise). Subsequent steps rely on reading the broker's fields.

2. **Ownership check** — `sfAccount` on the transaction must match `sfOwner` on the broker SLE (`tecNO_PERMISSION` otherwise). Only the entity that created the broker may destroy it.

3. **Zero owner count** — `sleBroker->at(sfOwnerCount)` must be zero. Non-zero means there are active `Loan` objects backed by this broker; those must be fully repaid and deleted via `LoanDelete` before the broker itself can go (`tecHAS_OBLIGATIONS`).

4. **Vault consistency** — The vault referenced by `sfVaultID` on the broker SLE must still exist in the ledger. If it has disappeared, the ledger is corrupt and `tefBAD_LEDGER` is returned. This path is marked `LCOV_EXCL_START` because a healthy ledger cannot reach it — the vault and broker are created together and the vault cannot be deleted while a broker references it.

5. **Zero debt total (with rounding)** — `sfDebtTotal` on the broker is checked and, if non-zero, rounded toward zero using `roundToAsset` with `Number::towards_zero` at the vault's asset scale. Only if the rounded value is still non-zero does preclaim fail with `tecHAS_OBLIGATIONS`. The code comment explicitly labels this "purely defensive" — the last `LoanDelete` for a broker's final loan should have zeroed `sfDebtTotal`, but floating-point accounting can leave sub-precision dust that rounds to zero at the asset's scale. The rounding direction (toward zero rather than away) is deliberate: it gives dust the benefit of the doubt and allows deletion to proceed when the residual is within one unit of the asset's smallest denomination. This path is also excluded from coverage because it should be unreachable in practice.

6. **Deep-freeze check** — If `sfCoverAvailable` is non-zero, the remaining cover capital will be returned to the broker owner upon deletion. If the owner's account is deep-frozen for the vault's asset type (meaning their ability to receive that asset has been administratively revoked), the transfer cannot happen, so preclaim returns whatever error `checkDeepFrozen` produces. This check is intentionally skipped when `coverAvailable` is zero to avoid blocking no-op refunds.

## `doApply` — Mutation Sequence

The apply phase performs a strict sequence of mutations. Any `tefBAD_LEDGER` returns inside it are guarded with `LCOV_EXCL_LINE` because the preclaim phase already verified the preconditions, and reaching them in apply would indicate a fundamental ledger consistency failure.

1. **Directory removal** — The broker SLE is removed from two owner directories: the broker owner's account directory (keyed by `sfOwnerNode`) and the vault pseudo-account's directory (keyed by `sfVaultNode`). This bidirectional cleanup mirrors the two-directory insertion done in `LoanBrokerSet::doApply`.

2. **Cover refund** — Any `sfCoverAvailable` balance is transferred from the broker's pseudo-account back to `account_` (the transaction submitter / broker owner) via `accountSend` with `WaiveTransferFee::Yes`. Fees are waived because this is a protocol-internal cleanup transfer, not a user-initiated payment.

3. **Empty holding removal** — `removeEmptyHolding` is called on the broker pseudo-account for the vault asset. This removes the trust line or MPToken object that was holding the now-zero cover balance, recovering the owner reserve that was charged when the holding was created.

4. **Pseudo-account sanity checks** — Three defensive assertions confirm the pseudo-account is fully empty before erasure: its `sfBalance` is zero, its `sfOwnerCount` is zero, and it has no owner directory. All three are marked `LCOV_EXCL_LINE` — the preceding steps should have cleared all these obligations, and failure here would indicate a protocol-level bug.

5. **Object erasure** — The broker pseudo-account SLE is erased first, then the broker SLE itself. Order matters here: erasing the pseudo-account's SLE while the broker SLE is still readable means the erasure can reference broker metadata if needed. The reverse order could leave a dangling reference.

6. **Owner count adjustment** — `adjustOwnerCount` is called with `-2` on the broker owner's account SLE. The two-unit decrement balances the two-unit increment in `LoanBrokerSet`: one for the `LoanBroker` object itself, one for the pseudo-account.

7. **Asset association** — `associateAsset(*broker, vaultAsset)` is called after the broker SLE has been erased. This iterates over all `sMD_NeedsAsset` fields on the (now-erased, but still in-scope) SLE and records the asset, enabling downstream components such as fee calculations or reserve tracking to identify which asset was involved. The pattern of calling this after mutation — rather than before — is consistent across all lending transactors.

## Design Notes

The pseudo-account pattern (a synthetic `AccountRoot` SLE that has no signing keys) is used to give the broker a first-class ledger presence as the custodian of cover capital. This allows standard ledger accounting — owner directories, balance checks, trust lines — to apply uniformly rather than requiring custom asset-holding logic inside the `LoanBroker` SLE itself. The cost is the two-object bookkeeping burden (broker object + pseudo-account), which is why the owner count is decremented by exactly two on deletion.

The debt-rounding check at preclaim reflects a broader pattern in the lending protocol: `STNumber` fields that store financial amounts are subject to accumulated imprecision from periodic payment calculations, and the protocol chooses a consistent rounding strategy (`towards_zero` for "does anything remain?") to avoid spurious failures at cleanup time. The explicit comment marking it "purely defensive" signals to future maintainers that if this check ever fires in production, it represents either a bug in `LoanDelete` or an unanticipated path through the loan lifecycle.
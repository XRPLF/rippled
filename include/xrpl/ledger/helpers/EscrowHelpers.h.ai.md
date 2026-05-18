# `include/xrpl/ledger/helpers/EscrowHelpers.h`

This header implements the token-delivery half of escrow resolution for the `featureTokenEscrow` amendment. When an escrow holding an IOU or Multi-Purpose Token (MPT) is either finished (`EscrowFinish`) or cancelled (`EscrowCancel`), the ledger must credit the appropriate account with the locked tokens. That credit is non-trivial: it may require creating a new trust line or MPToken holding object, computing and deducting a transfer fee, and validating that the resulting balance respects the receiver's declared credit limit. `EscrowHelpers.h` centralises all of this logic in a single function, `escrowUnlockApplyHelper`, which is specialized for each asset type.

## Template Architecture: Static Dispatch, No Runtime Overhead

The file declares a primary template `escrowUnlockApplyHelper<T>` constrained by `ValidIssueType` but provides no body for it. Only two explicit full specializations are defined — one for `Issue` (IOUs) and one for `MPTIssue` (MPTs) — both as `inline` functions, making them eligible for inlining into their callers. The calling code in `EscrowFinish.cpp` and `EscrowCancel.cpp` invokes this through `std::visit` on the `Asset` variant, letting the compiler resolve the correct specialization at compile time. This avoids virtual dispatch while keeping both code paths behind a single named interface, which makes future asset types easy to add: provide a new specialization and the `std::visit` dispatch picks it up automatically.

## The `Issue` Specialization (IOU Trust-Line Path)

The `Issue` path mirrors much of what a regular IOU payment does, but with several escrow-specific adaptations.

**Issuer short-circuits.** If the `sender` is the `issuer`, the function returns `tecINTERNAL` — this should never happen because an issuer cannot hold their own obligations as an escrow sender. If the `receiver` is the `issuer`, the function returns `tesSUCCESS` immediately: delivery to the issuer is a redemption event and is handled by the calling transactor at the balance level, not here.

**Trust line creation.** When `createAsset` is `true` — which the callers set only when the account submitting the transaction is also the beneficiary — the function will auto-create the receiver's trust line if it doesn't exist. This gating is a critical policy point: auto-creating a trust line for a third party would violate XRPL's rule that accounts control their own directory and reserve. The reserve check precedes creation: if `xrpBalance < accountReserve(ownerCount + 1)`, the call fails with `tecNO_LINE_INSUF_RESERVE`. The trust line is created via `trustCreate` with `sfDefaultRipple` behaviour derived from the destination's account flags.

When `createAsset` is `false`, the function validates that the pending transfer will not push the receiver's balance above their declared trust-line limit, failing with `tecLIMIT_EXCEEDED` if it would. This check is skipped when the receiver auto-creates their line because a freshly created line starts at zero balance with a limit of zero — the amount would always fail the check spuriously.

**Transfer fee deduction.** The `lockedRate` parameter is the transfer rate snapshotted at escrow creation time. The function caps it at the issuer's *current* transfer rate (`transferRate(view, amount)`), taking the lower of the two — protecting the receiver from a rate *increase* during the escrow's lifetime while preventing the issuer from artificially locking in a high rate. Fee is applied only when neither party is the issuer. Crucially, the fee is deducted *from* the escrowed amount rather than added on top: `finalAmt = amount - fee`. This differs from regular payments, where the sender covers the fee in addition to the principal. In an escrow, the locked principal is fixed; the fee absorbs a portion of it, leaving the receiver with less than the face value.

The actual credit is delivered by `directSendNoFee(view, issuer, receiver, finalAmt, true, journal)`. This call originates the IOU from the issuer's perspective (the correct model for IOU accounting on XRPL) without applying an additional fee layer.

## The `MPTIssue` Specialization (MPT Path)

The MPT path follows the same structure as the IOU path but targets MPToken objects rather than RippleState trust lines.

**MPToken creation.** If the receiver doesn't yet hold an MPToken for the issuance and `createAsset` is `true` and the receiver is not the issuer, the function creates one via `createMPToken` and increments the receiver's owner count with `adjustOwnerCount`. The reserve check uses `tecINSUFFICIENT_RESERVE` (consistent with the MPT framework) rather than the IOU-specific `tecNO_LINE_INSUF_RESERVE`. If no MPToken exists after the creation attempt and the receiver is not the issuer, the function returns `tecNO_PERMISSION`.

**The `fixTokenEscrowV1` bug fix.** The call to `unlockEscrowMPT` passes two amounts: `finalAmt` (the net after fee deduction) for the receiver credit, and a gross amount for the outstanding-balance adjustment. Without `fixTokenEscrowV1`, the gross argument is also `finalAmt`, meaning the MPT's `sfOutstandingAmount` is only reduced by the net delivered amount even though the escrowed principal was the full `amount`. With the fix enabled, the gross argument is the original `amount`, so the outstanding amount correctly decreases by the face value of the escrow, with the fee portion being "burned" from the outstanding supply rather than silently retained.

## `lockedRate` and Cancellation Semantics

`EscrowFinish.cpp` reads `sfTransferRate` from the escrow object and passes it as `lockedRate`. `EscrowCancel.cpp` always passes `parityRate` (1:1, no fee) — the original sender is returning their own tokens to themselves, so charging a transfer fee on cancellation would be economically incoherent.

## Relationship to Sibling Helpers

This file is a pure consumer. It delegates to `RippleStateHelpers.h` for `trustCreate`, `directSendNoFee`, and `transferRate`; to `MPTokenHelpers.h` for `createMPToken` and `unlockEscrowMPT`; and to `AccountRootHelpers.h` for `adjustOwnerCount`. Its sole responsibility is the escrow-specific sequencing and conditional logic that wraps those primitives: reserve checks, rate capping, fee deduction arithmetic, and the `createAsset` gating that enforces account sovereignty over directory entries.
# `PaymentChannelClaim.cpp` — Settling Off-Chain XRP Payments On-Chain

## Role in the System

Payment channels on the XRP Ledger implement a micropayment pattern where two parties can exchange XRP off-chain at high frequency without touching the ledger for every transfer. The channel is opened once (via `PaymentChannelCreate`) with a fixed capacity, and then the sender issues incrementally larger off-chain authorizations — cryptographically signed messages telling the receiver "you may now claim up to N drops from this channel." When the receiver is ready to settle, they submit a `PaymentChannelClaim` transaction to move the net balance on-chain. This file is the implementation of that settlement step.

`PaymentChannelClaim` handles every interacting case: the destination claiming with a signed authorization, the source reclaiming their own channel, cooperative or unilateral close requests, channel expiry enforcement, and optional credential-based deposit authorization.

## Cumulative Balance Model

The most important design decision to understand here is that `sfBalance` is a **cumulative total**, not a per-transaction payment amount. A `PayChan` ledger object tracks two key fields: `sfAmount` (the channel's total funded capacity) and `sfBalance` (how much has been claimed so far, monotonically increasing). When a sender authorizes a claim, they sign a message encoding the channel ID and the new cumulative total — "the receiver may now have claimed 300 XRP total from this channel." The receiver submits this signature along with the desired `sfBalance`.

In `doApply()`, the actual XRP moved is `reqDelta = reqBalance - chanBalance`: only the incremental difference between the requested cumulative balance and the previously settled balance actually transfers. This is why the code returns `tecUNFUNDED_PAYMENT` when `reqBalance <= chanBalance` — there is nothing new to transfer, not a payment failure per se. The cumulative model also prevents replay: a previously submitted claim cannot be resubmitted for double payment because the channel balance only moves forward.

## Validation Pipeline: Three Phases

`PaymentChannelClaim` participates in the standard `Transactor` three-phase pipeline.

**`checkExtraFeatures()`** is called by the Transactor framework before `preflight` as a feature-gate. It prevents `sfCredentialIDs` from appearing in the transaction if the `featureCredentials` amendment is not yet active. This is critical for forward-compatibility: new optional fields must be invisible to nodes running under older rule sets.

**`preflight()`** performs stateless validation with no access to ledger state. The checks here are deliberately ordered and interconnected:
- Both `sfBalance` and `sfAmount`, when present, must be positive XRP (not IOU amounts) and `sfBalance` must not exceed `sfAmount`.
- `tfClose` and `tfRenew` are mutually exclusive flags — a transaction cannot simultaneously request both channel closure and expiry removal.
- If `sfSignature` is present, `sfPublicKey` and `sfBalance` must also be present; a bare signature without the other fields is malformed.
- Signature verification happens entirely in preflight using `serializePayChanAuthorization()`, which serializes the channel keylet and authorized drop amount into a canonical byte string (prefixed with `HashPrefix::paymentChannelClaim`). This is verified against the transaction-supplied public key — but **not** yet against the channel's stored key, because preflight cannot read the ledger.

Credential field structure is validated via `credentials::checkFields()` at the end of `preflight`.

**`preclaim()`** does one thing: if `featureCredentials` is enabled, it validates the credential objects in the current ledger view via `credentials::valid()`. This is deliberately deferred from `preflight` because reading ledger objects to check credential expiration requires state access. Per the comment in `CredentialHelpers.h`, `credentials::valid()` should only be called in `preclaim` (not `doApply`) because it does not remove expired credentials — that cleanup is delegated to `verifyDepositPreauth()` in `doApply`.

## Apply Logic and State Transitions

`doApply()` first locates the `ltPAYCHAN` ledger object by the channel ID in `sfChannel`. If the channel cannot be found, `tecNO_TARGET` is returned.

**Expiry takes priority over everything else.** Before checking permissions or balances, the code checks whether the channel has reached either its absolute `sfCancelAfter` deadline or its owner-settable `sfExpiration`. If the current ledger's `parentCloseTime` has passed either threshold, `closeChannel()` is called unconditionally. This means a claim transaction can serve as the mechanism that triggers channel closure even when it would otherwise do nothing — a holder of an expired channel just needs someone to submit a transaction referencing it.

The permission check (`txAccount != src && txAccount != dst`) enforces that only the two parties involved in the channel can interact with it.

**Balance settlement**: When `sfBalance` is in the transaction:
- The destination must supply a signature — the check `txAccount == dst && !ctx_.tx[~sfSignature]` enforces this. A source can claim their own funds without a signature, but the destination cannot.
- If a signature is present, `ctx_.tx[sfPublicKey]` must match the `sfPublicKey` stored in the channel object. This is the doApply-side complement of the preflight signature check: preflight verified mathematical validity; doApply verifies the key belongs to this specific channel.
- After amount checks, the destination account balance is incremented by `reqDelta` and the channel's `sfBalance` is updated to the new cumulative total.
- `verifyDepositPreauth()` is called before the transfer to honor any `DepositPreauth` restrictions the destination account may have set, including cleaning up any expired credentials in the process.

**`tfRenew` flag**: Only the source (`src == txAccount`) may remove the channel expiry. Clearing `sfExpiration` (set to `std::nullopt`) gives the source a way to retract a previously-issued close request, effectively restarting the channel's active period.

**`tfClose` flag** implements an asymmetric close protocol:
- If the **destination** requests close, or if the channel is fully drained (`sfBalance == sfAmount`), `closeChannel()` runs immediately. The destination has no need to wait — they have already accepted all funds.
- If the **source** requests close, rather than closing immediately (which would let the source steal unclaimed XRP by locking out the destination), a `settleExpiration` is computed as `now + sfSettleDelay`. If no prior expiration exists, or if the current expiration is further in the future than `settleExpiration`, the closer expiration wins. This gives the destination a guaranteed window to submit their highest-authorized claim before the channel can be reclaimed.

**`closeChannel()` teardown** removes the `ltPAYCHAN` object from both the source's and destination's owner directories, returns unclaimed XRP (`sfAmount - sfBalance`) to the source, decrements the source's owner count, and erases the channel from the ledger.

## Relationship to Sibling Transactors

`PaymentChannelCreate` opens the channel and escrows the capacity. `PaymentChannelFund` adds XRP to an existing channel's `sfAmount` and optionally extends its expiry. `PaymentChannelClaim` is the only one that transfers XRP *out* to the destination — the other two only move XRP into or within the channel. All three share the same channel-close logic via `closeChannel()` from `PaymentChannelHelpers`, and all three check expiry at the start of `doApply` before doing anything else.
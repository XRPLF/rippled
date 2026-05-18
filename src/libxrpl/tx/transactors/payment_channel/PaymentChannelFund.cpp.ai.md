# `PaymentChannelFund.cpp` — Adding Funds to an Existing Payment Channel

## Role in the System

`PaymentChannelFund` implements the `PaymentChannelFund` transaction type, which lets a payment channel's owner top up the channel's XRP balance or extend its expiration time. It is one of three payment-channel transactors alongside `PaymentChannelCreate` and `PaymentChannelClaim`, and together they implement XRPL's off-ledger micropayment primitive: the channel sequesters XRP on-ledger while the parties exchange signed claim messages off-ledger, settling periodically. The fund transactor is the "refill" step — once a channel is running low, the owner can inject more XRP without closing and reopening.

## Class Design

`PaymentChannelFund` inherits from `Transactor` and follows the standard XRPL transactor pattern: a static `preflight` for stateless validation, and a `doApply` virtual method for stateful application. The `ConsequencesFactory` is set to `Custom`, which forces `makeTxConsequences` to be called. That method marks the full `sfAmount` (not just the fee) as XRP consumed from the account — this is critical so the transaction engine correctly computes account resource consumption when calculating whether conflicting transactions in the same ledger can coexist.

## `preflight` — Stateless Validation

The preflight check is intentionally minimal: it only verifies that `sfAmount` is a positive XRP value (not IOU). More complex checks that need ledger state (channel existence, permissions, reserve) are deferred to `doApply`. The `isXRP()` guard is necessary because the `sfAmount` field is polymorphic in the broader protocol and could carry non-XRP amounts from a malformed transaction.

## `doApply` — State Transitions and Their Order

The stateful apply phase performs a carefully ordered sequence of checks and mutations. The ordering is intentional and has protocol-level significance.

**Expiry check precedes permission check.** The channel is fetched first, then both `sfCancelAfter` (immutable hard deadline set at creation) and `sfExpiration` (mutable soft deadline) are checked against `parentCloseTime`. If either has passed, `closeChannel()` is called immediately and the transactor returns — *before* checking whether the submitter owns the channel. This design means any transaction touching an expired channel, regardless of who submitted it, will trigger cleanup. The XRPL relies on this: expired channels get garbage-collected by whoever touches them next, not just their owner.

**Only the owner can fund.** After the expiry check, the transactor enforces that `ctx_.tx[sfAccount]` equals the `sfAccount` stored in the channel SLE. Only the original creator-owner may add funds or extend expiration; the recipient has no such right.

**Expiration extension logic.** If the transaction includes an optional `sfExpiration` field, the code computes a `minExpiration` as `parentCloseTime + sfSettleDelay`. It then tightens this floor: if the channel already has an expiration set that is *earlier* than `minExpiration`, `minExpiration` is reduced to match the existing expiration. The effect is that the owner can never set a new expiration *earlier* than the current one, and can never set an expiration that bypasses the settle delay window. This prevents a subtle attack where an owner could sneak in a very short expiration to deprive the recipient of their settle window.

**Reserve and balance checks.** The transactor fetches the owner's account SLE and checks two conditions separately. First it checks `balance < reserve` (`tecINSUFFICIENT_RESERVE`) — the account's current balance doesn't even cover its base reserve, independent of the funding amount. Then it checks `balance < reserve + sfAmount` (`tecUNFUNDED`) — the account can't afford to send the requested amount on top of its reserve. This two-step structure gives callers distinct error codes for "account is already underwater" versus "amount too large."

**Destination existence guard.** Immediately before the ledger write, the code checks that the channel's destination account still exists. This handles a race condition: the destination account could have been deleted after channel creation. Adding more funds to an orphaned channel would be wasteful (those XRP would be unclaimable), so `tecNO_DST` prevents it.

**Ledger update — double-entry invariant.** The actual mutation increments `(*slep)[sfAmount]` (the total funded capacity of the channel) and decrements `(*sle)[sfBalance]` (the owner's account balance) by the same `sfAmount`. Both SLEs are then passed to `ctx_.view().update()`. Note that the channel's `sfAmount` tracks *total capacity*, not *available balance* — `sfBalance` on the channel tracks cumulative payments already claimed. The owner's account `sfBalance` is the XRP actually leaving the owner's pocket.

## Failure Modes and Defensive Checks

The `tefINTERNAL` returned when the source account SLE cannot be fetched is annotated `LCOV_EXCL_LINE`, indicating it is considered unreachable in practice — if a signed transaction has been accepted into the network, the submitting account must exist. The annotation signals that branch is purely defensive. All other error codes are reachable and tested: `tecNO_ENTRY` (channel doesn't exist), `tecNO_PERMISSION` (non-owner), `temBAD_EXPIRATION` (illegal expiration extension), `tecINSUFFICIENT_RESERVE`, `tecUNFUNDED`, and `tecNO_DST`.
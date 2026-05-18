# `PaymentChannelFund.h` — Payment Channel Funding Transactor

`PaymentChannelFund` is one of three transactors governing the XRPL payment channel lifecycle, alongside `PaymentChannelCreate` and `PaymentChannelClaim`. Its specific responsibility is allowing a channel's owner to add XRP to an already-open channel and optionally extend its expiration deadline. It is a thin header that declares the class interface; all logic lives in the corresponding `PaymentChannelFund.cpp`.

## Role in the Payment Channel Subsystem

Payment channels pre-fund a pool of XRP that a sender can draw down off-ledger via signed claims, with the on-ledger balance acting as the upper bound. Once a channel is created with an initial amount, the owner may need to top it up without closing and reopening — this is exactly what `PaymentChannelFund` handles. The transactor also serves as a side-channel for extending the channel's voluntary expiration (`sfExpiration`), independent of adding funds.

## Class Design and Inheritance

`PaymentChannelFund` inherits from `Transactor` and follows the standard three-phase contract the framework imposes:

- **`makeTxConsequences`** (static) — called before ledger application to declare the worst-case cost of the transaction. `PaymentChannelFund` sets `ConsequencesFactory` to `Custom` rather than `Normal` or `Blocker`, because the consequence amount is transaction-specific: it returns a `TxConsequences` carrying the full `sfAmount` value in XRP. This is necessary so the transaction queue can accurately account for funds that will be locked into the channel if this transaction succeeds.

- **`preflight`** (static) — lightweight, context-free validation that runs before any ledger state is consulted. The implementation enforces just two rules: `sfAmount` must be XRP (not an IOU), and it must be strictly positive. Anything more complex deferred to `doApply`.

- **`doApply`** (virtual override) — the ledger-mutating step, executing after preflight and preclaim pass.

## `doApply` Logic

The apply logic follows a layered guard structure:

**1. Channel existence.** The channel is looked up by its ID from `sfChannel`. If the `ltPAYCHAN` object is absent the transaction fails with `tecNO_ENTRY`.

**2. Expiry short-circuit.** Before doing anything else, the code checks both `sfCancelAfter` (the hard deadline set at creation) and `sfExpiration` (the voluntary soft deadline, settable by the owner). If the ledger's parent close time has passed either deadline, `doApply` immediately calls `closeChannel` and returns its result. This means a funding attempt on an expired channel cleanly triggers the channel's closure rather than silently failing — an important correctness property so expired channels don't linger indefinitely.

**3. Ownership enforcement.** Only the account that originally opened the channel (`sfAccount` on the `ltPAYCHAN` object) may fund it or modify its expiration. Any other account receives `tecNO_PERMISSION`. This is intentional: third-party funding would create perverse incentives and complicate dispute resolution.

**4. Optional expiration extension.** If `sfExpiration` is present in the transaction, the transactor enforces a minimum: the new expiration must be at least `parentCloseTime + sfSettleDelay` into the future (giving the destination sufficient time to claim after the owner tries to close). If the channel already has a custom expiration that is *earlier* than this computed minimum, the existing expiration becomes the floor instead. Attempting to set an expiration below this floor returns `temBAD_EXPIRATION`.

**5. Reserve and balance check.** The owner's account balance must cover both the account reserve (based on current `sfOwnerCount`) and the additional `sfAmount` being deposited. Insufficient reserve returns `tecINSUFFICIENT_RESERVE`; insufficient total balance returns `tecUNFUNDED`.

**6. Destination existence.** The transaction checks that the channel's destination account still exists on the ledger before accepting new funds. This prevents XRP from being locked into a channel whose beneficiary has been deleted, which would make the funds unclaimable.

**7. Ledger mutation.** On success, `sfAmount` on the channel object is incremented by the transaction amount, and the owner's `sfBalance` is decremented by the same amount. Both the channel SLE and the account SLE are written back via `ctx_.view().update()`.

## Design Observations

The expiry check triggering `closeChannel` inside a fund attempt is a deliberate defense-in-depth measure: since any account can submit any transaction against any channel ID, this ensures that even a mistaken or adversarial funding attempt against an expired channel performs useful cleanup work. The lack of a `preclaim` override (unlike `PaymentChannelCreate`, which does define one) reflects that all meaningful preconditions can be efficiently checked during apply once the channel object is in hand.

The `Custom` consequences type, shared with `PaymentChannelCreate`, distinguishes these transactors from simple fee-only transactions — the queue must treat them as locking up the declared XRP, not just the transaction fee.
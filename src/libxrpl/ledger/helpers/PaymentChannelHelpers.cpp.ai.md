# `PaymentChannelHelpers.cpp` — Payment Channel Teardown Logic

This file implements a single exported function, `closeChannel`, which serves as the shared teardown routine for XRPL payment channels. It is called from multiple payment-channel transactors (`PaymentChannelClaim` and `PaymentChannelFund`) whenever a channel must be removed from the ledger — whether due to explicit closure, expiration via `cancelAfter`, or a negotiated `expiration` timestamp reaching the current ledger close time.

## Role in the Ledger Helpers Pattern

The `src/libxrpl/ledger/helpers/` directory organizes reusable ledger-mutation logic by object type, keeping it separate from transaction-layer code. `PaymentChannelHelpers.cpp` follows the same pattern as `OfferHelpers.cpp` and `NFTokenHelpers.cpp`: transactors import focused helper functions rather than re-implementing multi-step mutations inline. This keeps each transactor's `doApply()` path focused on policy decisions while delegating structural ledger operations here.

## What `closeChannel` Does

`closeChannel` receives the payment channel's `SLE` (`slep`), the mutable `ApplyView`, its ledger `key` (a `uint256`), and a `beast::Journal` for diagnostics. It performs four ordered mutations and returns a `TER` result code:

**1. Remove from source owner directory.** The channel's `sfOwnerNode` records which directory page holds its entry in the source account's owner directory. `view.dirRemove` locates and removes it. Failure here returns `tefBAD_LEDGER` — this indicates structural ledger corruption that cannot be recovered from, which is why the failure path is marked `LCOV_EXCL_START` (it is unreachable under correct ledger operation and deliberately excluded from coverage requirements).

**2. Conditionally remove from destination owner directory.** The `sfDestinationNode` field is accessed via the tilde operator (`~`) as an optional field. Older payment channels created before the feature that added destination-side owner tracking may lack this field entirely. The conditional check `if (auto const page = (*slep)[~sfDestinationNode])` therefore serves dual purposes: it guards against older channels that have no destination directory entry, and it handles the case where the channel has no destination at all. Failure returns `tefBAD_LEDGER` for the same reasons as above.

**3. Return unspent funds to the source account.** This is the key financial mutation. The channel holds two amounts: `sfAmount` (the total deposited into the channel) and `sfBalance` (the portion already claimed by the recipient). The unspent remainder is `sfAmount - sfBalance`. The source account's XRP balance is updated as:

```
(*sle)[sfBalance] = (*sle)[sfBalance] + (*slep)[sfAmount] - (*slep)[sfBalance]
```

An `XRPL_ASSERT` immediately before this line enforces the invariant `sfAmount >= sfBalance`. This invariant is upheld by all transaction logic that modifies channel state — claim amounts are capped at `sfAmount`, so the channel's running balance can never exceed the deposited total. Using `XRPL_ASSERT` rather than a guarded return signals that a violation here represents an impossible state under correct ledger rules, not a recoverable user error. After adjusting the balance, `adjustOwnerCount` (from `AccountRootHelpers`) decrements the source account's owner count by one, reflecting that the channel no longer counts against its reserve.

**4. Erase the channel SLE.** `view.erase(slep)` removes the channel object itself from the ledger state. This must come last — the SLE is still read for `sfAmount` and `sfBalance` during step 3, and erasing it before that would leave the refund calculation without its inputs.

## Call Sites and Context

In `PaymentChannelClaim::doApply()`, `closeChannel` is invoked in two distinct situations. First, immediately upon discovering that the channel's `cancelAfter` or `expiration` has passed — in this case the entire claim is converted into a pure close, regardless of any balance or signature fields in the transaction. Second, when the `tfClose` flag is set and either the recipient is closing or the channel is fully drained (`sfBalance == sfAmount`); in those cases closure is immediate rather than deferred. In `PaymentChannelFund`, it handles the expired-channel fast path similarly, preventing anyone from adding funds to an already-expired channel while still cleaning up the object.

The function intentionally holds no logic about *when* to close a channel — that judgment belongs to the transactors. `closeChannel` only knows *how* to perform the closure atomically within a given `ApplyView`.
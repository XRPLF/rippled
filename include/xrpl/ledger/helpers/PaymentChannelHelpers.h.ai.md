# `PaymentChannelHelpers.h` — Payment Channel Close Logic

This header is part of the `include/xrpl/ledger/helpers/` collection — a set of thin, focused headers that isolate reusable ledger-mutation operations from individual transaction implementations. The file declares a single function, `closeChannel`, which encapsulates the complete teardown sequence for an XRPL payment channel object.

## Why a Standalone Helper

Payment channel closure is not triggered by a single transaction type. Both `PaymentChannelClaim` and `PaymentChannelFund` need to close an expired channel: the Claim transactor may close on expiry, on a `tfClose` flag, or when a fully-drained channel is claimed; the Fund transactor must refuse to top up a channel whose `cancelAfter` or `expiration` has already passed and instead close it immediately. Rather than duplicate the multi-step teardown logic in each transactor, `closeChannel` is extracted here so both `doApply()` implementations can call it identically.

## `closeChannel` — Anatomy of the Implementation

```cpp
TER closeChannel(
    std::shared_ptr<SLE> const& slep,
    ApplyView& view,
    uint256 const& key,
    beast::Journal j);
```

The implementation (in `src/libxrpl/ledger/helpers/PaymentChannelHelpers.cpp`) performs four distinct ledger mutations in order:

**1. Remove the channel from the source's owner directory.**  
The source account (`sfAccount`) always has a reference to the channel recorded in its owner directory under `sfOwnerNode`. `view.dirRemove` is called unconditionally. If that removal fails, the function returns `tefBAD_LEDGER` — a fatal internal-state error, not a user-visible transaction failure, hence the `LCOV_EXCL` annotation marking it as an unreachable path in normal testing.

**2. Conditionally remove the channel from the destination's owner directory.**  
`sfDestinationNode` is accessed as an optional field (`~sfDestinationNode`). This reflects a protocol evolution: older payment channels did not track the channel in the recipient's owner directory; newer ones do. The presence of the field determines whether a second `dirRemove` call is needed. This pattern avoids breaking backward compatibility while cleanly handling both old and new channel objects.

**3. Refund the unspent balance to the source account.**  
The key arithmetic is:

```cpp
(*sle)[sfBalance] = (*sle)[sfBalance] + (*slep)[sfAmount] - (*slep)[sfBalance];
```

Here `sfAmount` is the total XRP escrowed into the channel, and `sfBalance` on the channel SLE (`slep`) is the cumulative amount already paid out to the destination. The difference is the unspent portion that must be returned to the source. The immediately preceding `XRPL_ASSERT` enforces the invariant that `sfAmount >= sfBalance` — a channel balance can never exceed the channel's total funding. If this assertion were violated it would indicate ledger corruption, making it a justified hard check rather than recoverable error handling.

**4. Decrement the source's owner count and erase the channel object.**  
`adjustOwnerCount(view, sle, -1, j)` reduces the source's owner reservation by one, freeing up the XRP held in reserve for that object. Then `view.erase(slep)` removes the `ltPAYCHAN` entry from the ledger state entirely.

## Error Handling and Failure Modes

`closeChannel` can return three codes:

- `tesSUCCESS` — normal path.
- `tefBAD_LEDGER` — owner directory removal failed; indicates corrupted ledger state. Both removal failure branches are marked `LCOV_EXCL_START/STOP`, signifying they should be unreachable during correct operation and are protected only as a defensive backstop.
- `tefINTERNAL` — the source account SLE could not be found via `view.peek`. Also marked as a theoretically unreachable branch; a payment channel cannot legitimately exist without its source account.

The use of `tef` codes (transaction-execution fatal) rather than `tec` codes is deliberate: these are not conditions the user can correct by adjusting the transaction — they reflect unexpected ledger inconsistency.

## Position in the Helpers Layer

Compared to neighbors like `EscrowHelpers.h`, which contains complex templated logic for multi-asset escrow unlocking, `PaymentChannelHelpers.h` is intentionally minimal. Payment channels are XRP-only, so there is no asset type dispatch, no transfer-rate computation, and no trust-line creation to reason about. The helpers layer exists precisely to avoid embedding these shared operations inside the transactor classes, keeping each transactor focused on authorization and validation rather than raw ledger manipulation.
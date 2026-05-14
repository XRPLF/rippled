#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>

namespace xrpl {

/** Tear down a payment channel and return unspent XRP to its source account.
 *
 *  Performs four ledger mutations in order:
 *  1. Removes the channel from the source's owner directory (`sfOwnerNode`).
 *  2. Conditionally removes the channel from the destination's owner directory
 *     (`sfDestinationNode`) — the field is absent on older channel objects that
 *     predate destination-directory tracking, so its presence is tested before
 *     the removal attempt.
 *  3. Credits the unspent balance (`sfAmount - sfBalance`) back to the source
 *     account.  `sfAmount` is the total XRP escrowed; `sfBalance` is the
 *     cumulative amount already paid to the destination.
 *  4. Decrements the source's owner count and erases the `ltPAYCHAN` SLE.
 *
 *  Called by both `PaymentChannelClaim` and `PaymentChannelFund` whenever a
 *  channel must be closed — on expiry (`cancelAfter`/`expiration` elapsed), on
 *  an explicit `tfClose` flag, or when the channel is fully drained.
 *
 *  @param slep The `ltPAYCHAN` SLE to close; must satisfy
 *      `sfAmount >= sfBalance` (asserted).
 *  @param view The apply view through which all ledger mutations are made.
 *  @param key The ledger key of the channel SLE (used for directory removal).
 *  @param j Journal for fatal-level diagnostic messages on internal errors.
 *  @return `tesSUCCESS` on the normal path; `tefBAD_LEDGER` if an owner
 *      directory removal fails (indicates corrupted ledger state);
 *      `tefINTERNAL` if the source account SLE cannot be found.
 *  @note The `tefBAD_LEDGER` and `tefINTERNAL` branches are annotated
 *      `LCOV_EXCL` — they guard against ledger corruption that cannot occur
 *      during correct operation.
 */
TER
closeChannel(
    std::shared_ptr<SLE> const& slep,
    ApplyView& view,
    uint256 const& key,
    beast::Journal j);

}  // namespace xrpl

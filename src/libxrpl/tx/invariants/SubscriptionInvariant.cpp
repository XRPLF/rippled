#include <xrpl/tx/invariants/SubscriptionInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>

#include <algorithm>

namespace xrpl {

void
ValidSubscription::visitEntry(bool isDelete, SLE::ConstRef before, SLE::ConstRef after)
{
    // A deleted entry imposes no constraint on the resulting ledger.
    if (isDelete || !after || after->getType() != ltSUBSCRIPTION)
        return;

    subscriptions_.emplace_back(before, after);
}

bool
ValidSubscription::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const,
    ReadView const&,
    beast::Journal const& j)
{
    if (!isTesSuccess(result))
        return true;

    return std::ranges::all_of(subscriptions_, [&](auto const& entry) {
        auto const& [before, sleSub] = entry;
        STAmount const balance = sleSub->getFieldAmount(sfBalance);
        STAmount const amount = sleSub->getFieldAmount(sfAmount);

        if (balance.signum() < 0)
        {
            JLOG(j.fatal()) << "Invariant failed: subscription balance is negative";
            return false;
        }

        if (balance.asset() != amount.asset())
        {
            JLOG(j.fatal()) << "Invariant failed: subscription balance and amount "
                               "are denominated in different assets";
            return false;
        }

        if (sleSub->getAccountID(sfAccount) == sleSub->getAccountID(sfDestination))
        {
            JLOG(j.fatal()) << "Invariant failed: subscription account and "
                               "destination are the same";
            return false;
        }

        if (tx.getTxnType() == ttSUBSCRIPTION_CLAIM && before &&
            sleSub->getFieldU32(sfNextClaimTime) < before->getFieldU32(sfNextClaimTime))
        {
            JLOG(j.fatal()) << "Invariant failed: subscription claim moved NextClaimTime backwards";
            return false;
        }

        return true;
    });
}

}  // namespace xrpl

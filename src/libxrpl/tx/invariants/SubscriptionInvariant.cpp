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
#include <xrpl/protocol/XRPAmount.h>

namespace xrpl {

void
ValidSubscription::visitEntry(bool isDelete, SLE::const_ref, SLE::const_ref after)
{
    // A deleted entry imposes no constraint on the resulting ledger.
    if (isDelete || !after || after->getType() != ltSUBSCRIPTION)
        return;

    subscriptions_.push_back(after);
}

bool
ValidSubscription::finalize(
    STTx const&,
    TER const result,
    XRPAmount const,
    ReadView const&,
    beast::Journal const& j)
{
    if (!isTesSuccess(result))
        return true;

    for (auto const& sleSub : subscriptions_)
    {
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
    }

    return true;
}

}  // namespace xrpl

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Indexes.h>

#include <libxrpl/tx/transactors/payment_channel/PayChanHelpers.h>

namespace xrpl {

TER
closeChannel(
    std::shared_ptr<SLE> const& slep,
    ApplyView& view,
    uint256 const& key,
    beast::Journal j)
{
    AccountID const src = (*slep)[sfAccount];
    // Remove PayChan from owner directory
    {
        auto const page = (*slep)[sfOwnerNode];
        if (!view.dirRemove(keylet::ownerDir(src), page, key, true))
        {
            // LCOV_EXCL_START
            JLOG(j.fatal()) << "Could not remove paychan from src owner directory";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }
    }

    // Remove PayChan from recipient's owner directory, if present.
    if (auto const page = (*slep)[~sfDestinationNode])
    {
        auto const dst = (*slep)[sfDestination];
        if (!view.dirRemove(keylet::ownerDir(dst), *page, key, true))
        {
            // LCOV_EXCL_START
            JLOG(j.fatal()) << "Could not remove paychan from dst owner directory";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }
    }

    // Transfer amount back to owner, decrement owner count
    auto const sle = view.peek(keylet::account(src));
    if (!sle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    XRPL_ASSERT(
        (*slep)[sfAmount] >= (*slep)[sfBalance], "xrpl::closeChannel : minimum channel amount");
    (*sle)[sfBalance] = (*sle)[sfBalance] + (*slep)[sfAmount] - (*slep)[sfBalance];
    adjustOwnerCount(view, sle, -1, j);
    view.update(sle);

    // Remove PayChan from ledger
    view.erase(slep);
    return tesSUCCESS;
}

}  // namespace xrpl

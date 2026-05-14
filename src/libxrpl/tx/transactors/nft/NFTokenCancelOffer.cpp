/** @file
 *  Implements the `NFTokenCancelOffer` transactor, which removes one or more
 *  outstanding `NFTokenOffer` ledger objects in a single atomic transaction.
 *
 *  The three phases follow the standard transactor pattern: `preflight`
 *  performs stateless list validation (size bounds, no duplicates), `preclaim`
 *  checks per-offer cancellation rights on a read-only view, and `doApply`
 *  calls `nft::deleteTokenOffer` to remove each offer and release its reserve.
 *  Both `preclaim` and `doApply` silently skip offers that no longer exist,
 *  making the operation idempotent when an offer is consumed between submission
 *  and application.
 */
#include <xrpl/tx/transactors/nft/NFTokenCancelOffer.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/NFTokenHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STVector256.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <algorithm>
#include <memory>

namespace xrpl {

NotTEC
NFTokenCancelOffer::preflight(PreflightContext const& ctx)
{
    if (auto const& ids = ctx.tx[sfNFTokenOffers];
        ids.empty() || (ids.size() > kMAX_TOKEN_OFFER_CANCEL_COUNT))
        return temMALFORMED;

    // In order to prevent unnecessarily overlarge transactions, we
    // disallow duplicates in the list of offers to cancel.
    STVector256 ids = ctx.tx.getFieldV256(sfNFTokenOffers);
    std::ranges::sort(ids);
    if (std::ranges::adjacent_find(ids) != ids.end())
        return temMALFORMED;

    return tesSUCCESS;
}

TER
NFTokenCancelOffer::preclaim(PreclaimContext const& ctx)
{
    auto const account = ctx.tx[sfAccount];

    auto const& ids = ctx.tx[sfNFTokenOffers];

    auto ret = std::ranges::find_if(ids, [&ctx, &account](uint256 const& id) {
        auto const offer = ctx.view.read(keylet::child(id));

        if (!offer)
            return false;

        if (offer->getType() != ltNFTOKEN_OFFER)
            return true;

        if (hasExpired(ctx.view, (*offer)[~sfExpiration]))
            return false;

        if ((*offer)[sfOwner] == account)
            return false;

        if (auto const dest = (*offer)[~sfDestination]; dest == account)
            return false;

        return true;
    });

    if (ret != ids.end())
        return tecNO_PERMISSION;

    return tesSUCCESS;
}

TER
NFTokenCancelOffer::doApply()
{
    for (auto const& id : ctx_.tx[sfNFTokenOffers])
    {
        if (auto offer = view().peek(keylet::nftoffer(id));
            offer && !nft::deleteTokenOffer(view(), offer))
        {
            // LCOV_EXCL_START
            JLOG(j_.fatal()) << "Unable to delete token offer " << id << " (ledger " << view().seq()
                             << ")";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }
    }

    return tesSUCCESS;
}

void
NFTokenCancelOffer::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
NFTokenCancelOffer::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl

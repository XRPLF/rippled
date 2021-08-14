//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2021 Ripple Labs Inc.

  Permission to use, copy, modify, and/or distribute this software for any
  purpose  with  or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
  MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/app/tx/impl/NFTokenCancelOffer.h>
#include <ripple/app/tx/impl/details/NFTokenUtils.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/st.h>
#include <boost/endian/conversion.hpp>

namespace ripple {

NotTEC
NFTokenCancelOffer::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureNonFungibleTokensV1))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfNFTokenCancelOfferMask)
        return temINVALID_FLAG;

    if (auto const& ids = ctx.tx[sfNFTokenOffers];
        ids.empty() || (ids.size() > maxTokenOfferCancelCount))
        return temMALFORMED;

    // In order to prevent unnecessarily overlarge transactions, we
    // disallow duplicates in the list of offers to cancel.
    STVector256 ids = ctx.tx.getFieldV256(sfNFTokenOffers);
    std::sort(ids.begin(), ids.end());
    if (std::adjacent_find(ids.begin(), ids.end()) != ids.end())
        return temMALFORMED;

    return preflight2(ctx);
}

TER
NFTokenCancelOffer::preclaim(PreclaimContext const& ctx)
{
    auto const account = ctx.tx[sfAccount];

    auto const& ids = ctx.tx[sfNFTokenOffers];

    auto ret = std::find_if(
        ids.begin(), ids.end(), [&ctx, &account](uint256 const& id) {
            auto const offer = ctx.view.read(keylet::child(id));

            // If id is not in the ledger we assume the offer was consumed
            // before we got here.
            if (!offer)
                return false;

            // If id is in the ledger but is not an NFTokenOffer, then
            // they have no permission.
            if (offer->getType() != ltNFTOKEN_OFFER)
                return true;

            // Anyone can cancel, if expired
            if (hasExpired(ctx.view, (*offer)[~sfExpiration]))
                return false;

            // The owner can always cancel
            if ((*offer)[sfOwner] == account)
                return false;

            // The recipient can always cancel
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
            JLOG(j_.fatal()) << "Unable to delete token offer " << id
                             << " (ledger " << view().seq() << ")";
            return tefBAD_LEDGER;
        }
    }

    return tesSUCCESS;
}

}  // namespace ripple

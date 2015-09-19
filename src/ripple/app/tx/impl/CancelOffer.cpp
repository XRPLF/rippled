//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <BeastConfig.h>
#include <ripple/app/tx/impl/CancelOffer.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/ledger/View.h>

namespace ripple {

TER
CancelOffer::preflight (PreflightContext const& ctx)
{
    auto const ret = preflight1 (ctx);
    if (!isTesSuccess (ret))
        return ret;

    auto const uTxFlags = ctx.tx.getFlags();

    if (uTxFlags & tfUniversalMask)
    {
        JLOG(ctx.j.trace) << "Malformed transaction: " <<
            "Invalid flags set.";
        return temINVALID_FLAG;
    }

    auto const seq = ctx.tx.getFieldU32 (sfOfferSequence);
    if (! seq)
    {
        JLOG(ctx.j.trace) <<
            "CancelOffer::preflight: missing sequence";
        return temBAD_SEQUENCE;
    }

    return preflight2(ctx);
}

//------------------------------------------------------------------------------

TER
CancelOffer::doApply ()
{
    std::uint32_t const uOfferSequence = tx().getFieldU32 (sfOfferSequence);

    auto const sle = view().read(
        keylet::account(account_));
    if (sle->getFieldU32 (sfSequence) - 1 <= uOfferSequence)
    {
        j_.trace << "Malformed transaction: " <<
            "Sequence " << uOfferSequence << " is invalid.";
        return temBAD_SEQUENCE;
    }

    uint256 const offerIndex (getOfferIndex (account_, uOfferSequence));

    auto sleOffer = view().peek (
        keylet::offer(offerIndex));

    if (sleOffer)
    {
        j_.debug << "Trying to cancel offer #" << uOfferSequence;
        return offerDelete (view(), sleOffer, ctx_.app.journal ("View"));
    }

    j_.debug << "Offer #" << uOfferSequence << " can't be found.";
    return tesSUCCESS;
}

}

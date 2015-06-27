
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

namespace ripple {

TER
CancelOffer::preCheck ()
{
    std::uint32_t const uTxFlags (mTxn.getFlags ());

    if (uTxFlags & tfUniversalMask)
    {
        j_.trace << "Malformed transaction: " <<
            "Invalid flags set.";
        return temINVALID_FLAG;
    }

    std::uint32_t const uOfferSequence = mTxn.getFieldU32 (sfOfferSequence);

    if (!uOfferSequence)
    {
        j_.trace << "Malformed transaction: " <<
            "No sequence specified.";
        return temBAD_SEQUENCE;
    }

    return Transactor::preCheck ();
}

TER
CancelOffer::doApply ()
{
    std::uint32_t const uOfferSequence = mTxn.getFieldU32 (sfOfferSequence);

    if (mTxnAccount->getFieldU32 (sfSequence) - 1 <= uOfferSequence)
    {
        j_.trace << "Malformed transaction: " <<
            "Sequence " << uOfferSequence << " is invalid.";
        return temBAD_SEQUENCE;
    }

    uint256 const offerIndex (getOfferIndex (mTxnAccountID, uOfferSequence));

    auto sleOffer = view().peek (
        keylet::offer(offerIndex));

    if (sleOffer)
    {
        j_.debug << "Trying to cancel offer #" << uOfferSequence;
        return offerDelete (view(), sleOffer);
    }

    j_.debug << "Offer #" << uOfferSequence << " can't be found.";
    return tesSUCCESS;
}

}

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

namespace ripple {


TER CancelOffer::doApply ()
{
    std::uint32_t const uOfferSequence = mTxn.getFieldU32 (sfOfferSequence);
    std::uint32_t const uAccountSequenceNext = mTxnAccount->getFieldU32 (sfSequence);

    m_journal.debug <<
        "uAccountSequenceNext=" << uAccountSequenceNext <<
        " uOfferSequence=" << uOfferSequence;

    std::uint32_t const uTxFlags (mTxn.getFlags ());

    if (uTxFlags & tfUniversalMask)
    {
        m_journal.trace <<
            "Malformed transaction: Invalid flags set.";
        return temINVALID_FLAG;
    }

    if (!uOfferSequence || uAccountSequenceNext - 1 <= uOfferSequence)
    {
        m_journal.trace <<
            "uAccountSequenceNext=" << uAccountSequenceNext <<
            " uOfferSequence=" << uOfferSequence;
        return temBAD_SEQUENCE;
    }

    uint256 const offerIndex (
        Ledger::getOfferIndex (mTxnAccountID, uOfferSequence));

    SLE::pointer sleOffer (
        mEngine->entryCache (ltOFFER, offerIndex));

    if (sleOffer)
    {
        m_journal.debug <<
            "OfferCancel: uOfferSequence=" << uOfferSequence;

        return mEngine->view ().offerDelete (sleOffer);
    }

    m_journal.warning <<
        "OfferCancel: offer not found: " <<
        RippleAddress::createHumanAccountID (mTxnAccountID) <<
        " : " << uOfferSequence <<
        " : " << to_string (offerIndex);

    return tesSUCCESS;
}

}

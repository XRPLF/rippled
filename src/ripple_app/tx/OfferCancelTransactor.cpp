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

SETUP_LOG (OfferCancelTransactor)

TER OfferCancelTransactor::doApply ()
{
    TER             terResult;
    const uint32    uOfferSequence          = mTxn.getFieldU32 (sfOfferSequence);
    const uint32    uAccountSequenceNext    = mTxnAccount->getFieldU32 (sfSequence);

    WriteLog (lsDEBUG, OfferCancelTransactor) << "OfferCancel: uAccountSequenceNext=" << uAccountSequenceNext << " uOfferSequence=" << uOfferSequence;

    const uint32    uTxFlags                = mTxn.getFlags ();

    if (uTxFlags & tfUniversalMask)
    {
        WriteLog (lsINFO, OfferCancelTransactor) << "OfferCancel: Malformed transaction: Invalid flags set.";

        return temINVALID_FLAG;
    }

    if (!uOfferSequence || uAccountSequenceNext - 1 <= uOfferSequence)
    {
        WriteLog (lsINFO, OfferCancelTransactor) << "OfferCancel: uAccountSequenceNext=" << uAccountSequenceNext << " uOfferSequence=" << uOfferSequence;

        terResult   = temBAD_SEQUENCE;
    }
    else
    {
        const uint256   uOfferIndex = Ledger::getOfferIndex (mTxnAccountID, uOfferSequence);
        SLE::pointer    sleOffer    = mEngine->entryCache (ltOFFER, uOfferIndex);

        if (sleOffer)
        {
            WriteLog (lsDEBUG, OfferCancelTransactor) << "OfferCancel: uOfferSequence=" << uOfferSequence;

            terResult   = mEngine->getNodes ().offerDelete (sleOffer);
        }
        else
        {
            WriteLog (lsWARNING, OfferCancelTransactor) << "OfferCancel: offer not found: "
                    << RippleAddress::createHumanAccountID (mTxnAccountID)
                    << " : " << uOfferSequence
                    << " : " << uOfferIndex.ToString ();

            terResult   = tesSUCCESS;
        }
    }

    return terResult;
}

// vim:ts=4

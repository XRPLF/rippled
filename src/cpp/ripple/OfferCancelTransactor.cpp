
SETUP_LOG (OfferCancelTransactor)

TER OfferCancelTransactor::doApply ()
{
    TER             terResult;
    const uint32    uOfferSequence          = mTxn.getFieldU32 (sfOfferSequence);
    const uint32    uAccountSequenceNext    = mTxnAccount->getFieldU32 (sfSequence);

    WriteLog (lsDEBUG, OfferCancelTransactor) << "OfferCancel: uAccountSequenceNext=" << uAccountSequenceNext << " uOfferSequence=" << uOfferSequence;

    const uint32    uTxFlags                = mTxn.getFlags ();

    if (uTxFlags)
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
            WriteLog (lsWARNING, OfferCancelTransactor) << "OfferCancel: uOfferSequence=" << uOfferSequence;

            terResult   = mEngine->getNodes ().offerDelete (sleOffer, uOfferIndex, mTxnAccountID);
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

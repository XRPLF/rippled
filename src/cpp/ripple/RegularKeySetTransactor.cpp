//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

SETUP_LOG (RegularKeySetTransactor)

uint64 RegularKeySetTransactor::calculateBaseFee ()
{
    if ( mTxnAccount
            && (! (mTxnAccount->getFlags () & lsfPasswordSpent))
            && (mSigningPubKey.getAccountID () == mTxnAccountID))
    {
        // flag is armed and they signed with the right account
        return 0;
    }

    return Transactor::calculateBaseFee ();
}


TER RegularKeySetTransactor::doApply ()
{
    Log::out() << "RegularKeySet>";

    const uint32        uTxFlags        = mTxn.getFlags ();

    if (uTxFlags)
    {
        WriteLog (lsINFO, RegularKeySetTransactor) << "RegularKeySet: Malformed transaction: Invalid flags set.";

        return temINVALID_FLAG;
    }

    if (mFeeDue.isZero ())
    {
        mTxnAccount->setFlag (lsfPasswordSpent);
    }

    if (mTxn.isFieldPresent (sfRegularKey))
    {
	uint160 uAuthKeyID = mTxn.getFieldAccount160 (sfRegularKey);
        mTxnAccount->setFieldAccount (sfRegularKey, uAuthKeyID);
    }
    else
    {
        if (mTxnAccount->isFlag (lsfDisableMaster))
	    return tecMASTER_DISABLED;
        mTxnAccount->makeFieldAbsent (sfRegularKey);
    }

    Log::out() << "RegularKeySet<";

    return tesSUCCESS;
}

// vim:ts=4

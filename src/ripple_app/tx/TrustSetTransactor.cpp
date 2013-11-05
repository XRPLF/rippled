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


SETUP_LOG (TrustSetTransactor)

TER TrustSetTransactor::doApply ()
{
    TER         terResult       = tesSUCCESS;
    WriteLog (lsINFO, TrustSetTransactor) << "doTrustSet>";

    const STAmount      saLimitAmount   = mTxn.getFieldAmount (sfLimitAmount);
    const bool          bQualityIn      = mTxn.isFieldPresent (sfQualityIn);
    const bool          bQualityOut     = mTxn.isFieldPresent (sfQualityOut);
    const uint160       uCurrencyID     = saLimitAmount.getCurrency ();
    uint160             uDstAccountID   = saLimitAmount.getIssuer ();
    const bool          bHigh           = mTxnAccountID > uDstAccountID;        // true, iff current is high account.

    uint32              uQualityIn      = bQualityIn ? mTxn.getFieldU32 (sfQualityIn) : 0;
    uint32              uQualityOut     = bQualityOut ? mTxn.getFieldU32 (sfQualityOut) : 0;

    if (!saLimitAmount.isLegalNet ())
        return temBAD_AMOUNT;

    if (bQualityIn && QUALITY_ONE == uQualityIn)
        uQualityIn  = 0;

    if (bQualityOut && QUALITY_ONE == uQualityOut)
        uQualityOut = 0;

    const uint32        uTxFlags        = mTxn.getFlags ();

    if (uTxFlags & tfTrustSetMask)
    {
        WriteLog (lsINFO, TrustSetTransactor) << "doTrustSet: Malformed transaction: Invalid flags set.";

        return temINVALID_FLAG;
    }

    const bool          bSetAuth        = isSetBit (uTxFlags, tfSetfAuth);
    const bool          bSetNoRipple    = isSetBit (uTxFlags, tfSetNoRipple);
    const bool          bClearNoRipple  = isSetBit (uTxFlags, tfClearNoRipple);

    if (bSetAuth && !isSetBit (mTxnAccount->getFieldU32 (sfFlags), lsfRequireAuth))
    {
        WriteLog (lsINFO, TrustSetTransactor) << "doTrustSet: Retry: Auth not required.";

        return tefNO_AUTH_REQUIRED;
    }

    if (saLimitAmount.isNative ())
    {
        WriteLog (lsINFO, TrustSetTransactor) << boost::str (boost::format ("doTrustSet: Malformed transaction: Native credit limit: %s")
                                              % saLimitAmount.getFullText ());

        return temBAD_LIMIT;
    }

    if (saLimitAmount.isNegative ())
    {
        WriteLog (lsINFO, TrustSetTransactor) << "doTrustSet: Malformed transaction: Negative credit limit.";

        return temBAD_LIMIT;
    }

    // Check if destination makes sense.
    if (!uDstAccountID || uDstAccountID == ACCOUNT_ONE)
    {
        WriteLog (lsINFO, TrustSetTransactor) << "doTrustSet: Malformed transaction: Destination account not specified.";

        return temDST_NEEDED;
    }

    if (mTxnAccountID == uDstAccountID)
    {
        SLE::pointer        selDelete   = mEngine->entryCache (ltRIPPLE_STATE, Ledger::getRippleStateIndex (mTxnAccountID, uDstAccountID, uCurrencyID));

        if (selDelete)
        {
            WriteLog (lsWARNING, TrustSetTransactor) << "doTrustSet: Clearing redundant line.";

            return mEngine->getNodes ().trustDelete (selDelete, mTxnAccountID, uDstAccountID);
        }
        else
        {
            WriteLog (lsINFO, TrustSetTransactor) << "doTrustSet: Malformed transaction: Can not extend credit to self.";

            return temDST_IS_SRC;
        }
    }

    SLE::pointer        sleDst          = mEngine->entryCache (ltACCOUNT_ROOT, Ledger::getAccountRootIndex (uDstAccountID));

    if (!sleDst)
    {
        WriteLog (lsINFO, TrustSetTransactor) << "doTrustSet: Delay transaction: Destination account does not exist.";

        return tecNO_DST;
    }

    const uint32        uOwnerCount     = mTxnAccount->getFieldU32 (sfOwnerCount);
    // The reserve required to create the line.
    const uint64        uReserveCreate  = (uOwnerCount < 2) ? 0 : mEngine->getLedger ()->getReserve (uOwnerCount + 1);

    STAmount            saLimitAllow    = saLimitAmount;
    saLimitAllow.setIssuer (mTxnAccountID);

    SLE::pointer        sleRippleState  = mEngine->entryCache (ltRIPPLE_STATE, Ledger::getRippleStateIndex (mTxnAccountID, uDstAccountID, uCurrencyID));

    if (sleRippleState)
    {
        STAmount        saLowBalance;
        STAmount        saLowLimit;
        STAmount        saHighBalance;
        STAmount        saHighLimit;
        uint32          uLowQualityIn;
        uint32          uLowQualityOut;
        uint32          uHighQualityIn;
        uint32          uHighQualityOut;
        const uint160&  uLowAccountID   = !bHigh ? mTxnAccountID : uDstAccountID;
        const uint160&  uHighAccountID  =  bHigh ? mTxnAccountID : uDstAccountID;
        SLE::ref        sleLowAccount   = !bHigh ? mTxnAccount : sleDst;
        SLE::ref        sleHighAccount  =  bHigh ? mTxnAccount : sleDst;

        //
        // Balances
        //

        saLowBalance    = sleRippleState->getFieldAmount (sfBalance);
        saHighBalance   = -saLowBalance;

        //
        // Limits
        //

        sleRippleState->setFieldAmount (!bHigh ? sfLowLimit : sfHighLimit, saLimitAllow);

        saLowLimit  = !bHigh ? saLimitAllow : sleRippleState->getFieldAmount (sfLowLimit);
        saHighLimit =  bHigh ? saLimitAllow : sleRippleState->getFieldAmount (sfHighLimit);

        //
        // Quality in
        //

        if (!bQualityIn)
        {
            // Not setting. Just get it.

            uLowQualityIn   = sleRippleState->getFieldU32 (sfLowQualityIn);
            uHighQualityIn  = sleRippleState->getFieldU32 (sfHighQualityIn);
        }
        else if (uQualityIn)
        {
            // Setting.

            sleRippleState->setFieldU32 (!bHigh ? sfLowQualityIn : sfHighQualityIn, uQualityIn);

            uLowQualityIn   = !bHigh ? uQualityIn : sleRippleState->getFieldU32 (sfLowQualityIn);
            uHighQualityIn  =  bHigh ? uQualityIn : sleRippleState->getFieldU32 (sfHighQualityIn);
        }
        else
        {
            // Clearing.

            sleRippleState->makeFieldAbsent (!bHigh ? sfLowQualityIn : sfHighQualityIn);

            uLowQualityIn   = !bHigh ? 0 : sleRippleState->getFieldU32 (sfLowQualityIn);
            uHighQualityIn  =  bHigh ? 0 : sleRippleState->getFieldU32 (sfHighQualityIn);
        }

        if (QUALITY_ONE == uLowQualityIn)   uLowQualityIn   = 0;

        if (QUALITY_ONE == uHighQualityIn)  uHighQualityIn  = 0;

        //
        // Quality out
        //

        if (!bQualityOut)
        {
            // Not setting. Just get it.

            uLowQualityOut  = sleRippleState->getFieldU32 (sfLowQualityOut);
            uHighQualityOut = sleRippleState->getFieldU32 (sfHighQualityOut);
        }
        else if (uQualityOut)
        {
            // Setting.

            sleRippleState->setFieldU32 (!bHigh ? sfLowQualityOut : sfHighQualityOut, uQualityOut);

            uLowQualityOut  = !bHigh ? uQualityOut : sleRippleState->getFieldU32 (sfLowQualityOut);
            uHighQualityOut =  bHigh ? uQualityOut : sleRippleState->getFieldU32 (sfHighQualityOut);
        }
        else
        {
            // Clearing.

            sleRippleState->makeFieldAbsent (!bHigh ? sfLowQualityOut : sfHighQualityOut);

            uLowQualityOut  = !bHigh ? 0 : sleRippleState->getFieldU32 (sfLowQualityOut);
            uHighQualityOut =  bHigh ? 0 : sleRippleState->getFieldU32 (sfHighQualityOut);
        }

        if (QUALITY_ONE == uLowQualityOut)  uLowQualityOut  = 0;

        if (QUALITY_ONE == uHighQualityOut) uHighQualityOut = 0;

        const bool  bLowReserveSet      = uLowQualityIn || uLowQualityOut || !!saLowLimit || saLowBalance.isPositive ();
        const bool  bLowReserveClear    = !bLowReserveSet;

        const bool  bHighReserveSet     = uHighQualityIn || uHighQualityOut || !!saHighLimit || saHighBalance.isPositive ();
        const bool  bHighReserveClear   = !bHighReserveSet;

        const bool  bDefault            = bLowReserveClear && bHighReserveClear;

        const uint32    uFlagsIn        = sleRippleState->getFieldU32 (sfFlags);
        uint32          uFlagsOut       = uFlagsIn;

        const bool  bLowReserved        = isSetBit (uFlagsIn, lsfLowReserve);
        const bool  bHighReserved       = isSetBit (uFlagsIn, lsfHighReserve);

        bool        bReserveIncrease    = false;

        if (bSetAuth)
        {
            uFlagsOut           |= (bHigh ? lsfHighAuth : lsfLowAuth);
        }

        if (bSetNoRipple && !bClearNoRipple)
        {
            uFlagsOut           |= (bHigh ? lsfHighNoRipple : lsfLowNoRipple);
        }
        else if (bClearNoRipple && !bSetNoRipple)
        {
            uFlagsOut           &= ~(bHigh ? lsfHighNoRipple : lsfLowNoRipple);
        }

        if (bLowReserveSet && !bLowReserved)
        {
            // Set reserve for low account.

            mEngine->getNodes ().ownerCountAdjust (uLowAccountID, 1, sleLowAccount);
            uFlagsOut           |= lsfLowReserve;

            if (!bHigh)
                bReserveIncrease    = true;
        }

        if (bLowReserveClear && bLowReserved)
        {
            // Clear reserve for low account.

            mEngine->getNodes ().ownerCountAdjust (uLowAccountID, -1, sleLowAccount);
            uFlagsOut   &= ~lsfLowReserve;
        }

        if (bHighReserveSet && !bHighReserved)
        {
            // Set reserve for high account.

            mEngine->getNodes ().ownerCountAdjust (uHighAccountID, 1, sleHighAccount);
            uFlagsOut   |= lsfHighReserve;

            if (bHigh)
                bReserveIncrease    = true;
        }

        if (bHighReserveClear && bHighReserved)
        {
            // Clear reserve for high account.

            mEngine->getNodes ().ownerCountAdjust (uHighAccountID, -1, sleHighAccount);
            uFlagsOut   &= ~lsfHighReserve;
        }

        if (uFlagsIn != uFlagsOut)
            sleRippleState->setFieldU32 (sfFlags, uFlagsOut);

        if (bDefault || CURRENCY_BAD == uCurrencyID)
        {
            // Delete.

            terResult   = mEngine->getNodes ().trustDelete (sleRippleState, uLowAccountID, uHighAccountID);
        }
        else if (bReserveIncrease
                 && mPriorBalance.getNValue () < uReserveCreate) // Reserve is not scaled by load.
        {
            WriteLog (lsINFO, TrustSetTransactor) << "doTrustSet: Delay transaction: Insufficent reserve to add trust line.";

            // Another transaction could provide XRP to the account and then this transaction would succeed.
            terResult   = tecINSUF_RESERVE_LINE;
        }
        else
        {
            mEngine->entryModify (sleRippleState);

            WriteLog (lsINFO, TrustSetTransactor) << "doTrustSet: Modify ripple line";
        }
    }
    // Line does not exist.
    else if (!saLimitAmount                                     // Setting default limit.
             && (!bQualityIn || !uQualityIn)                         // Not setting quality in or setting default quality in.
             && (!bQualityOut || !uQualityOut))                      // Not setting quality out or setting default quality out.
    {
        WriteLog (lsINFO, TrustSetTransactor) << "doTrustSet: Redundant: Setting non-existent ripple line to defaults.";

        return tecNO_LINE_REDUNDANT;
    }
    else if (mPriorBalance.getNValue () < uReserveCreate)       // Reserve is not scaled by load.
    {
        WriteLog (lsINFO, TrustSetTransactor) << "doTrustSet: Delay transaction: Line does not exist. Insufficent reserve to create line.";

        // Another transaction could create the account and then this transaction would succeed.
        terResult   = tecNO_LINE_INSUF_RESERVE;
    }
    else if (CURRENCY_BAD == uCurrencyID)
    {
        terResult   = temBAD_CURRENCY;
    }
    else
    {
        STAmount    saBalance   = STAmount (uCurrencyID, ACCOUNT_ONE);  // Zero balance in currency.

        WriteLog (lsINFO, TrustSetTransactor) << "doTrustSet: Creating ripple line: "
                                              << Ledger::getRippleStateIndex (mTxnAccountID, uDstAccountID, uCurrencyID).ToString ();

        // Create a new ripple line.
        terResult   = mEngine->getNodes ().trustCreate (
                          bHigh,
                          mTxnAccountID,
                          uDstAccountID,
                          Ledger::getRippleStateIndex (mTxnAccountID, uDstAccountID, uCurrencyID),
                          mTxnAccount,
                          bSetAuth,
                          bSetNoRipple && !bClearNoRipple,
                          saBalance,
                          saLimitAllow,       // Limit for who is being charged.
                          uQualityIn,
                          uQualityOut);
    }

    WriteLog (lsINFO, TrustSetTransactor) << "doTrustSet<";

    return terResult;
}

// vim:ts=4

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

TER TrustSetTransactor::doApply ()
{
    TER terResult = tesSUCCESS;

    STAmount const saLimitAmount (mTxn.getFieldAmount (sfLimitAmount));
    bool const bQualityIn (mTxn.isFieldPresent (sfQualityIn));
    bool const bQualityOut (mTxn.isFieldPresent (sfQualityOut));
    
    uint160 const uCurrencyID (saLimitAmount.getCurrency ());
    uint160 uDstAccountID (saLimitAmount.getIssuer ());

    // true, iff current is high account.
    bool const bHigh = mTxnAccountID > uDstAccountID;

    std::uint32_t uQualityIn (bQualityIn ? mTxn.getFieldU32 (sfQualityIn) : 0);
    std::uint32_t uQualityOut (bQualityOut ? mTxn.getFieldU32 (sfQualityOut) : 0);

    if (!saLimitAmount.isLegalNet ())
        return temBAD_AMOUNT;

    if (bQualityIn && QUALITY_ONE == uQualityIn)
        uQualityIn = 0;

    if (bQualityOut && QUALITY_ONE == uQualityOut)
        uQualityOut = 0;

    std::uint32_t const uTxFlags = mTxn.getFlags ();

    if (uTxFlags & tfTrustSetMask)
    {
        m_journal.trace <<
            "Malformed transaction: Invalid flags set.";
        return temINVALID_FLAG;
    }

    bool const bSetAuth = isSetBit (uTxFlags, tfSetfAuth);
    bool const bSetNoRipple = isSetBit (uTxFlags, tfSetNoRipple);
    bool const bClearNoRipple  = isSetBit (uTxFlags, tfClearNoRipple);

    if (bSetAuth && !isSetBit (mTxnAccount->getFieldU32 (sfFlags), lsfRequireAuth))
    {
        m_journal.trace << 
            "Retry: Auth not required.";
        return tefNO_AUTH_REQUIRED;
    }

    if (saLimitAmount.isNative ())
    {
        m_journal.trace <<
            "Malformed transaction: Native credit limit: " <<
            saLimitAmount.getFullText ();
        return temBAD_LIMIT;
    }

    if (saLimitAmount.isNegative ())
    {
        m_journal.trace << 
            "Malformed transaction: Negative credit limit.";
        return temBAD_LIMIT;
    }

    // Check if destination makes sense.
    if (!uDstAccountID || uDstAccountID == ACCOUNT_ONE)
    {
        m_journal.trace << 
            "Malformed transaction: Destination account not specified.";

        return temDST_NEEDED;
    }

    if (mTxnAccountID == uDstAccountID)
    {
        SLE::pointer selDelete (
            mEngine->entryCache (ltRIPPLE_STATE, 
                Ledger::getRippleStateIndex (
                    mTxnAccountID, uDstAccountID, uCurrencyID)));
        
        if (selDelete)
        {
            m_journal.warning << 
                "Clearing redundant line.";

            return mEngine->getNodes ().trustDelete (
                selDelete, mTxnAccountID, uDstAccountID);
        }
        else
        {
            m_journal.trace <<
                "Malformed transaction: Can not extend credit to self.";
            return temDST_IS_SRC;
        }
    }

    SLE::pointer sleDst (mEngine->entryCache (
        ltACCOUNT_ROOT, Ledger::getAccountRootIndex (uDstAccountID)));

    if (!sleDst)
    {
        m_journal.trace << 
            "Delay transaction: Destination account does not exist.";
        return tecNO_DST;
    }

    std::uint32_t const uOwnerCount (mTxnAccount->getFieldU32 (sfOwnerCount));
    // The reserve required to create the line.
    std::uint64_t const uReserveCreate = 
        (uOwnerCount < 2) 
            ? 0 
            : mEngine->getLedger ()->getReserve (uOwnerCount + 1);

    STAmount saLimitAllow = saLimitAmount;
    saLimitAllow.setIssuer (mTxnAccountID);

    SLE::pointer sleRippleState (mEngine->entryCache (ltRIPPLE_STATE,
        Ledger::getRippleStateIndex (mTxnAccountID, uDstAccountID, uCurrencyID)));

    if (sleRippleState)
    {
        STAmount        saLowBalance;
        STAmount        saLowLimit;
        STAmount        saHighBalance;
        STAmount        saHighLimit;
        std::uint32_t   uLowQualityIn;
        std::uint32_t   uLowQualityOut;
        std::uint32_t   uHighQualityIn;
        std::uint32_t   uHighQualityOut;
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

        std::uint32_t const uFlagsIn (sleRippleState->getFieldU32 (sfFlags));
        std::uint32_t uFlagsOut (uFlagsIn);

        if (bSetNoRipple && !bClearNoRipple && (bHigh ? saHighBalance : saLowBalance).isGEZero())
        {
            uFlagsOut           |= (bHigh ? lsfHighNoRipple : lsfLowNoRipple);
        }
        else if (bClearNoRipple && !bSetNoRipple)
        {
            uFlagsOut           &= ~(bHigh ? lsfHighNoRipple : lsfLowNoRipple);
        }

        if (QUALITY_ONE == uLowQualityOut)  uLowQualityOut  = 0;

        if (QUALITY_ONE == uHighQualityOut) uHighQualityOut = 0;


        bool const  bLowReserveSet      = uLowQualityIn || uLowQualityOut ||
                                          isSetBit (uFlagsOut, lsfLowNoRipple) ||
                                          !!saLowLimit || saLowBalance.isPositive ();
        bool const  bLowReserveClear    = !bLowReserveSet;

        bool const  bHighReserveSet     = uHighQualityIn || uHighQualityOut ||
                                          isSetBit (uFlagsOut, lsfHighNoRipple) ||
                                          !!saHighLimit || saHighBalance.isPositive ();
        bool const  bHighReserveClear   = !bHighReserveSet;

        bool const  bDefault            = bLowReserveClear && bHighReserveClear;

        bool const  bLowReserved        = isSetBit (uFlagsIn, lsfLowReserve);
        bool const  bHighReserved       = isSetBit (uFlagsIn, lsfHighReserve);

        bool        bReserveIncrease    = false;

        if (bSetAuth)
        {
            uFlagsOut           |= (bHigh ? lsfHighAuth : lsfLowAuth);
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
            m_journal.trace <<
                "Delay transaction: Insufficent reserve to add trust line.";
            
            // Another transaction could provide XRP to the account and then
            // this transaction would succeed.
            terResult   = tecINSUF_RESERVE_LINE;
        }
        else
        {
            mEngine->entryModify (sleRippleState);

            m_journal.trace <<
                "Modify ripple line";
        }
    }
    // Line does not exist.
    else if (!saLimitAmount                                     // Setting default limit.
             && (!bQualityIn || !uQualityIn)                         // Not setting quality in or setting default quality in.
             && (!bQualityOut || !uQualityOut))                      // Not setting quality out or setting default quality out.
    {
        m_journal.trace << 
            "Redundant: Setting non-existent ripple line to defaults.";
        return tecNO_LINE_REDUNDANT;
    }
    else if (mPriorBalance.getNValue () < uReserveCreate)       // Reserve is not scaled by load.
    {
        m_journal.trace << 
            "Delay transaction: Line does not exist. Insufficent reserve to create line.";

        // Another transaction could create the account and then this transaction would succeed.
        terResult = tecNO_LINE_INSUF_RESERVE;
    }
    else if (CURRENCY_BAD == uCurrencyID)
    {
        terResult   = temBAD_CURRENCY;
    }
    else
    {
        // Zero balance in currency.
        STAmount saBalance (STAmount (uCurrencyID, ACCOUNT_ONE));  


        m_journal.trace << 
            "doTrustSet: Creating ripple line: " <<
            Ledger::getRippleStateIndex (mTxnAccountID, uDstAccountID, uCurrencyID).ToString ();

        // Create a new ripple line.
        terResult = mEngine->getNodes ().trustCreate (
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

    return terResult;
}

}

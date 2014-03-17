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

TER PaymentTransactor::doApply ()
{
    // Ripple if source or destination is non-native or if there are paths.
    std::uint32_t const uTxFlags = mTxn.getFlags ();
    bool const bPartialPayment = isSetBit (uTxFlags, tfPartialPayment);
    bool const bLimitQuality = isSetBit (uTxFlags, tfLimitQuality);
    bool const bNoRippleDirect = isSetBit (uTxFlags, tfNoRippleDirect);
    bool const bPaths = mTxn.isFieldPresent (sfPaths);
    bool const bMax = mTxn.isFieldPresent (sfSendMax);
    uint160 const uDstAccountID = mTxn.getFieldAccount160 (sfDestination);
    STAmount const saDstAmount = mTxn.getFieldAmount (sfAmount);
    STAmount const saMaxAmount = bMax
                                   ? mTxn.getFieldAmount (sfSendMax)
                                   : saDstAmount.isNative ()
                                   ? saDstAmount
                                   : STAmount (saDstAmount.getCurrency (),
                                        mTxnAccountID,
                                        saDstAmount.getMantissa (),
                                        saDstAmount.getExponent (),
                                        saDstAmount.isNegative ());
    uint160 const uSrcCurrency = saMaxAmount.getCurrency ();
    uint160 const uDstCurrency = saDstAmount.getCurrency ();
    bool const bXRPDirect = uSrcCurrency.isZero () && uDstCurrency.isZero ();

    m_journal.info << 
        "saMaxAmount=" << saMaxAmount.getFullText () <<
        " saDstAmount=" << saDstAmount.getFullText ();

    if (!saDstAmount.isLegalNet () || !saMaxAmount.isLegalNet ())
        return temBAD_AMOUNT;

    if (uTxFlags & tfPaymentMask)
    {
        m_journal.info << 
            "Malformed transaction: Invalid flags set.";

        return temINVALID_FLAG;
    }
    else if (!uDstAccountID)
    {
        m_journal.info <<
            "Malformed transaction: Payment destination account not specified.";

        return temDST_NEEDED;
    }
    else if (bMax && !saMaxAmount.isPositive ())
    {
        m_journal.info <<
            "Malformed transaction: bad max amount: " << saMaxAmount.getFullText ();

        return temBAD_AMOUNT;
    }
    else if (!saDstAmount.isPositive ())
    {
        m_journal.info <<
            "Malformed transaction: bad dst amount: " << saDstAmount.getFullText ();

        return temBAD_AMOUNT;
    }
    else if (CURRENCY_BAD == uSrcCurrency || CURRENCY_BAD == uDstCurrency)
    {
        m_journal.info <<
            "Malformed transaction: Bad currency.";

        return temBAD_CURRENCY;
    }
    else if (mTxnAccountID == uDstAccountID && uSrcCurrency == uDstCurrency && !bPaths)
    {
        m_journal.info << 
            "Malformed transaction: Redundant transaction:" <<
            " src=" << mTxnAccountID.ToString () <<
            " dst=" << uDstAccountID.ToString () <<
            " src_cur=" << uSrcCurrency.ToString () <<
            " dst_cur=" << uDstCurrency.ToString ();

        return temREDUNDANT;
    }
    else if (bMax && saMaxAmount == saDstAmount && saMaxAmount.getCurrency () == saDstAmount.getCurrency ())
    {
        m_journal.info <<
            "Malformed transaction: Redundant SendMax.";

        return temREDUNDANT_SEND_MAX;
    }
    else if (bXRPDirect && bMax)
    {
        m_journal.info <<
            "Malformed transaction: SendMax specified for XRP to XRP.";

        return temBAD_SEND_XRP_MAX;
    }
    else if (bXRPDirect && bPaths)
    {
        m_journal.info <<
            "Malformed transaction: Paths specified for XRP to XRP.";

        return temBAD_SEND_XRP_PATHS;
    }
    else if (bXRPDirect && bPartialPayment)
    {
        m_journal.info <<
            "Malformed transaction: Partial payment specified for XRP to XRP.";

        return temBAD_SEND_XRP_PARTIAL;
    }
    else if (bXRPDirect && bLimitQuality)
    {
        m_journal.info <<
            "Malformed transaction: Limit quality specified for XRP to XRP.";

        return temBAD_SEND_XRP_LIMIT;
    }
    else if (bXRPDirect && bNoRippleDirect)
    {
        m_journal.info <<
            "Malformed transaction: No ripple direct specified for XRP to XRP.";

        return temBAD_SEND_XRP_NO_DIRECT;
    }

    SLE::pointer sleDst (mEngine->entryCache (
        ltACCOUNT_ROOT, Ledger::getAccountRootIndex (uDstAccountID)));

    if (!sleDst)
    {
        // Destination account does not exist.

        if (!saDstAmount.isNative ())
        {
            m_journal.info <<
                "Delay transaction: Destination account does not exist.";

            // Another transaction could create the account and then this transaction would succeed.
            return tecNO_DST;
        }
        else if (isSetBit (mParams, tapOPEN_LEDGER) && bPartialPayment)
        {
            // Make retry work smaller, by rejecting this.
            m_journal.info <<
                "Delay transaction: Partial payment not allowed to create account.";
            

            // Another transaction could create the account and then this
            // transaction would succeed.
            return telNO_DST_PARTIAL;
        }
        // Note: Reserve is not scaled by load.
        else if (saDstAmount.getNValue () < mEngine->getLedger ()->getReserve (0))
        {
            m_journal.info <<
                "Delay transaction: Destination account does not exist. " <<
                "Insufficent payment to create account.";

            // Another transaction could create the account and then this
            // transaction would succeed.
            return tecNO_DST_INSUF_XRP;
        }

        // Create the account.
        sleDst = mEngine->entryCreate (
            ltACCOUNT_ROOT, Ledger::getAccountRootIndex (uDstAccountID));

        sleDst->setFieldAccount (sfAccount, uDstAccountID);
        sleDst->setFieldU32 (sfSequence, 1);
    }
    else if ((sleDst->getFlags () & lsfRequireDestTag) && !mTxn.isFieldPresent (sfDestinationTag))
    {
        m_journal.info <<
            "Malformed transaction: DestinationTag required.";

        return tefDST_TAG_NEEDED;
    }
    else
    {
        mEngine->entryModify (sleDst);
    }

    TER terResult;
    // XXX Should bMax be sufficient to imply ripple?
    bool const bRipple = bPaths || bMax || !saDstAmount.isNative ();

    if (bRipple)
    {
        // Ripple payment

        STPathSet spsPaths = mTxn.getFieldPathSet (sfPaths);
        std::vector<PathState::pointer> vpsExpanded;
        STAmount saMaxAmountAct;
        STAmount saDstAmountAct;

        try
        {
            bool const openLedger = isSetBit (mParams, tapOPEN_LEDGER);
            bool const tooManyPaths = spsPaths.size () > MaxPathSize;

            terResult = openLedger && tooManyPaths
                        ? telBAD_PATH_COUNT // Too many paths for proposed ledger.
                        : RippleCalc::rippleCalc (
                              mEngine->getNodes (),
                              saMaxAmountAct,
                              saDstAmountAct,
                              vpsExpanded,
                              saMaxAmount,
                              saDstAmount,
                              uDstAccountID,
                              mTxnAccountID,
                              spsPaths,
                              bPartialPayment,
                              bLimitQuality,
                              bNoRippleDirect, // Always compute for finalizing ledger.
                              false, // Not standalone, delete unfundeds.
                              isSetBit (mParams, tapOPEN_LEDGER));

            if (isTerRetry(terResult))
                terResult = tecPATH_DRY;

            if ((tesSUCCESS == terResult) && (saDstAmountAct != saDstAmount))
                mEngine->getNodes().setDeliveredAmount (saDstAmountAct);
        }
        catch (std::exception const& e)
        {
            m_journal.info <<
                "Caught throw: " << e.what ();

            terResult   = tefEXCEPTION;
        }
    }
    else
    {
        // Direct XRP payment.

        std::uint32_t const uOwnerCount (mTxnAccount->getFieldU32 (sfOwnerCount));
        std::uint64_t const uReserve (mEngine->getLedger ()->getReserve (uOwnerCount));

        // Make sure have enough reserve to send. Allow final spend to use reserve for fee.
        if (mPriorBalance < saDstAmount + std::max(uReserve, mTxn.getTransactionFee ().getNValue ()))
        {
            // Vote no. However, transaction might succeed, if applied in a different order.
            m_journal.info << "Delay transaction: Insufficient funds: " <<
                " " << mPriorBalance.getText () <<
                " / " << (saDstAmount + uReserve).getText () <<
                " (" << uReserve << ")";

            terResult   = tecUNFUNDED_PAYMENT;
        }
        else
        {
            mTxnAccount->setFieldAmount (sfBalance, mSourceBalance - saDstAmount);
            sleDst->setFieldAmount (sfBalance, sleDst->getFieldAmount (sfBalance) + saDstAmount);

            // re-arm the password change fee if we can and need to
            if ((sleDst->getFlags () & lsfPasswordSpent))
                sleDst->clearFlag (lsfPasswordSpent);

            terResult = tesSUCCESS;
        }
    }

    std::string strToken;
    std::string strHuman;

    if (transResultInfo (terResult, strToken, strHuman))
    {
        m_journal.info << 
            strToken << ": " << strHuman;
    }
    else
    {
        assert (false);
    }

    return terResult;
}

}


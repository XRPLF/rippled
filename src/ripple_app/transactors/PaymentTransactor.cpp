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

SETUP_LOG (PaymentTransactor)

#define RIPPLE_PATHS_MAX    6

TER PaymentTransactor::doApply ()
{
    // Ripple if source or destination is non-native or if there are paths.
    const beast::uint32 uTxFlags        = mTxn.getFlags ();
    const bool      bPartialPayment = isSetBit (uTxFlags, tfPartialPayment);
    const bool      bLimitQuality   = isSetBit (uTxFlags, tfLimitQuality);
    const bool      bNoRippleDirect = isSetBit (uTxFlags, tfNoRippleDirect);
    const bool      bPaths          = mTxn.isFieldPresent (sfPaths);
    const bool      bMax            = mTxn.isFieldPresent (sfSendMax);
    const uint160   uDstAccountID   = mTxn.getFieldAccount160 (sfDestination);
    const STAmount  saDstAmount     = mTxn.getFieldAmount (sfAmount);
    const STAmount  saMaxAmount     = bMax
                                      ? mTxn.getFieldAmount (sfSendMax)
                                      : saDstAmount.isNative ()
                                      ? saDstAmount
                                      : STAmount (saDstAmount.getCurrency (), mTxnAccountID, saDstAmount.getMantissa (), saDstAmount.getExponent (), saDstAmount.isNegative ());
    const uint160   uSrcCurrency    = saMaxAmount.getCurrency ();
    const uint160   uDstCurrency    = saDstAmount.getCurrency ();
    const bool      bXRPDirect      = uSrcCurrency.isZero () && uDstCurrency.isZero ();

    WriteLog (lsINFO, PaymentTransactor) << boost::str (boost::format ("Payment> saMaxAmount=%s saDstAmount=%s")
                                         % saMaxAmount.getFullText ()
                                         % saDstAmount.getFullText ());

    if (!saDstAmount.isLegalNet () || !saMaxAmount.isLegalNet ())
        return temBAD_AMOUNT;

    if (uTxFlags & tfPaymentMask)
    {
        WriteLog (lsINFO, PaymentTransactor) << "Payment: Malformed transaction: Invalid flags set.";

        return temINVALID_FLAG;
    }
    else if (!uDstAccountID)
    {
        WriteLog (lsINFO, PaymentTransactor) << "Payment: Malformed transaction: Payment destination account not specified.";

        return temDST_NEEDED;
    }
    else if (bMax && !saMaxAmount.isPositive ())
    {
        WriteLog (lsINFO, PaymentTransactor) << "Payment: Malformed transaction: bad max amount: " << saMaxAmount.getFullText ();

        return temBAD_AMOUNT;
    }
    else if (!saDstAmount.isPositive ())
    {
        WriteLog (lsINFO, PaymentTransactor) << "Payment: Malformed transaction: bad dst amount: " << saDstAmount.getFullText ();

        return temBAD_AMOUNT;
    }
    else if (CURRENCY_BAD == uSrcCurrency || CURRENCY_BAD == uDstCurrency)
    {
        WriteLog (lsINFO, PaymentTransactor) << "Payment: Malformed transaction: Bad currency.";

        return temBAD_CURRENCY;
    }
    else if (mTxnAccountID == uDstAccountID && uSrcCurrency == uDstCurrency && !bPaths)
    {
        WriteLog (lsINFO, PaymentTransactor) << boost::str (boost::format ("Payment: Malformed transaction: Redundant transaction: src=%s, dst=%s, src_cur=%s, dst_cur=%s")
                                             % mTxnAccountID.ToString ()
                                             % uDstAccountID.ToString ()
                                             % uSrcCurrency.ToString ()
                                             % uDstCurrency.ToString ());

        return temREDUNDANT;
    }
    else if (bMax && saMaxAmount == saDstAmount && saMaxAmount.getCurrency () == saDstAmount.getCurrency ())
    {
        WriteLog (lsINFO, PaymentTransactor) << "Payment: Malformed transaction: Redundant SendMax.";

        return temREDUNDANT_SEND_MAX;
    }
    else if (bXRPDirect && bMax)
    {
        WriteLog (lsINFO, PaymentTransactor) << "Payment: Malformed transaction: SendMax specified for XRP to XRP.";

        return temBAD_SEND_XRP_MAX;
    }
    else if (bXRPDirect && bPaths)
    {
        WriteLog (lsINFO, PaymentTransactor) << "Payment: Malformed transaction: Paths specified for XRP to XRP.";

        return temBAD_SEND_XRP_PATHS;
    }
    else if (bXRPDirect && bPartialPayment)
    {
        WriteLog (lsINFO, PaymentTransactor) << "Payment: Malformed transaction: Partial payment specified for XRP to XRP.";

        return temBAD_SEND_XRP_PARTIAL;
    }
    else if (bXRPDirect && bLimitQuality)
    {
        WriteLog (lsINFO, PaymentTransactor) << "Payment: Malformed transaction: Limit quality specified for XRP to XRP.";

        return temBAD_SEND_XRP_LIMIT;
    }
    else if (bXRPDirect && bNoRippleDirect)
    {
        WriteLog (lsINFO, PaymentTransactor) << "Payment: Malformed transaction: No ripple direct specified for XRP to XRP.";

        return temBAD_SEND_XRP_NO_DIRECT;
    }

    SLE::pointer    sleDst  = mEngine->entryCache (ltACCOUNT_ROOT, Ledger::getAccountRootIndex (uDstAccountID));

    if (!sleDst)
    {
        // Destination account does not exist.

        if (!saDstAmount.isNative ())
        {
            WriteLog (lsINFO, PaymentTransactor) << "Payment: Delay transaction: Destination account does not exist.";

            // Another transaction could create the account and then this transaction would succeed.
            return tecNO_DST;
        }
        else if (isSetBit (mParams, tapOPEN_LEDGER) && bPartialPayment)
        {
            WriteLog (lsINFO, PaymentTransactor) << "Payment: Delay transaction: Partial payment not allowed to create account.";
            // Make retry work smaller, by rejecting this.

            // Another transaction could create the account and then this transaction would succeed.
            return telNO_DST_PARTIAL;
        }
        else if (saDstAmount.getNValue () < mEngine->getLedger ()->getReserve (0)) // Reserve is not scaled by load.
        {
            WriteLog (lsINFO, PaymentTransactor) << "Payment: Delay transaction: Destination account does not exist. Insufficent payment to create account.";

            // Another transaction could create the account and then this transaction would succeed.
            return tecNO_DST_INSUF_XRP;
        }

        // Create the account.
        sleDst  = mEngine->entryCreate (ltACCOUNT_ROOT, Ledger::getAccountRootIndex (uDstAccountID));

        sleDst->setFieldAccount (sfAccount, uDstAccountID);
        sleDst->setFieldU32 (sfSequence, 1);
    }
    else if ((sleDst->getFlags () & lsfRequireDestTag) && !mTxn.isFieldPresent (sfDestinationTag))
    {
        WriteLog (lsINFO, PaymentTransactor) << "Payment: Malformed transaction: DestinationTag required.";

        return tefDST_TAG_NEEDED;
    }
    else
    {
        mEngine->entryModify (sleDst);
    }

    TER         terResult;
    // XXX Should bMax be sufficient to imply ripple?
    const bool  bRipple = bPaths || bMax || !saDstAmount.isNative ();

    if (bRipple)
    {
        // Ripple payment

        STPathSet   spsPaths = mTxn.getFieldPathSet (sfPaths);
        std::vector<PathState::pointer> vpsExpanded;
        STAmount    saMaxAmountAct;
        STAmount    saDstAmountAct;

        try
        {
            terResult   = isSetBit (mParams, tapOPEN_LEDGER) && spsPaths.size () > RIPPLE_PATHS_MAX
                          ? telBAD_PATH_COUNT         // Too many paths for proposed ledger.
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
                              bNoRippleDirect,        // Always compute for finalizing ledger.
                              false,                  // Not standalone, delete unfundeds.
                              isSetBit (mParams, tapOPEN_LEDGER));

            if (isTerRetry(terResult))
                terResult = tecPATH_DRY;

            if ((tesSUCCESS == terResult) && (saDstAmountAct != saDstAmount))
                mEngine->getNodes().setDeliveredAmount (saDstAmountAct);
        }
        catch (const std::exception& e)
        {
            WriteLog (lsINFO, PaymentTransactor) << "Payment: Caught throw: " << e.what ();

            terResult   = tefEXCEPTION;
        }
    }
    else
    {
        // Direct XRP payment.

        const beast::uint32 uOwnerCount     = mTxnAccount->getFieldU32 (sfOwnerCount);
        const beast::uint64 uReserve        = mEngine->getLedger ()->getReserve (uOwnerCount);

        // Make sure have enough reserve to send. Allow final spend to use reserve for fee.
        if (mPriorBalance < saDstAmount + std::max(uReserve, mTxn.getTransactionFee ().getNValue ()))
        {
            // Vote no. However, transaction might succeed, if applied in a different order.
            WriteLog (lsINFO, PaymentTransactor) << "";
            WriteLog (lsINFO, PaymentTransactor) << boost::str (boost::format ("Payment: Delay transaction: Insufficient funds: %s / %s (%d)")
                                                 % mPriorBalance.getText () % (saDstAmount + uReserve).getText () % uReserve);

            terResult   = tecUNFUNDED_PAYMENT;
        }
        else
        {
            mTxnAccount->setFieldAmount (sfBalance, mSourceBalance - saDstAmount);
            sleDst->setFieldAmount (sfBalance, sleDst->getFieldAmount (sfBalance) + saDstAmount);

            // re-arm the password change fee if we can and need to
            if ((sleDst->getFlags () & lsfPasswordSpent))
                sleDst->clearFlag (lsfPasswordSpent);

            terResult   = tesSUCCESS;
        }
    }

    std::string strToken;
    std::string strHuman;

    if (transResultInfo (terResult, strToken, strHuman))
    {
        WriteLog (lsINFO, PaymentTransactor) << boost::str (boost::format ("Payment: %s: %s") % strToken % strHuman);
    }
    else
    {
        assert (false);
    }

    return terResult;
}

}


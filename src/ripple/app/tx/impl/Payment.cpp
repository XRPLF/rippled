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
#include <ripple/app/tx/impl/Payment.h>
#include <ripple/app/paths/RippleCalc.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/JsonFields.h>

namespace ripple {

// See https://ripple.com/wiki/Transaction_Format#Payment_.280.29

// Mon Aug 17 11:00:00am PDT
static std::uint32_t const deliverMinTime = 493149600;

bool
allowDeliverMin (ApplyView const& view)
{
    return view.info().parentCloseTime > deliverMinTime ||
        (view.flags() & tapENABLE_TESTING);
}

TER
Payment::preflight (PreflightContext const& ctx)
{
    auto const ret = preflight1 (ctx);
    if (!isTesSuccess (ret))
        return ret;

    auto& tx = ctx.tx;
    auto& j = ctx.j;

    std::uint32_t const uTxFlags = tx.getFlags ();

    if (uTxFlags & tfPaymentMask)
    {
        JLOG(j.trace) << "Malformed transaction: " <<
            "Invalid flags set.";
        return temINVALID_FLAG;
    }

    bool const partialPaymentAllowed = uTxFlags & tfPartialPayment;
    bool const limitQuality = uTxFlags & tfLimitQuality;
    bool const defaultPathsAllowed = !(uTxFlags & tfNoRippleDirect);
    bool const bPaths = tx.isFieldPresent (sfPaths);
    bool const bMax = tx.isFieldPresent (sfSendMax);

    STAmount const saDstAmount (tx.getFieldAmount (sfAmount));

    STAmount maxSourceAmount;
    auto const account = tx.getAccountID(sfAccount);

    if (bMax)
        maxSourceAmount = tx.getFieldAmount (sfSendMax);
    else if (saDstAmount.native ())
        maxSourceAmount = saDstAmount;
    else
        maxSourceAmount = STAmount (
            { saDstAmount.getCurrency (), account },
            saDstAmount.mantissa(), saDstAmount.exponent (),
            saDstAmount < zero);

    auto const& uSrcCurrency = maxSourceAmount.getCurrency ();
    auto const& uDstCurrency = saDstAmount.getCurrency ();

    // isZero() is XRP.  FIX!
    bool const bXRPDirect = uSrcCurrency.isZero () && uDstCurrency.isZero ();

    if (!isLegalNet (saDstAmount) || !isLegalNet (maxSourceAmount))
        return temBAD_AMOUNT;

    auto const uDstAccountID = tx.getAccountID (sfDestination);

    if (!uDstAccountID)
    {
        JLOG(j.trace) << "Malformed transaction: " <<
            "Payment destination account not specified.";
        return temDST_NEEDED;
    }
    if (bMax && maxSourceAmount <= zero)
    {
        JLOG(j.trace) << "Malformed transaction: " <<
            "bad max amount: " << maxSourceAmount.getFullText ();
        return temBAD_AMOUNT;
    }
    if (saDstAmount <= zero)
    {
        JLOG(j.trace) << "Malformed transaction: "<<
            "bad dst amount: " << saDstAmount.getFullText ();
        return temBAD_AMOUNT;
    }
    if (badCurrency() == uSrcCurrency || badCurrency() == uDstCurrency)
    {
        JLOG(j.trace) <<"Malformed transaction: " <<
            "Bad currency.";
        return temBAD_CURRENCY;
    }
    if (account == uDstAccountID && uSrcCurrency == uDstCurrency && !bPaths)
    {
        // You're signing yourself a payment.
        // If bPaths is true, you might be trying some arbitrage.
        JLOG(j.trace) << "Malformed transaction: " <<
            "Redundant payment from " << to_string (account) <<
            " to self without path for " << to_string (uDstCurrency);
        return temREDUNDANT;
    }
    if (bXRPDirect && bMax)
    {
        // Consistent but redundant transaction.
        JLOG(j.trace) << "Malformed transaction: " <<
            "SendMax specified for XRP to XRP.";
        return temBAD_SEND_XRP_MAX;
    }
    if (bXRPDirect && bPaths)
    {
        // XRP is sent without paths.
        JLOG(j.trace) << "Malformed transaction: " <<
            "Paths specified for XRP to XRP.";
        return temBAD_SEND_XRP_PATHS;
    }
    if (bXRPDirect && partialPaymentAllowed)
    {
        // Consistent but redundant transaction.
        JLOG(j.trace) << "Malformed transaction: " <<
            "Partial payment specified for XRP to XRP.";
        return temBAD_SEND_XRP_PARTIAL;
    }
    if (bXRPDirect && limitQuality)
    {
        // Consistent but redundant transaction.
        JLOG(j.trace) << "Malformed transaction: " <<
            "Limit quality specified for XRP to XRP.";
        return temBAD_SEND_XRP_LIMIT;
    }
    if (bXRPDirect && !defaultPathsAllowed)
    {
        // Consistent but redundant transaction.
        JLOG(j.trace) << "Malformed transaction: " <<
            "No ripple direct specified for XRP to XRP.";
        return temBAD_SEND_XRP_NO_DIRECT;
    }

    if (tx.isFieldPresent(sfDeliverMin))
    {
        if (! partialPaymentAllowed)
        {
            JLOG(j.trace) << "Malformed transaction: Partial payment not "
                "specified for " << jss::DeliverMin.c_str() << ".";
            return temBAD_AMOUNT;
        }

        auto const dMin = tx.getFieldAmount(sfDeliverMin);
        if (!isLegalNet(dMin) || dMin <= zero)
        {
            JLOG(j.trace) << "Malformed transaction: Invalid " <<
                jss::DeliverMin.c_str() << " amount. " <<
                    dMin.getFullText();
            return temBAD_AMOUNT;
        }
        if (dMin.issue() != saDstAmount.issue())
        {
            JLOG(j.trace) <<  "Malformed transaction: Dst issue differs "
                "from " << jss::DeliverMin.c_str() << ". " <<
                    dMin.getFullText();
            return temBAD_AMOUNT;
        }
        if (dMin > saDstAmount)
        {
            JLOG(j.trace) << "Malformed transaction: Dst amount less than " <<
                jss::DeliverMin.c_str() << ". " <<
                    dMin.getFullText();
            return temBAD_AMOUNT;
        }
    }

    return preflight2 (ctx);
}

TER
Payment::doApply ()
{
    bool const deliverMin = tx().isFieldPresent(sfDeliverMin);

    if (deliverMin)
    {
        if (! allowDeliverMin(view()))
            return temMALFORMED;
    }

    // Ripple if source or destination is non-native or if there are paths.
    std::uint32_t const uTxFlags = tx().getFlags ();
    bool const partialPaymentAllowed = uTxFlags & tfPartialPayment;
    bool const limitQuality = uTxFlags & tfLimitQuality;
    bool const defaultPathsAllowed = !(uTxFlags & tfNoRippleDirect);
    bool const bPaths = tx().isFieldPresent (sfPaths);
    bool const bMax = tx().isFieldPresent (sfSendMax);

    AccountID const uDstAccountID (tx().getAccountID (sfDestination));
    STAmount const saDstAmount (tx().getFieldAmount (sfAmount));
    STAmount maxSourceAmount;
    if (bMax)
        maxSourceAmount = tx().getFieldAmount (sfSendMax);
    else if (saDstAmount.native ())
        maxSourceAmount = saDstAmount;
    else
        maxSourceAmount = STAmount (
            {saDstAmount.getCurrency (), account_},
            saDstAmount.mantissa(), saDstAmount.exponent (),
            saDstAmount < zero);

    j_.trace <<
        "maxSourceAmount=" << maxSourceAmount.getFullText () <<
        " saDstAmount=" << saDstAmount.getFullText ();

    // Open a ledger for editing.
    auto const k = keylet::account(uDstAccountID);
    SLE::pointer sleDst = view().peek (k);

    if (!sleDst)
    {
        // Destination account does not exist.
        if (!saDstAmount.native ())
        {
            j_.trace <<
                "Delay transaction: Destination account does not exist.";

            // Another transaction could create the account and then this
            // transaction would succeed.
            return tecNO_DST;
        }
        else if (view().open()
            && partialPaymentAllowed)
        {
            // You cannot fund an account with a partial payment.
            // Make retry work smaller, by rejecting this.
            j_.trace <<
                "Delay transaction: Partial payment not allowed to create account.";


            // Another transaction could create the account and then this
            // transaction would succeed.
            return telNO_DST_PARTIAL;
        }
        else if (saDstAmount < STAmount (view().fees().accountReserve(0)))
        {
            // accountReserve is the minimum amount that an account can have.
            // Reserve is not scaled by load.
            j_.trace <<
                "Delay transaction: Destination account does not exist. " <<
                "Insufficent payment to create account.";

            // TODO: dedupe
            // Another transaction could create the account and then this
            // transaction would succeed.
            return tecNO_DST_INSUF_XRP;
        }

        // Create the account.
        sleDst = std::make_shared<SLE>(k);
        sleDst->setAccountID (sfAccount, uDstAccountID);
        sleDst->setFieldU32 (sfSequence, 1);
        view().insert(sleDst);
    }
    else if ((sleDst->getFlags () & lsfRequireDestTag) &&
                !tx().isFieldPresent (sfDestinationTag))
    {
        // The tag is basically account-specific information we don't
        // understand, but we can require someone to fill it in.

        // We didn't make this test for a newly-formed account because there's
        // no way for this field to be set.
        j_.trace << "Malformed transaction: DestinationTag required.";

        return tecDST_TAG_NEEDED;
    }
    else
    {
        // Tell the engine that we are intending to change the the destination
        // account.  The source account gets always charged a fee so it's always
        // marked as modified.
        view().update (sleDst);
    }

    TER terResult;

    bool const bRipple = bPaths || bMax || !saDstAmount.native ();
    // XXX Should bMax be sufficient to imply ripple?

    if (bRipple)
    {
        // Ripple payment with at least one intermediate step and uses
        // transitive balances.

        // Copy paths into an editable class.
        STPathSet spsPaths = tx().getFieldPathSet (sfPaths);

        try
        {
            path::RippleCalc::Input rcInput;
            rcInput.partialPaymentAllowed = partialPaymentAllowed;
            rcInput.defaultPathsAllowed = defaultPathsAllowed;
            rcInput.limitQuality = limitQuality;
            rcInput.deleteUnfundedOffers = true;
            rcInput.isLedgerOpen = view().open();

            bool pathTooBig = spsPaths.size () > MaxPathSize;

            for (auto const& path : spsPaths)
                if (path.size () > MaxPathLength)
                    pathTooBig = true;

            if (rcInput.isLedgerOpen && pathTooBig)
            {
                terResult = telBAD_PATH_COUNT; // Too many paths for proposed ledger.
            }
            else
            {
                path::RippleCalc::Output rc;
                {
                    PaymentSandbox pv(&view());
                    rc = path::RippleCalc::rippleCalculate (
                        pv,
                        maxSourceAmount,
                        saDstAmount,
                        uDstAccountID,
                        account_,
                        spsPaths,
                        ctx_.app.logs(),
                        &rcInput);
                    // VFALCO NOTE We might not need to apply, depending
                    //             on the TER. But always applying *should*
                    //             be safe.
                    pv.apply(ctx_.rawView());
                }

                // TODO: is this right?  If the amount is the correct amount, was
                // the delivered amount previously set?
                if (rc.result () == tesSUCCESS &&
                    rc.actualAmountOut != saDstAmount)
                {
                    if (deliverMin && rc.actualAmountOut <
                        tx().getFieldAmount (sfDeliverMin))
                        rc.setResult (tecPATH_PARTIAL);
                    else
                        ctx_.deliver (rc.actualAmountOut);
                }

                terResult = rc.result ();
            }

            // TODO(tom): what's going on here?
            if (isTerRetry (terResult))
                terResult = tecPATH_DRY;

        }
        catch (std::exception const& e)
        {
            j_.trace <<
                "Caught throw: " << e.what ();

            terResult = tefEXCEPTION;
        }
    }
    else
    {
        assert (saDstAmount.native ());

        // Direct XRP payment.

        // uOwnerCount is the number of entries in this legder for this
        // account that require a reserve.
        auto const uOwnerCount = view().read(
            keylet::account(account_))->getFieldU32 (sfOwnerCount);

        // This is the total reserve in drops.
        auto const reserve = view().fees().accountReserve(uOwnerCount);

        // mPriorBalance is the balance on the sending account BEFORE the
        // fees were charged. We want to make sure we have enough reserve
        // to send. Allow final spend to use reserve for fee.
        auto const mmm = std::max(reserve,
            tx().getFieldAmount (sfFee).xrp ());

        if (mPriorBalance < saDstAmount.xrp () + mmm)
        {
            // Vote no. However the transaction might succeed, if applied in
            // a different order.
            j_.trace << "Delay transaction: Insufficient funds: " <<
                " " << to_string (mPriorBalance) <<
                " / " << to_string (saDstAmount.xrp () + mmm) <<
                " (" << to_string (reserve) << ")";

            terResult = tecUNFUNDED_PAYMENT;
        }
        else
        {
            // The source account does have enough money, so do the
            // arithmetic for the transfer and make the ledger change.
            view().peek(keylet::account(account_))->setFieldAmount (sfBalance,
                mSourceBalance - saDstAmount);
            sleDst->setFieldAmount (sfBalance,
                sleDst->getFieldAmount (sfBalance) + saDstAmount);

            // Re-arm the password change fee if we can and need to.
            if ((sleDst->getFlags () & lsfPasswordSpent))
                sleDst->clearFlag (lsfPasswordSpent);

            terResult = tesSUCCESS;
        }
    }

    std::string strToken;
    std::string strHuman;

    if (transResultInfo (terResult, strToken, strHuman))
    {
        j_.trace <<
            strToken << ": " << strHuman;
    }
    else
    {
        assert (false);
    }

    return terResult;
}

}  // ripple

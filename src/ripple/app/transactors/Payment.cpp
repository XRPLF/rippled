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
#include <ripple/app/paths/RippleCalc.h>
#include <ripple/app/transactors/Transactor.h>
#include <ripple/basics/Log.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple {

// See https://ripple.com/wiki/Transaction_Format#Payment_.280.29

class Payment
    : public Transactor
{
    /* The largest number of paths we allow */
    static std::size_t const MaxPathSize = 6;

    /* The longest path we allow */
    static std::size_t const MaxPathLength = 8;

public:
    Payment (
        STTx const& txn,
        TransactionEngineParams params,
        TransactionEngine* engine)
        : Transactor (
            txn,
            params,
            engine,
            deprecatedLogs().journal("Payment"))
    {

    }

    TER preCheck () override
    {
        std::uint32_t const uTxFlags = mTxn.getFlags ();

        if (uTxFlags & tfPaymentMask)
        {
            m_journal.trace << "Malformed transaction: " <<
                "Invalid flags set.";
            return temINVALID_FLAG;
        }

        bool const partialPaymentAllowed = uTxFlags & tfPartialPayment;
        bool const limitQuality = uTxFlags & tfLimitQuality;
        bool const defaultPathsAllowed = !(uTxFlags & tfNoRippleDirect);
        bool const bPaths = mTxn.isFieldPresent (sfPaths);
        bool const bMax = mTxn.isFieldPresent (sfSendMax);

        STAmount const saDstAmount (mTxn.getFieldAmount (sfAmount));

        STAmount maxSourceAmount;

        if (bMax)
            maxSourceAmount = mTxn.getFieldAmount (sfSendMax);
        else if (saDstAmount.isNative ())
            maxSourceAmount = saDstAmount;
        else
            maxSourceAmount = STAmount (
                { saDstAmount.getCurrency (), mTxnAccountID },
                saDstAmount.mantissa(), saDstAmount.exponent (),
                saDstAmount < zero);

        auto const& uSrcCurrency = maxSourceAmount.getCurrency ();
        auto const& uDstCurrency = saDstAmount.getCurrency ();

        // isZero() is XRP.  FIX!
        bool const bXRPDirect = uSrcCurrency.isZero () && uDstCurrency.isZero ();

        if (!isLegalNet (saDstAmount) || !isLegalNet (maxSourceAmount))
            return temBAD_AMOUNT;

        Account const uDstAccountID (mTxn.getFieldAccount160 (sfDestination));

        if (!uDstAccountID)
        {
            m_journal.trace << "Malformed transaction: " <<
                "Payment destination account not specified.";
            return temDST_NEEDED;
        }
        if (bMax && maxSourceAmount <= zero)
        {
            m_journal.trace << "Malformed transaction: " <<
                "bad max amount: " << maxSourceAmount.getFullText ();
            return temBAD_AMOUNT;
        }
        if (saDstAmount <= zero)
        {
            m_journal.trace << "Malformed transaction: "<<
                "bad dst amount: " << saDstAmount.getFullText ();
            return temBAD_AMOUNT;
        }
        if (badCurrency() == uSrcCurrency || badCurrency() == uDstCurrency)
        {
            m_journal.trace <<"Malformed transaction: " <<
                "Bad currency.";
            return temBAD_CURRENCY;
        }
        if (mTxnAccountID == uDstAccountID && uSrcCurrency == uDstCurrency && !bPaths)
        {
            // You're signing yourself a payment.
            // If bPaths is true, you might be trying some arbitrage.
            m_journal.trace << "Malformed transaction: " <<
                "Redundant payment from " << to_string (mTxnAccountID) <<
                " to self without path for " << to_string (uDstCurrency);
            return temREDUNDANT;
        }
        if (bXRPDirect && bMax)
        {
            // Consistent but redundant transaction.
            m_journal.trace << "Malformed transaction: " <<
                "SendMax specified for XRP to XRP.";
            return temBAD_SEND_XRP_MAX;
        }
        if (bXRPDirect && bPaths)
        {
            // XRP is sent without paths.
            m_journal.trace << "Malformed transaction: " <<
                "Paths specified for XRP to XRP.";
            return temBAD_SEND_XRP_PATHS;
        }
        if (bXRPDirect && partialPaymentAllowed)
        {
            // Consistent but redundant transaction.
            m_journal.trace << "Malformed transaction: " <<
                "Partial payment specified for XRP to XRP.";
            return temBAD_SEND_XRP_PARTIAL;
        }
        if (bXRPDirect && limitQuality)
        {
            // Consistent but redundant transaction.
            m_journal.trace << "Malformed transaction: " <<
                "Limit quality specified for XRP to XRP.";
            return temBAD_SEND_XRP_LIMIT;
        }
        if (bXRPDirect && !defaultPathsAllowed)
        {
            // Consistent but redundant transaction.
            m_journal.trace << "Malformed transaction: " <<
                "No ripple direct specified for XRP to XRP.";
            return temBAD_SEND_XRP_NO_DIRECT;
        }

        return Transactor::preCheck ();
    }

    TER doApply () override
    {
        // Ripple if source or destination is non-native or if there are paths.
        std::uint32_t const uTxFlags = mTxn.getFlags ();
        bool const partialPaymentAllowed = uTxFlags & tfPartialPayment;
        bool const limitQuality = uTxFlags & tfLimitQuality;
        bool const defaultPathsAllowed = !(uTxFlags & tfNoRippleDirect);
        bool const bPaths = mTxn.isFieldPresent (sfPaths);
        bool const bMax = mTxn.isFieldPresent (sfSendMax);
        Account const uDstAccountID (mTxn.getFieldAccount160 (sfDestination));
        STAmount const saDstAmount (mTxn.getFieldAmount (sfAmount));
        STAmount maxSourceAmount;
        if (bMax)
            maxSourceAmount = mTxn.getFieldAmount (sfSendMax);
        else if (saDstAmount.isNative ())
            maxSourceAmount = saDstAmount;
        else
          maxSourceAmount = STAmount (
              {saDstAmount.getCurrency (), mTxnAccountID},
              saDstAmount.mantissa(), saDstAmount.exponent (),
              saDstAmount < zero);

        m_journal.trace <<
            "maxSourceAmount=" << maxSourceAmount.getFullText () <<
            " saDstAmount=" << saDstAmount.getFullText ();

        // Open a ledger for editing.
        auto const index = getAccountRootIndex (uDstAccountID);
        SLE::pointer sleDst (mEngine->view().entryCache (ltACCOUNT_ROOT, index));

        if (!sleDst)
        {
            // Destination account does not exist.
            if (!saDstAmount.isNative ())
            {
                m_journal.trace <<
                    "Delay transaction: Destination account does not exist.";

                // Another transaction could create the account and then this
                // transaction would succeed.
                return tecNO_DST;
            }
            else if (mParams & tapOPEN_LEDGER && partialPaymentAllowed)
            {
                // You cannot fund an account with a partial payment.
                // Make retry work smaller, by rejecting this.
                m_journal.trace <<
                    "Delay transaction: Partial payment not allowed to create account.";


                // Another transaction could create the account and then this
                // transaction would succeed.
                return telNO_DST_PARTIAL;
            }
            else if (saDstAmount < mEngine->getLedger ()->getReserve (0))
            {
                // getReserve() is the minimum amount that an account can have.
                // Reserve is not scaled by load.
                m_journal.trace <<
                    "Delay transaction: Destination account does not exist. " <<
                    "Insufficent payment to create account.";

                // TODO: dedupe
                // Another transaction could create the account and then this
                // transaction would succeed.
                return tecNO_DST_INSUF_XRP;
            }

            // Create the account.
            auto const newIndex = getAccountRootIndex (uDstAccountID);
            sleDst = mEngine->view().entryCreate (ltACCOUNT_ROOT, newIndex);
            sleDst->setFieldAccount (sfAccount, uDstAccountID);
            sleDst->setFieldU32 (sfSequence, 1);
        }
        else if ((sleDst->getFlags () & lsfRequireDestTag) &&
                 !mTxn.isFieldPresent (sfDestinationTag))
        {
            // The tag is basically account-specific information we don't
            // understand, but we can require someone to fill it in.

            // We didn't make this test for a newly-formed account because there's
            // no way for this field to be set.
            m_journal.trace << "Malformed transaction: DestinationTag required.";

            return tecDST_TAG_NEEDED;
        }
        else
        {
            // Tell the engine that we are intending to change the the destination
            // account.  The source account gets always charged a fee so it's always
            // marked as modified.
            mEngine->view().entryModify (sleDst);
        }

        TER terResult;

        bool const bRipple = bPaths || bMax || !saDstAmount.isNative ();
        // XXX Should bMax be sufficient to imply ripple?

        if (bRipple)
        {
            // Ripple payment with at least one intermediate step and uses
            // transitive balances.

            // Copy paths into an editable class.
            STPathSet spsPaths = mTxn.getFieldPathSet (sfPaths);

            try
            {
                path::RippleCalc::Input rcInput;
                rcInput.partialPaymentAllowed = partialPaymentAllowed;
                rcInput.defaultPathsAllowed = defaultPathsAllowed;
                rcInput.limitQuality = limitQuality;
                rcInput.deleteUnfundedOffers = true;
                rcInput.isLedgerOpen = static_cast<bool>(mParams & tapOPEN_LEDGER);

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
                        ScopedDeferCredits g (mEngine->view ());
                        rc = path::RippleCalc::rippleCalculate (
                            mEngine->view (),
                            maxSourceAmount,
                            saDstAmount,
                            uDstAccountID,
                            mTxnAccountID,
                            spsPaths,
                            &rcInput);
                    }

                    // TODO: is this right?  If the amount is the correct amount, was
                    // the delivered amount previously set?
                    if (rc.result () == tesSUCCESS && rc.actualAmountOut != saDstAmount)
                        mEngine->view ().setDeliveredAmount (rc.actualAmountOut);

                    terResult = rc.result ();
                }

                // TODO(tom): what's going on here?
                if (isTerRetry (terResult))
                    terResult = tecPATH_DRY;

            }
            catch (std::exception const& e)
            {
                m_journal.trace <<
                    "Caught throw: " << e.what ();

                terResult = tefEXCEPTION;
            }
        }
        else
        {
            // Direct XRP payment.

            // uOwnerCount is the number of entries in this legder for this account
            // that require a reserve.

            std::uint32_t const uOwnerCount (mTxnAccount->getFieldU32 (sfOwnerCount));

            // This is the total reserve in drops.
            // TODO(tom): there should be a class for this.
            std::uint64_t const uReserve (mEngine->getLedger ()->getReserve (uOwnerCount));

            // mPriorBalance is the balance on the sending account BEFORE the fees were charged.
            //
            // Make sure have enough reserve to send. Allow final spend to use
            // reserve for fee.
            auto const mmm = std::max(uReserve, getNValue (mTxn.getTransactionFee ()));
            if (mPriorBalance < saDstAmount + mmm)
            {
                // Vote no.
                // However, transaction might succeed, if applied in a different order.
                m_journal.trace << "Delay transaction: Insufficient funds: " <<
                    " " << mPriorBalance.getText () <<
                    " / " << (saDstAmount + uReserve).getText () <<
                    " (" << uReserve << ")";

                terResult = tecUNFUNDED_PAYMENT;
            }
            else
            {
                // The source account does have enough money, so do the arithmetic
                // for the transfer and make the ledger change.
                mTxnAccount->setFieldAmount (sfBalance, mSourceBalance - saDstAmount);
                sleDst->setFieldAmount (sfBalance, sleDst->getFieldAmount (sfBalance) + saDstAmount);

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
            m_journal.trace <<
                strToken << ": " << strHuman;
        }
        else
        {
            assert (false);
        }

        return terResult;
    }
};

TER
transact_Payment (
    STTx const& txn,
    TransactionEngineParams params,
    TransactionEngine* engine)
{
    return Payment(txn, params, engine).apply ();
}

}  // ripple

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

#include <xrpld/app/misc/CredentialHelpers.h>
#include <xrpld/app/paths/RippleCalc.h>
#include <xrpld/app/tx/detail/Payment.h>
#include <xrpld/ledger/View.h>
#include <xrpl/basics/Log.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/st.h>

namespace ripple {

TxConsequences
Payment::makeTxConsequences(PreflightContext const& ctx)
{
    auto calculateMaxXRPSpend = [](STTx const& tx) -> XRPAmount {
        STAmount const maxAmount =
            tx.isFieldPresent(sfSendMax) ? tx[sfSendMax] : tx[sfAmount];

        // If there's no sfSendMax in XRP, and the sfAmount isn't
        // in XRP, then the transaction does not spend XRP.
        return maxAmount.native() ? maxAmount.xrp() : beast::zero;
    };

    return TxConsequences{ctx.tx, calculateMaxXRPSpend(ctx.tx)};
}

STAmount
getMaxSourceAmount(
    AccountID const& account,
    STAmount const& dstAmount,
    std::optional<STAmount> const& sendMax)
{
    if (sendMax)
        return *sendMax;
    else if (dstAmount.native() || dstAmount.holds<MPTIssue>())
        return dstAmount;
    else
        return STAmount(
            Issue{dstAmount.get<Issue>().currency, account},
            dstAmount.mantissa(),
            dstAmount.exponent(),
            dstAmount < beast::zero);
}

NotTEC
Payment::preflight(PreflightContext const& ctx)
{
    if (ctx.tx.isFieldPresent(sfCredentialIDs) &&
        !ctx.rules.enabled(featureCredentials))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    auto& tx = ctx.tx;
    auto& j = ctx.j;

    STAmount const dstAmount(tx.getFieldAmount(sfAmount));
    bool const mptDirect = dstAmount.holds<MPTIssue>();

    if (mptDirect && !ctx.rules.enabled(featureMPTokensV1))
        return temDISABLED;

    std::uint32_t const txFlags = tx.getFlags();

    std::uint32_t paymentMask = mptDirect ? tfMPTPaymentMask : tfPaymentMask;

    if (txFlags & paymentMask)
    {
        JLOG(j.trace()) << "Malformed transaction: Invalid flags set.";
        return temINVALID_FLAG;
    }

    if (mptDirect && ctx.tx.isFieldPresent(sfPaths))
        return temMALFORMED;

    bool const partialPaymentAllowed = txFlags & tfPartialPayment;
    bool const limitQuality = txFlags & tfLimitQuality;
    bool const defaultPathsAllowed = !(txFlags & tfNoRippleDirect);
    bool const hasPaths = tx.isFieldPresent(sfPaths);
    bool const hasMax = tx.isFieldPresent(sfSendMax);

    auto const deliverMin = tx[~sfDeliverMin];

    auto const account = tx.getAccountID(sfAccount);
    STAmount const maxSourceAmount =
        getMaxSourceAmount(account, dstAmount, tx[~sfSendMax]);

    if ((mptDirect && dstAmount.asset() != maxSourceAmount.asset()) ||
        (!mptDirect && maxSourceAmount.holds<MPTIssue>()))
    {
        JLOG(j.trace()) << "Malformed transaction: inconsistent issues: "
                        << dstAmount.getFullText() << " "
                        << maxSourceAmount.getFullText() << " "
                        << deliverMin.value_or(STAmount{}).getFullText();
        return temMALFORMED;
    }

    auto const& srcAsset = maxSourceAmount.asset();
    auto const& dstAsset = dstAmount.asset();

    bool const xrpDirect = srcAsset.native() && dstAsset.native();

    if (!isLegalNet(dstAmount) || !isLegalNet(maxSourceAmount))
        return temBAD_AMOUNT;

    auto const dstAccountID = tx.getAccountID(sfDestination);

    if (!dstAccountID)
    {
        JLOG(j.trace()) << "Malformed transaction: "
                        << "Payment destination account not specified.";
        return temDST_NEEDED;
    }
    if (hasMax && maxSourceAmount <= beast::zero)
    {
        JLOG(j.trace()) << "Malformed transaction: bad max amount: "
                        << maxSourceAmount.getFullText();
        return temBAD_AMOUNT;
    }
    if (dstAmount <= beast::zero)
    {
        JLOG(j.trace()) << "Malformed transaction: bad dst amount: "
                        << dstAmount.getFullText();
        return temBAD_AMOUNT;
    }
    if (badCurrency() == srcAsset || badCurrency() == dstAsset)
    {
        JLOG(j.trace()) << "Malformed transaction: Bad currency.";
        return temBAD_CURRENCY;
    }
    if (account == dstAccountID && equalTokens(srcAsset, dstAsset) && !hasPaths)
    {
        // You're signing yourself a payment.
        // If hasPaths is true, you might be trying some arbitrage.
        JLOG(j.trace()) << "Malformed transaction: "
                        << "Redundant payment from " << to_string(account)
                        << " to self without path for " << to_string(dstAsset);
        return temREDUNDANT;
    }
    if (xrpDirect && hasMax)
    {
        // Consistent but redundant transaction.
        JLOG(j.trace()) << "Malformed transaction: "
                        << "SendMax specified for XRP to XRP.";
        return temBAD_SEND_XRP_MAX;
    }
    if ((xrpDirect || mptDirect) && hasPaths)
    {
        // XRP is sent without paths.
        JLOG(j.trace()) << "Malformed transaction: "
                        << "Paths specified for XRP to XRP or MPT to MPT.";
        return temBAD_SEND_XRP_PATHS;
    }
    if (xrpDirect && partialPaymentAllowed)
    {
        // Consistent but redundant transaction.
        JLOG(j.trace()) << "Malformed transaction: "
                        << "Partial payment specified for XRP to XRP.";
        return temBAD_SEND_XRP_PARTIAL;
    }
    if ((xrpDirect || mptDirect) && limitQuality)
    {
        // Consistent but redundant transaction.
        JLOG(j.trace())
            << "Malformed transaction: "
            << "Limit quality specified for XRP to XRP or MPT to MPT.";
        return temBAD_SEND_XRP_LIMIT;
    }
    if ((xrpDirect || mptDirect) && !defaultPathsAllowed)
    {
        // Consistent but redundant transaction.
        JLOG(j.trace())
            << "Malformed transaction: "
            << "No ripple direct specified for XRP to XRP or MPT to MPT.";
        return temBAD_SEND_XRP_NO_DIRECT;
    }

    if (deliverMin)
    {
        if (!partialPaymentAllowed)
        {
            JLOG(j.trace()) << "Malformed transaction: Partial payment not "
                               "specified for "
                            << jss::DeliverMin.c_str() << ".";
            return temBAD_AMOUNT;
        }

        auto const dMin = *deliverMin;
        if (!isLegalNet(dMin) || dMin <= beast::zero)
        {
            JLOG(j.trace())
                << "Malformed transaction: Invalid " << jss::DeliverMin.c_str()
                << " amount. " << dMin.getFullText();
            return temBAD_AMOUNT;
        }
        if (dMin.asset() != dstAmount.asset())
        {
            JLOG(j.trace())
                << "Malformed transaction: Dst issue differs "
                   "from "
                << jss::DeliverMin.c_str() << ". " << dMin.getFullText();
            return temBAD_AMOUNT;
        }
        if (dMin > dstAmount)
        {
            JLOG(j.trace())
                << "Malformed transaction: Dst amount less than "
                << jss::DeliverMin.c_str() << ". " << dMin.getFullText();
            return temBAD_AMOUNT;
        }
    }

    if (auto const err = credentials::checkFields(ctx); !isTesSuccess(err))
        return err;

    return preflight2(ctx);
}

TER
Payment::preclaim(PreclaimContext const& ctx)
{
    // Ripple if source or destination is non-native or if there are paths.
    std::uint32_t const txFlags = ctx.tx.getFlags();
    bool const partialPaymentAllowed = txFlags & tfPartialPayment;
    auto const hasPaths = ctx.tx.isFieldPresent(sfPaths);
    auto const sendMax = ctx.tx[~sfSendMax];

    AccountID const dstAccountID(ctx.tx[sfDestination]);
    STAmount const dstAmount(ctx.tx[sfAmount]);

    auto const k = keylet::account(dstAccountID);
    auto const sleDst = ctx.view.read(k);

    if (!sleDst)
    {
        // Destination account does not exist.
        if (!dstAmount.native())
        {
            JLOG(ctx.j.trace())
                << "Delay transaction: Destination account does not exist.";

            // Another transaction could create the account and then this
            // transaction would succeed.
            return tecNO_DST;
        }
        else if (ctx.view.open() && partialPaymentAllowed)
        {
            // You cannot fund an account with a partial payment.
            // Make retry work smaller, by rejecting this.
            JLOG(ctx.j.trace()) << "Delay transaction: Partial payment not "
                                   "allowed to create account.";

            // Another transaction could create the account and then this
            // transaction would succeed.
            return telNO_DST_PARTIAL;
        }
        else if (dstAmount < STAmount(ctx.view.fees().accountReserve(0)))
        {
            // accountReserve is the minimum amount that an account can have.
            // Reserve is not scaled by load.
            JLOG(ctx.j.trace())
                << "Delay transaction: Destination account does not exist. "
                << "Insufficent payment to create account.";

            // TODO: dedupe
            // Another transaction could create the account and then this
            // transaction would succeed.
            return tecNO_DST_INSUF_XRP;
        }
    }
    else if (
        (sleDst->getFlags() & lsfRequireDestTag) &&
        !ctx.tx.isFieldPresent(sfDestinationTag))
    {
        // The tag is basically account-specific information we don't
        // understand, but we can require someone to fill it in.

        // We didn't make this test for a newly-formed account because there's
        // no way for this field to be set.
        JLOG(ctx.j.trace())
            << "Malformed transaction: DestinationTag required.";

        return tecDST_TAG_NEEDED;
    }

    // Payment with at least one intermediate step and uses transitive balances.
    if ((hasPaths || sendMax || !dstAmount.native()) && ctx.view.open())
    {
        STPathSet const& paths = ctx.tx.getFieldPathSet(sfPaths);

        if (paths.size() > MaxPathSize ||
            std::any_of(paths.begin(), paths.end(), [](STPath const& path) {
                return path.size() > MaxPathLength;
            }))
        {
            return telBAD_PATH_COUNT;
        }
    }

    if (auto const err = credentials::valid(ctx, ctx.tx[sfAccount]);
        !isTesSuccess(err))
        return err;

    return tesSUCCESS;
}

TER
Payment::doApply()
{
    auto const deliverMin = ctx_.tx[~sfDeliverMin];

    // Ripple if source or destination is non-native or if there are paths.
    std::uint32_t const txFlags = ctx_.tx.getFlags();
    bool const partialPaymentAllowed = txFlags & tfPartialPayment;
    bool const limitQuality = txFlags & tfLimitQuality;
    bool const defaultPathsAllowed = !(txFlags & tfNoRippleDirect);
    auto const hasPaths = ctx_.tx.isFieldPresent(sfPaths);
    auto const sendMax = ctx_.tx[~sfSendMax];

    AccountID const dstAccountID(ctx_.tx.getAccountID(sfDestination));
    STAmount const dstAmount(ctx_.tx.getFieldAmount(sfAmount));
    bool const mptDirect = dstAmount.holds<MPTIssue>();
    STAmount const maxSourceAmount =
        getMaxSourceAmount(account_, dstAmount, sendMax);

    JLOG(j_.trace()) << "maxSourceAmount=" << maxSourceAmount.getFullText()
                     << " dstAmount=" << dstAmount.getFullText();

    // Open a ledger for editing.
    auto const k = keylet::account(dstAccountID);
    SLE::pointer sleDst = view().peek(k);

    if (!sleDst)
    {
        std::uint32_t const seqno{
            view().rules().enabled(featureDeletableAccounts) ? view().seq()
                                                             : 1};

        // Create the account.
        sleDst = std::make_shared<SLE>(k);
        sleDst->setAccountID(sfAccount, dstAccountID);
        sleDst->setFieldU32(sfSequence, seqno);

        view().insert(sleDst);
    }
    else
    {
        // Tell the engine that we are intending to change the destination
        // account.  The source account gets always charged a fee so it's always
        // marked as modified.
        view().update(sleDst);
    }

    // Determine whether the destination requires deposit authorization.
    bool const depositAuth = view().rules().enabled(featureDepositAuth);
    bool const reqDepositAuth =
        sleDst->getFlags() & lsfDepositAuth && depositAuth;

    bool const depositPreauth = view().rules().enabled(featureDepositPreauth);

    bool const ripple =
        (hasPaths || sendMax || !dstAmount.native()) && !mptDirect;

    // If the destination has lsfDepositAuth set, then only direct XRP
    // payments (no intermediate steps) are allowed to the destination.
    if (!depositPreauth && ripple && reqDepositAuth)
        return tecNO_PERMISSION;

    if (ripple)
    {
        // Ripple payment with at least one intermediate step and uses
        // transitive balances.

        if (depositPreauth && depositAuth)
        {
            // If depositPreauth is enabled, then an account that requires
            // authorization has two ways to get an IOU Payment in:
            //  1. If Account == Destination, or
            //  2. If Account is deposit preauthorized by destination.

            if (auto err =
                    verifyDepositPreauth(ctx_, account_, dstAccountID, sleDst);
                !isTesSuccess(err))
                return err;
        }

        path::RippleCalc::Input rcInput;
        rcInput.partialPaymentAllowed = partialPaymentAllowed;
        rcInput.defaultPathsAllowed = defaultPathsAllowed;
        rcInput.limitQuality = limitQuality;
        rcInput.isLedgerOpen = view().open();

        path::RippleCalc::Output rc;
        {
            PaymentSandbox pv(&view());
            JLOG(j_.debug()) << "Entering RippleCalc in payment: "
                             << ctx_.tx.getTransactionID();
            rc = path::RippleCalc::rippleCalculate(
                pv,
                maxSourceAmount,
                dstAmount,
                dstAccountID,
                account_,
                ctx_.tx.getFieldPathSet(sfPaths),
                ctx_.app.logs(),
                &rcInput);
            // VFALCO NOTE We might not need to apply, depending
            //             on the TER. But always applying *should*
            //             be safe.
            pv.apply(ctx_.rawView());
        }

        // TODO: is this right?  If the amount is the correct amount, was
        // the delivered amount previously set?
        if (rc.result() == tesSUCCESS && rc.actualAmountOut != dstAmount)
        {
            if (deliverMin && rc.actualAmountOut < *deliverMin)
                rc.setResult(tecPATH_PARTIAL);
            else
                ctx_.deliver(rc.actualAmountOut);
        }

        auto terResult = rc.result();

        // Because of its overhead, if RippleCalc
        // fails with a retry code, claim a fee
        // instead. Maybe the user will be more
        // careful with their path spec next time.
        if (isTerRetry(terResult))
            terResult = tecPATH_DRY;
        return terResult;
    }
    else if (mptDirect)
    {
        JLOG(j_.trace()) << " dstAmount=" << dstAmount.getFullText();
        auto const& mptIssue = dstAmount.get<MPTIssue>();

        if (auto const ter = requireAuth(view(), mptIssue, account_);
            ter != tesSUCCESS)
            return ter;

        if (auto const ter = requireAuth(view(), mptIssue, dstAccountID);
            ter != tesSUCCESS)
            return ter;

        if (auto const ter =
                canTransfer(view(), mptIssue, account_, dstAccountID);
            ter != tesSUCCESS)
            return ter;

        if (auto err =
                verifyDepositPreauth(ctx_, account_, dstAccountID, sleDst);
            !isTesSuccess(err))
            return err;

        auto const& issuer = mptIssue.getIssuer();

        // Transfer rate
        Rate rate{QUALITY_ONE};
        // Payment between the holders
        if (account_ != issuer && dstAccountID != issuer)
        {
            // If globally/individually locked then
            //   - can't send between holders
            //   - holder can send back to issuer
            //   - issuer can send to holder
            if (isFrozen(view(), account_, mptIssue) ||
                isFrozen(view(), dstAccountID, mptIssue))
                return tecLOCKED;

            // Get the rate for a payment between the holders.
            rate = transferRate(view(), mptIssue.getMptID());
        }

        // Amount to deliver.
        STAmount amountDeliver = dstAmount;
        // Factor in the transfer rate.
        // No rounding. It'll change once MPT integrated into DEX.
        STAmount requiredMaxSourceAmount = multiply(dstAmount, rate);

        // Send more than the account wants to pay or less than
        // the account wants to deliver (if no SendMax).
        // Adjust the amount to deliver.
        if (partialPaymentAllowed && requiredMaxSourceAmount > maxSourceAmount)
        {
            requiredMaxSourceAmount = maxSourceAmount;
            // No rounding. It'll change once MPT integrated into DEX.
            amountDeliver = divide(maxSourceAmount, rate);
        }

        if (requiredMaxSourceAmount > maxSourceAmount ||
            (deliverMin && amountDeliver < *deliverMin))
            return tecPATH_PARTIAL;

        PaymentSandbox pv(&view());
        auto res = accountSend(
            pv, account_, dstAccountID, amountDeliver, ctx_.journal);
        if (res == tesSUCCESS)
            pv.apply(ctx_.rawView());
        else if (res == tecINSUFFICIENT_FUNDS || res == tecPATH_DRY)
            res = tecPATH_PARTIAL;

        return res;
    }

    assert(dstAmount.native());

    // Direct XRP payment.

    auto const sleSrc = view().peek(keylet::account(account_));
    if (!sleSrc)
        return tefINTERNAL;

    // ownerCount is the number of entries in this ledger for this
    // account that require a reserve.
    auto const ownerCount = sleSrc->getFieldU32(sfOwnerCount);

    // This is the total reserve in drops.
    auto const reserve = view().fees().accountReserve(ownerCount);

    // mPriorBalance is the balance on the sending account BEFORE the
    // fees were charged. We want to make sure we have enough reserve
    // to send. Allow final spend to use reserve for fee.
    auto const mmm = std::max(reserve, ctx_.tx.getFieldAmount(sfFee).xrp());

    if (mPriorBalance < dstAmount.xrp() + mmm)
    {
        // Vote no. However the transaction might succeed, if applied in
        // a different order.
        JLOG(j_.trace()) << "Delay transaction: Insufficient funds: "
                         << to_string(mPriorBalance) << " / "
                         << to_string(dstAmount.xrp() + mmm) << " ("
                         << to_string(reserve) << ")";

        return tecUNFUNDED_PAYMENT;
    }

    // AMMs can never receive an XRP payment.
    // Must use AMMDeposit transaction instead.
    if (sleDst->isFieldPresent(sfAMMID))
        return tecNO_PERMISSION;

    // The source account does have enough money.  Make sure the
    // source account has authority to deposit to the destination.
    if (depositAuth)
    {
        // If depositPreauth is enabled, then an account that requires
        // authorization has three ways to get an XRP Payment in:
        //  1. If Account == Destination, or
        //  2. If Account is deposit preauthorized by destination, or
        //  3. If the destination's XRP balance is
        //    a. less than or equal to the base reserve and
        //    b. the deposit amount is less than or equal to the base reserve,
        // then we allow the deposit.
        //
        // Rule 3 is designed to keep an account from getting wedged
        // in an unusable state if it sets the lsfDepositAuth flag and
        // then consumes all of its XRP.  Without the rule if an
        // account with lsfDepositAuth set spent all of its XRP, it
        // would be unable to acquire more XRP required to pay fees.
        //
        // We choose the base reserve as our bound because it is
        // a small number that seldom changes but is always sufficient
        // to get the account un-wedged.

        // Get the base reserve.
        XRPAmount const dstReserve{view().fees().accountReserve(0)};

        if (dstAmount > dstReserve ||
            sleDst->getFieldAmount(sfBalance) > dstReserve)
        {
            if (auto err =
                    verifyDepositPreauth(ctx_, account_, dstAccountID, sleDst);
                !isTesSuccess(err))
                return err;
        }
    }

    // Do the arithmetic for the transfer and make the ledger change.
    sleSrc->setFieldAmount(sfBalance, mSourceBalance - dstAmount);
    sleDst->setFieldAmount(
        sfBalance, sleDst->getFieldAmount(sfBalance) + dstAmount);

    // Re-arm the password change fee if we can and need to.
    if ((sleDst->getFlags() & lsfPasswordSpent))
        sleDst->clearFlag(lsfPasswordSpent);

    return tesSUCCESS;
}

}  // namespace ripple

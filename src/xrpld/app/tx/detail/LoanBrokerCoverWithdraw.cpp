//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#include <xrpld/app/tx/detail/LoanBrokerCoverWithdraw.h>
//
#include <xrpld/app/misc/LendingHelpers.h>
#include <xrpld/app/tx/detail/Payment.h>
#include <xrpld/ledger/ApplyView.h>
#include <xrpld/ledger/View.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>

namespace ripple {

bool
LoanBrokerCoverWithdraw::isEnabled(PreflightContext const& ctx)
{
    return lendingProtocolEnabled(ctx);
}

NotTEC
LoanBrokerCoverWithdraw::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfLoanBrokerID] == beast::zero)
        return temINVALID;

    auto const dstAmount = ctx.tx[sfAmount];
    if (dstAmount <= beast::zero)
        return temBAD_AMOUNT;

    if (!isLegalNet(dstAmount))
        return temBAD_AMOUNT;

    if (auto const destination = ctx.tx[~sfDestination];
        destination.has_value())
    {
        if (*destination == beast::zero)
        {
            JLOG(ctx.j.debug())
                << "LoanBrokerCoverWithdraw: zero/empty destination account.";
            return temMALFORMED;
        }
    }
    else if (ctx.tx.isFieldPresent(sfDestinationTag))
    {
        JLOG(ctx.j.debug())
            << "LoanBrokerCoverWithdraw: sfDestinationTag is set but "
               "sfDestination is not";
        return temMALFORMED;
    }

    return tesSUCCESS;
}

TER
LoanBrokerCoverWithdraw::preclaim(PreclaimContext const& ctx)
{
    auto const& tx = ctx.tx;

    auto const account = tx[sfAccount];
    auto const brokerID = tx[sfLoanBrokerID];
    auto const amount = tx[sfAmount];

    auto const dstAcct = [&]() -> AccountID {
        if (auto const dst = tx[~sfDestination])
            return *dst;
        return account;
    }();

    auto const sleBroker = ctx.view.read(keylet::loanbroker(brokerID));
    if (!sleBroker)
    {
        JLOG(ctx.j.warn()) << "LoanBroker does not exist.";
        return tecNO_ENTRY;
    }
    if (account != sleBroker->at(sfOwner))
    {
        JLOG(ctx.j.warn()) << "Account is not the owner of the LoanBroker.";
        return tecNO_PERMISSION;
    }
    auto const vault = ctx.view.read(keylet::vault(sleBroker->at(sfVaultID)));
    auto const vaultAsset = vault->at(sfAsset);

    if (amount.asset() != vaultAsset)
        return tecWRONG_ASSET;

    // Withdrawal to a 3rd party destination account is essentially a transfer.
    // Enforce all the usual asset transfer checks.
    AuthType authType = AuthType::Legacy;
    if (account != dstAcct)
    {
        auto const sleDst = ctx.view.read(keylet::account(dstAcct));
        if (sleDst == nullptr)
            return tecNO_DST;

        if (sleDst->isFlag(lsfRequireDestTag) &&
            !tx.isFieldPresent(sfDestinationTag))
            return tecDST_TAG_NEEDED;  // Cannot send without a tag

        if (sleDst->isFlag(lsfDepositAuth))
        {
            if (!ctx.view.exists(keylet::depositPreauth(dstAcct, account)))
                return tecNO_PERMISSION;
        }
        // The destination account must have consented to receive the asset by
        // creating a RippleState or MPToken
        authType = AuthType::StrongAuth;
    }

    // Destination MPToken must exist (if asset is an MPT)
    if (auto const ter = requireAuth(ctx.view, vaultAsset, dstAcct, authType))
        return ter;

    // The broker's pseudo-account is the source of funds.
    auto const pseudoAccountID = sleBroker->at(sfAccount);

    // Check for freezes, unless sending directly to the issuer
    if (dstAcct != vaultAsset.getIssuer())
    {
        // Cannot send a frozen Asset
        if (auto const ret = checkFrozen(ctx.view, pseudoAccountID, vaultAsset))
            return ret;
        // Destination account cannot receive if asset is deep frozen
        if (auto const ret = checkDeepFrozen(ctx.view, dstAcct, vaultAsset))
            return ret;
    }

    auto const coverAvail = sleBroker->at(sfCoverAvailable);
    // Cover Rate is in 1/10 bips units
    auto const currentDebtTotal = sleBroker->at(sfDebtTotal);
    auto const minimumCover = roundToAsset(
        vaultAsset,
        tenthBipsOfValue(
            currentDebtTotal, TenthBips32(sleBroker->at(sfCoverRateMinimum))),
        currentDebtTotal);
    if (coverAvail < amount)
        return tecINSUFFICIENT_FUNDS;
    if ((coverAvail - amount) < minimumCover)
        return tecINSUFFICIENT_FUNDS;

    if (accountHolds(
            ctx.view,
            pseudoAccountID,
            vaultAsset,
            FreezeHandling::fhZERO_IF_FROZEN,
            AuthHandling::ahZERO_IF_UNAUTHORIZED,
            ctx.j) < amount)
        return tecINSUFFICIENT_FUNDS;

    return tesSUCCESS;
}

TER
LoanBrokerCoverWithdraw::doApply()
{
    auto const& tx = ctx_.tx;

    auto const brokerID = tx[sfLoanBrokerID];
    auto const amount = tx[sfAmount];
    auto const dstAcct = [&]() -> AccountID {
        if (auto const dst = tx[~sfDestination])
            return *dst;
        return account_;
    }();

    auto broker = view().peek(keylet::loanbroker(brokerID));
    if (!broker)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const brokerPseudoID = *broker->at(sfAccount);

    // Decrease the LoanBroker's CoverAvailable by Amount
    broker->at(sfCoverAvailable) -= amount;
    view().update(broker);

    // Move the funds from the broker's pseudo-account to the dstAcct

    if (dstAcct == account_ || amount.native())
    {
        // Transfer assets directly from pseudo-account to depositor.
        // Because this is either a self-transfer or an XRP payment, there is no
        // need to use the payment engine.
        return accountSend(
            view(), brokerPseudoID, dstAcct, amount, j_, WaiveTransferFee::Yes);
    }

    {
        // If sending the Cover to a different account, then this is
        // effectively a payment. Use the Payment transaction code to call
        // the payment engine, though only a subset of the functionality is
        // supported in this transaction. e.g. No paths, no partial
        // payments.
        bool const mptDirect = amount.holds<MPTIssue>();
        STAmount const maxSourceAmount =
            Payment::getMaxSourceAmount(brokerPseudoID, amount);
        SLE::pointer sleDst = view().peek(keylet::account(dstAcct));
        if (!sleDst)
            return tecINTERNAL;

        Payment::RipplePaymentParams paymentParams{
            .ctx = ctx_,
            .maxSourceAmount = maxSourceAmount,
            .srcAccountID = brokerPseudoID,
            .dstAccountID = dstAcct,
            .sleDst = sleDst,
            .dstAmount = amount,
            .paths = STPathSet{},
            .deliverMin = std::nullopt,
            .j = j_};

        TER ret;
        if (mptDirect)
        {
            ret = Payment::makeMPTDirectPayment(paymentParams);
        }
        else
        {
            ret = Payment::makeRipplePayment(paymentParams);
        }
        // Always claim a fee
        if (!isTesSuccess(ret) && !isTecClaim(ret))
        {
            JLOG(j_.info())
                << "LoanBrokerCoverWithdraw: changing result from "
                << transToken(ret)
                << " to tecPATH_DRY for IOU payment with Destination";
            return tecPATH_DRY;
        }
        return ret;
    }
}

//------------------------------------------------------------------------------

}  // namespace ripple

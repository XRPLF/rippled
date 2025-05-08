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

#include <xrpld/app/tx/detail/LoanPay.h>
//
#include <xrpld/app/misc/LendingHelpers.h>
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
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STXChainBridge.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>

namespace ripple {

bool
LoanPay::isEnabled(PreflightContext const& ctx)
{
    return lendingProtocolEnabled(ctx);
}

NotTEC
LoanPay::doPreflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfLoanID] == beast::zero)
        return temINVALID;

    if (ctx.tx[sfAmount] <= beast::zero)
        return temBAD_AMOUNT;

    return tesSUCCESS;
}

TER
LoanPay::preclaim(PreclaimContext const& ctx)
{
    auto const& tx = ctx.tx;

    auto const account = tx[sfAccount];
    auto const loanID = tx[sfLoanID];
    auto const amount = tx[sfAmount];

    auto const loanSle = ctx.view.read(keylet::loan(loanID));
    if (!loanSle)
    {
        JLOG(ctx.j.warn()) << "Loan does not exist.";
        return tecNO_ENTRY;
    }

    auto const principalOutstanding = loanSle->at(sfPrincipalOutstanding);
    TenthBips32 const interestRate{loanSle->at(sfInterestRate)};
    auto const paymentInterval = loanSle->at(sfPaymentInterval);
    auto const paymentRemaining = loanSle->at(sfPaymentRemaining);
    TenthBips32 const lateInterestRate{loanSle->at(sfLateInterestRate)};
    auto const latePaymentFee = loanSle->at(sfLatePaymentFee);
    auto const prevPaymentDate = loanSle->at(sfPreviousPaymentDate);
    auto const startDate = loanSle->at(sfStartDate);
    auto const nextDueDate = loanSle->at(sfNextPaymentDueDate);

    if (loanSle->at(sfBorrower) != account)
    {
        JLOG(ctx.j.warn()) << "Loan does not belong to the account.";
        return tecNO_PERMISSION;
    }

    if (!hasExpired(ctx.view, startDate))
    {
        JLOG(ctx.j.warn()) << "Loan has not started yet.";
        return tecTOO_SOON;
    }

    if (paymentRemaining == 0 || principalOutstanding == 0)
    {
        JLOG(ctx.j.warn()) << "Loan is already paid off.";
        return tecKILLED;
    }

    auto const loanBrokerID = loanSle->at(sfLoanBrokerID);
    auto const loanBrokerSle = ctx.view.read(keylet::loanbroker(loanBrokerID));
    if (!loanBrokerSle)
    {
        // This should be impossible
        // LCOV_EXCL_START
        JLOG(ctx.j.fatal()) << "LoanBroker does not exist.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }
    auto const brokerPseudoAccount = loanBrokerSle->at(sfAccount);
    auto const vaultID = loanBrokerSle->at(sfVaultID);
    auto const vaultSle = ctx.view.read(keylet::vault(vaultID));
    if (!vaultSle)
    {
        // This should be impossible
        // LCOV_EXCL_START
        JLOG(ctx.j.fatal()) << "Vault does not exist.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }
    auto const asset = vaultSle->at(sfAsset);

    if (amount.asset() != asset)
    {
        JLOG(ctx.j.warn()) << "Loan amount does not match the Vault asset.";
        return tecWRONG_ASSET;
    }

    if (isFrozen(ctx.view, brokerPseudoAccount, asset))
    {
        JLOG(ctx.j.warn()) << "Loan Broker pseudo-account is frozen.";
        return asset.holds<Issue>() ? tecFROZEN : tecLOCKED;
    }
    if (asset.holds<Issue>())
    {
        auto const issue = asset.get<Issue>();
        if (isDeepFrozen(ctx.view, account, issue.currency, issue.account))
        {
            JLOG(ctx.j.warn()) << "Borrower account is frozen.";
            return tecFROZEN;
        }
    }

    return tesSUCCESS;
}

TER
LoanPay::doApply()
{
    auto const& tx = ctx_.tx;
    auto& view = ctx_.view();

    auto const amount = tx[sfAmount];

    auto const loanID = tx[sfLoanID];
    auto const loanSle = view.peek(keylet::loan(loanID));
    if (!loanSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    auto const brokerID = loanSle->at(sfLoanBrokerID);
    auto const brokerSle = view.peek(keylet::loanbroker(brokerID));
    if (!brokerSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    auto const brokerOwner = brokerSle->at(sfOwner);
    auto const brokerPseudoAccount = brokerSle->at(sfAccount);
    auto const vaultID = brokerSle->at(sfVaultID);
    auto const vaultSle = view.peek(keylet::vault(vaultID));
    if (!vaultSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    auto const vaultPseudoAccount = vaultSle->at(sfAccount);
    auto const asset = vaultSle->at(sfAsset);

    //------------------------------------------------------
    // Loan object state changes
    view.update(loanSle);

    Expected<LoanPaymentParts, TER> paymentParts =
        loanComputePaymentParts(asset, view, loanSle, amount, j_);

    if (!paymentParts)
        return paymentParts.error();

    // If the loan was impaired, it isn't anymore.
    loanSle->clearFlag(lsfLoanImpaired);

    //------------------------------------------------------
    // LoanBroker object state changes
    view.update(brokerSle);

    TenthBips32 managementFeeRate{brokerSle->at(sfManagementFeeRate)};
    auto const managementFee = roundToAsset(
        asset, tenthBipsOfValue(paymentParts->interestPaid, managementFeeRate));

    auto const totalPaidToVault = paymentParts->principalPaid +
        paymentParts->interestPaid - managementFee;

    auto const totalFee = paymentParts->feePaid + managementFee;

    // If there is not enough first-loss capital
    auto coverAvailableField = brokerSle->at(sfCoverAvailable);
    auto debtTotalField = brokerSle->at(sfDebtTotal);
    TenthBips32 const coverRateMinimum{brokerSle->at(sfCoverRateMinimum)};

    bool const sufficientCover = coverAvailableField >=
        tenthBipsOfValue(debtTotalField.value(), coverRateMinimum);
    if (!sufficientCover)
    {
        // Add the fee to to First Loss Cover Pool
        coverAvailableField += totalFee;
    }

    // Decrease LoanBroker Debt by the amount paid, add the Loan value change,
    // and subtract the change in the management fee
    auto const vaultValueChange = paymentParts->valueChange -
        tenthBipsOfValue(paymentParts->valueChange, managementFeeRate);
    debtTotalField += vaultValueChange - totalPaidToVault;

    //------------------------------------------------------
    // Vault object state changes
    view.update(vaultSle);

    vaultSle->at(sfAssetsAvailable) += totalPaidToVault;
    vaultSle->at(sfAssetsTotal) += vaultValueChange;

    // Move funds
    STAmount const paidToVault(asset, totalPaidToVault);
    STAmount const paidToBroker(asset, totalFee);
    XRPL_ASSERT2(
        paidToVault + paidToBroker == amount,
        "ripple::LoanPay::doApply",
        "correct payment totals");

    if (auto const ter = accountSend(
            view,
            account_,
            vaultPseudoAccount,
            paidToVault,
            j_,
            WaiveTransferFee::Yes))
        return ter;
    if (auto const ter = accountSend(
            view,
            account_,
            sufficientCover ? brokerOwner : brokerPseudoAccount,
            paidToBroker,
            j_,
            WaiveTransferFee::Yes))
        return ter;

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

}  // namespace ripple

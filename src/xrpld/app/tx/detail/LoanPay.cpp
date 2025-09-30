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

namespace ripple {

bool
LoanPay::checkExtraFeatures(PreflightContext const& ctx)
{
    return checkLendingProtocolDependencies(ctx);
}

NotTEC
LoanPay::preflight(PreflightContext const& ctx)
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
    auto const paymentRemaining = loanSle->at(sfPaymentRemaining);
    TenthBips32 const lateInterestRate{loanSle->at(sfLateInterestRate)};

    if (loanSle->at(sfBorrower) != account)
    {
        JLOG(ctx.j.warn()) << "Loan does not belong to the account.";
        return tecNO_PERMISSION;
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

    if (auto const ret = checkFrozen(ctx.view, account, asset))
    {
        JLOG(ctx.j.warn()) << "Borrower account is frozen.";
        return ret;
    }
    if (auto const ret = checkDeepFrozen(ctx.view, brokerPseudoAccount, asset))
    {
        JLOG(ctx.j.warn()) << "Loan Broker pseudo-account can not receive "
                              "funds (deep frozen).";
        return ret;
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
    auto const asset = *vaultSle->at(sfAsset);

    //------------------------------------------------------
    // Loan object state changes
    Number const originalPrincipalRequested = loanSle->at(sfPrincipalRequested);

    Expected<LoanPaymentParts, TER> paymentParts =
        loanMakePayment(asset, view, loanSle, amount, j_);

    if (!paymentParts)
        return paymentParts.error();

    // If the payment computation completed without error, the loanSle object
    // has been modified.
    view.update(loanSle);

    // If the loan was impaired, it isn't anymore.
    loanSle->clearFlag(lsfLoanImpaired);

    XRPL_ASSERT_PARTS(
        paymentParts->principalPaid > 0,
        "ripple::LoanPay::doApply",
        "valid principal paid");
    XRPL_ASSERT_PARTS(
        paymentParts->interestPaid >= 0,
        "ripple::LoanPay::doApply",
        "valid interest paid");
    XRPL_ASSERT_PARTS(
        paymentParts->feePaid >= 0,
        "ripple::LoanPay::doApply",
        "valid fee paid");
    if (paymentParts->principalPaid <= 0 || paymentParts->interestPaid < 0 ||
        paymentParts->feePaid < 0)
    {
        // LCOV_EXCL_START
        JLOG(j_.fatal()) << "Loan payment computation returned invalid values.";
        return tecINTERNAL;
        // LCOV_EXCL_STOP
    }

    //------------------------------------------------------
    // LoanBroker object state changes
    view.update(brokerSle);

    TenthBips32 managementFeeRate{brokerSle->at(sfManagementFeeRate)};
    auto const managementFee = roundToAsset(
        asset,
        tenthBipsOfValue(paymentParts->interestPaid, managementFeeRate),
        originalPrincipalRequested);

    auto const totalPaidToVault = paymentParts->principalPaid +
        paymentParts->interestPaid - managementFee;

    auto const totalPaidToBroker = paymentParts->feePaid + managementFee;

    XRPL_ASSERT_PARTS(
        (totalPaidToVault + totalPaidToBroker) ==
            (paymentParts->principalPaid + paymentParts->interestPaid +
             paymentParts->feePaid),
        "ripple::LoanPay::doApply",
        "payments add up");

    // If there is not enough first-loss capital
    auto coverAvailableField = brokerSle->at(sfCoverAvailable);
    auto debtTotalField = brokerSle->at(sfDebtTotal);
    TenthBips32 const coverRateMinimum{brokerSle->at(sfCoverRateMinimum)};

    bool const sufficientCover = coverAvailableField >=
        roundToAsset(asset,
                     tenthBipsOfValue(debtTotalField.value(), coverRateMinimum),
                     originalPrincipalRequested);
    if (!sufficientCover)
    {
        // Add the fee to First Loss Cover Pool
        coverAvailableField += totalPaidToBroker;
    }
    auto const brokerPayee =
        sufficientCover ? brokerOwner : brokerPseudoAccount;

    // Decrease LoanBroker Debt by the amount paid, add the Loan value change,
    // and subtract the change in the management fee
    auto const vaultValueChange = valueMinusManagementFee(
        asset,
        paymentParts->valueChange,
        managementFeeRate,
        originalPrincipalRequested);
    // debtDecrease may be negative, increasing the debt
    auto const debtDecrease = totalPaidToVault - vaultValueChange;
    XRPL_ASSERT_PARTS(
        roundToAsset(asset, debtDecrease, originalPrincipalRequested) ==
            debtDecrease,
        "ripple::LoanPay::doApply",
        "debtDecrease rounding good");
    if (debtDecrease >= debtTotalField)
        debtTotalField = 0;
    else
        debtTotalField -= debtDecrease;

    //------------------------------------------------------
    // Vault object state changes
    view.update(vaultSle);

    vaultSle->at(sfAssetsAvailable) += totalPaidToVault;
    vaultSle->at(sfAssetsTotal) += vaultValueChange;

    // Move funds
    STAmount const paidToVault(asset, totalPaidToVault);
    STAmount const paidToBroker(asset, totalPaidToBroker);
    XRPL_ASSERT_PARTS(
        paidToVault + paidToBroker <= amount,
        "ripple::LoanPay::doApply",
        "amount is sufficient");
    XRPL_ASSERT_PARTS(
        paidToVault + paidToBroker <= paymentParts->principalPaid +
                paymentParts->interestPaid + paymentParts->feePaid,
        "ripple::LoanPay::doApply",
        "payment agreement");

    auto const accountBalanceBefore =
        accountHolds(view, account_, asset, fhIGNORE_FREEZE, ahIGNORE_AUTH, j_);
    auto const vaultBalanceBefore = accountHolds(
        view, vaultPseudoAccount, asset, fhIGNORE_FREEZE, ahIGNORE_AUTH, j_);
    auto const brokerBalanceBefore = accountHolds(
        view, brokerPayee, asset, fhIGNORE_FREEZE, ahIGNORE_AUTH, j_);

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
            brokerPayee,
            paidToBroker,
            j_,
            WaiveTransferFee::Yes))
        return ter;

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

}  // namespace ripple

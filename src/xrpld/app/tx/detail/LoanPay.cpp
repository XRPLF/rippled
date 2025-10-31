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
#include <xrpld/app/tx/detail/LoanManage.h>

#include <xrpl/json/to_string.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

bool
LoanPay::checkExtraFeatures(PreflightContext const& ctx)
{
    return checkLendingProtocolDependencies(ctx);
}

std::uint32_t
LoanPay::getFlagsMask(PreflightContext const& ctx)
{
    return tfLoanPayMask;
}

NotTEC
LoanPay::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfLoanID] == beast::zero)
        return temINVALID;

    if (ctx.tx[sfAmount] <= beast::zero)
        return temBAD_AMOUNT;

    // isFlag requires an exact match - all flags to be set - to return true.
    if (ctx.tx.isFlag(tfLoanOverpayment | tfLoanFullPayment))
        return temINVALID_FLAG;

    return tesSUCCESS;
}

XRPAmount
LoanPay::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    auto const normalCost = Transactor::calculateBaseFee(view, tx);

    if (tx.isFlag(tfLoanFullPayment))
        // The loan will be making one set of calculations for one (large)
        // payment
        return normalCost;

    // The fee is based on the potential number of payments, unless the loan is
    // being fully paid off.
    auto const amount = tx[sfAmount];
    auto const loanID = tx[sfLoanID];

    auto const loanSle = view.read(keylet::loan(loanID));
    if (!loanSle)
        // Let preclaim worry about the error for this
        return normalCost;

    if (loanSle->at(sfPaymentRemaining) <= loanPaymentsPerFeeIncrement)
    {
        // If there are fewer than loanPaymentsPerFeeIncrement payments left to
        // pay, we can skip the computations.
        return normalCost;
    }

    if (hasExpired(view, loanSle->at(sfNextPaymentDueDate)))
        // If the payment is late, it'll only make one payment
        return normalCost;

    auto const brokerSle =
        view.read(keylet::loanbroker(loanSle->at(sfLoanBrokerID)));
    if (!brokerSle)
        // Let preclaim worry about the error for this
        return normalCost;
    auto const vaultSle = view.read(keylet::vault(brokerSle->at(sfVaultID)));
    if (!vaultSle)
        // Let preclaim worry about the error for this
        return normalCost;

    auto const asset = vaultSle->at(sfAsset);

    if (asset != amount.asset())
        // Let preclaim worry about the error for this
        return normalCost;

    auto const scale = loanSle->at(sfLoanScale);

    auto const regularPayment =
        roundPeriodicPayment(asset, loanSle->at(sfPeriodicPayment), scale) +
        loanSle->at(sfLoanServiceFee);

    // If making an overpayment, count it as a full payment because it will do
    // about the same amount of work, if not more.
    NumberRoundModeGuard mg(
        tx.isFlag(tfLoanOverpayment) ? Number::upward : Number::downward);
    // Estimate how many payments will be made
    Number const numPaymentEstimate =
        static_cast<std::int64_t>(amount / regularPayment);
    // Charge one base fee per paymentsPerFeeIncrement payments, rounding up.
    Number::setround(Number::upward);
    auto const feeIncrements = std::max(
        std::int64_t(1),
        static_cast<std::int64_t>(
            numPaymentEstimate / loanPaymentsPerFeeIncrement));

    return feeIncrements * normalCost;
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

    if (loanSle->at(sfBorrower) != account)
    {
        JLOG(ctx.j.warn()) << "Loan does not belong to the account.";
        return tecNO_PERMISSION;
    }

    if (tx.isFlag(tfLoanOverpayment) && !loanSle->isFlag(lsfLoanOverpayment))
    {
        JLOG(ctx.j.warn())
            << "Requested overpayment on a loan that doesn't allow it";
        return temINVALID_FLAG;
    }

    auto const principalOutstanding = loanSle->at(sfPrincipalOutstanding);
    TenthBips32 const interestRate{loanSle->at(sfInterestRate)};
    auto const paymentRemaining = loanSle->at(sfPaymentRemaining);
    TenthBips32 const lateInterestRate{loanSle->at(sfLateInterestRate)};

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
    auto const vaultPseudoAccount = vaultSle->at(sfAccount);

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
    if (auto const ret = checkDeepFrozen(ctx.view, vaultPseudoAccount, asset))
    {
        JLOG(ctx.j.warn())
            << "Vault pseudo-account can not receive funds (deep frozen).";
        return ret;
    }
    // Make sure the borrower has enough funds to make the payment!
    // Do not support "partial payments" - if the transaction says to pay X,
    // then the account must have X available, even if the loan payment takes
    // less.
    if (auto const balance = accountCanSend(
            ctx.view,
            account,
            asset,
            fhZERO_IF_FROZEN,
            ahZERO_IF_UNAUTHORIZED,
            ctx.j);
        balance < amount)
    {
        JLOG(ctx.j.warn()) << "Payment amount too large. Amount: "
                           << to_string(amount.getJson())
                           << ". Balance: " << to_string(balance.getJson());
        return tecINSUFFICIENT_FUNDS;
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
    std::int32_t const loanScale = loanSle->at(sfLoanScale);

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

    // Determine where to send the broker's fee
    auto coverAvailableProxy = brokerSle->at(sfCoverAvailable);
    TenthBips32 const coverRateMinimum{brokerSle->at(sfCoverRateMinimum)};
    auto debtTotalProxy = brokerSle->at(sfDebtTotal);

    // Send the broker fee to the owner if they have sufficient cover available,
    // _and_ if the owner can receive funds. If not, so as not to block the
    // payment, add it to the cover balance (send it to the broker pseudo
    // account).
    //
    // Normally freeze status is checked in preflight, but we do it here to
    // avoid duplicating the check. It'll claim a fee either way.
    bool const sendBrokerFeeToOwner = coverAvailableProxy >=
            roundToAsset(asset,
                         tenthBipsOfValue(
                             debtTotalProxy.value(), coverRateMinimum),
                         loanScale) &&
        !isDeepFrozen(view, brokerOwner, asset);

    auto const brokerPayee =
        sendBrokerFeeToOwner ? brokerOwner : brokerPseudoAccount;
    auto const brokerPayeeSle = view.peek(keylet::account(brokerPayee));
    if (!sendBrokerFeeToOwner)
    {
        // If we can't send the fee to the owner, and the pseudo-account is
        // frozen, then we have to fail the payment.
        if (auto const ret = checkDeepFrozen(view, brokerPayee, asset))
        {
            JLOG(j_.warn())
                << "Both Loan Broker and Loan Broker pseudo-account "
                   "can not receive funds (deep frozen).";
            return ret;
        }
    }

    //------------------------------------------------------
    // Loan object state changes

    // Unimpair the loan if it was impaired. Do this before the payment is
    // attempted, so the original values can be used. If the payment fails, this
    // change will be discarded.
    if (loanSle->isFlag(lsfLoanImpaired))
    {
        LoanManage::unimpairLoan(view, loanSle, vaultSle, j_);
    }

    Expected<LoanPaymentParts, TER> const paymentParts =
        tx.isFlag(tfLoanFullPayment) ? loanMakeFullPayment(
                                           asset,
                                           view,
                                           loanSle,
                                           brokerSle,
                                           amount,
                                           tx.isFlag(tfLoanOverpayment),
                                           j_)
                                     : loanMakePayment(
                                           asset,
                                           view,
                                           loanSle,
                                           brokerSle,
                                           amount,
                                           tx.isFlag(tfLoanOverpayment),
                                           j_);

    if (!paymentParts)
    {
        XRPL_ASSERT_PARTS(
            paymentParts.error(),
            "ripple::LoanPay::doApply",
            "payment error is an error");
        return paymentParts.error();
    }

    // If the payment computation completed without error, the loanSle object
    // has been modified.
    view.update(loanSle);

    XRPL_ASSERT_PARTS(
        // It is possible to pay 0 principal
        paymentParts->principalPaid >= 0,
        "ripple::LoanPay::doApply",
        "valid principal paid");
    XRPL_ASSERT_PARTS(
        // It is possible to pay 0 interest
        paymentParts->interestPaid >= 0,
        "ripple::LoanPay::doApply",
        "valid interest paid");
    XRPL_ASSERT_PARTS(
        // It should not be possible to pay 0 total
        paymentParts->principalPaid + paymentParts->interestPaid > 0,
        "ripple::LoanPay::doApply",
        "valid principal paid");
    XRPL_ASSERT_PARTS(
        paymentParts->feePaid >= 0,
        "ripple::LoanPay::doApply",
        "valid fee paid");

    if (paymentParts->principalPaid < 0 || paymentParts->interestPaid < 0 ||
        paymentParts->feePaid < 0)
    {
        // LCOV_EXCL_START
        JLOG(j_.fatal()) << "Loan payment computation returned invalid values.";
        return tecLIMIT_EXCEEDED;
        // LCOV_EXCL_STOP
    }

    //------------------------------------------------------
    // LoanBroker object state changes
    view.update(brokerSle);

    auto assetsAvailableProxy = vaultSle->at(sfAssetsAvailable);
    // The vault may be at a different scale than the loan. Reduce rounding
    // errors during the payment by rounding some of the values to that scale.
    auto const vaultScale = assetsAvailableProxy->value().exponent();

    auto const totalPaidToVaultRaw =
        paymentParts->principalPaid + paymentParts->interestPaid;
    auto const totalPaidToVaultRounded =
        roundToAsset(asset, totalPaidToVaultRaw, vaultScale, Number::downward);
    XRPL_ASSERT_PARTS(
        !asset.integral() || totalPaidToVaultRaw == totalPaidToVaultRounded,
        "ripple::LoanPay::doApply",
        "rounding does nothing for integral asset");
    auto const totalPaidToVaultForDebt =
        totalPaidToVaultRaw - paymentParts->valueChange;

    auto const totalPaidToBroker = paymentParts->feePaid;

    XRPL_ASSERT_PARTS(
        (totalPaidToVaultRaw + totalPaidToBroker) ==
            (paymentParts->principalPaid + paymentParts->interestPaid +
             paymentParts->feePaid),
        "ripple::LoanPay::doApply",
        "payments add up");

    // Decrease LoanBroker Debt by the amount paid, add the Loan value change
    // (which might be negative). totalPaidToVaultForDebt may be negative,
    // increasing the debt
    XRPL_ASSERT_PARTS(
        isRounded(asset, totalPaidToVaultForDebt, loanScale),
        "ripple::LoanPay::doApply",
        "totalPaidToVaultForDebt rounding good");
    // Despite our best efforts, it's possible for rounding errors to accumulate
    // in the loan broker's debt total. This is because the broker may have more
    // that one loan with significantly different scales.
    if (totalPaidToVaultForDebt >= debtTotalProxy)
        debtTotalProxy = 0;
    else
        debtTotalProxy -= totalPaidToVaultForDebt;

    //------------------------------------------------------
    // Vault object state changes
    view.update(vaultSle);

    Number const assetsAvailableBefore = *assetsAvailableProxy;
    Number const pseudoAccountBalanceBefore = accountHolds(
        view,
        vaultPseudoAccount,
        asset,
        FreezeHandling::fhIGNORE_FREEZE,
        AuthHandling::ahIGNORE_AUTH,
        j_);

    {
        XRPL_ASSERT_PARTS(
            assetsAvailableBefore == pseudoAccountBalanceBefore,
            "ripple::LoanPay::doApply",
            "vault pseudo balance agrees before");

        auto assetsTotalProxy = vaultSle->at(sfAssetsTotal);

        assetsAvailableProxy += totalPaidToVaultRounded;
        assetsTotalProxy += paymentParts->valueChange;

        XRPL_ASSERT_PARTS(
            *assetsAvailableProxy <= *assetsTotalProxy,
            "ripple::LoanPay::doApply",
            "assets available must not be greater than assets outstanding");

        if (*assetsAvailableProxy > *assetsTotalProxy)
        {
            // LCOV_EXCL_START
            return tecINTERNAL;
            // LCOV_EXCL_STOP
        }
    }

    // Move funds
    XRPL_ASSERT_PARTS(
        totalPaidToVaultRounded + totalPaidToBroker <= amount,
        "ripple::LoanPay::doApply",
        "amount is sufficient");

    if (!sendBrokerFeeToOwner)
    {
        // If there is not enough first-loss capital, add the fee to First Loss
        // Cover Pool. Note that this moves the entire fee - it does not attempt
        // to split it. The broker can Withdraw it later if they want, or leave
        // it for future needs.
        coverAvailableProxy += totalPaidToBroker;
    }

#if !NDEBUG
    auto const accountBalanceBefore = accountCanSend(
        view, account_, asset, fhIGNORE_FREEZE, ahIGNORE_AUTH, j_);
    auto const vaultBalanceBefore = account_ == vaultPseudoAccount
        ? STAmount{asset, 0}
        : accountCanSend(
              view,
              vaultPseudoAccount,
              asset,
              fhIGNORE_FREEZE,
              ahIGNORE_AUTH,
              j_);
    auto const brokerBalanceBefore = account_ == brokerPayee
        ? STAmount{asset, 0}
        : accountCanSend(
              view, brokerPayee, asset, fhIGNORE_FREEZE, ahIGNORE_AUTH, j_);
#endif

    if (totalPaidToVaultRounded != beast::zero)
    {
        if (auto const ter = requireAuth(
                view, asset, vaultPseudoAccount, AuthType::StrongAuth))
            return ter;
    }

    if (totalPaidToBroker != beast::zero)
    {
        if (brokerPayee == account_)
        {
            // The broker may have deleted their holding. Recreate it if needed
            if (auto const ter = addEmptyHolding(
                    view,
                    brokerPayee,
                    brokerPayeeSle->at(sfBalance).value().xrp(),
                    asset,
                    j_);
                ter && ter != tecDUPLICATE)
                // ignore tecDUPLICATE. That means the holding already exists,
                // and is fine here
                return ter;
        }
        if (auto const ter =
                requireAuth(view, asset, brokerPayee, AuthType::StrongAuth))
            return ter;
    }

    if (auto const ter = accountSendMulti(
            view,
            account_,
            asset,
            {{vaultPseudoAccount, totalPaidToVaultRounded},
             {brokerPayee, totalPaidToBroker}},
            j_,
            WaiveTransferFee::Yes))
        return ter;

    Number const assetsAvailableAfter = *assetsAvailableProxy;
    Number const pseudoAccountBalanceAfter = accountHolds(
        view,
        vaultPseudoAccount,
        asset,
        FreezeHandling::fhIGNORE_FREEZE,
        AuthHandling::ahIGNORE_AUTH,
        j_);
    XRPL_ASSERT_PARTS(
        assetsAvailableAfter == pseudoAccountBalanceAfter,
        "ripple::LoanPay::doApply",
        "vault pseudo balance agrees after");

#if !NDEBUG
    auto const accountBalanceAfter = accountCanSend(
        view, account_, asset, fhIGNORE_FREEZE, ahIGNORE_AUTH, j_);
    auto const vaultBalanceAfter = account_ == vaultPseudoAccount
        ? STAmount{asset, 0}
        : accountCanSend(
              view,
              vaultPseudoAccount,
              asset,
              fhIGNORE_FREEZE,
              ahIGNORE_AUTH,
              j_);
    auto const brokerBalanceAfter = account_ == brokerPayee
        ? STAmount{asset, 0}
        : accountCanSend(
              view, brokerPayee, asset, fhIGNORE_FREEZE, ahIGNORE_AUTH, j_);

    XRPL_ASSERT_PARTS(
        accountBalanceBefore + vaultBalanceBefore + brokerBalanceBefore ==
            accountBalanceAfter + vaultBalanceAfter + brokerBalanceAfter,
        "ripple::LoanPay::doApply",
        "funds are conserved (with rounding)");
    XRPL_ASSERT_PARTS(
        accountBalanceAfter >= beast::zero,
        "ripple::LoanPay::doApply",
        "positive account balance");
    XRPL_ASSERT_PARTS(
        accountBalanceAfter < accountBalanceBefore,
        "ripple::LoanPay::doApply",
        "account balance decreased");
    XRPL_ASSERT_PARTS(
        vaultBalanceAfter >= beast::zero && brokerBalanceAfter >= beast::zero,
        "ripple::LoanPay::doApply",
        "positive vault and broker balances");
    XRPL_ASSERT_PARTS(
        vaultBalanceAfter >= vaultBalanceBefore,
        "ripple::LoanPay::doApply",
        "vault balance did not decrease");
    XRPL_ASSERT_PARTS(
        brokerBalanceAfter >= brokerBalanceBefore,
        "ripple::LoanPay::doApply",
        "broker balance did not decrease");
    XRPL_ASSERT_PARTS(
        vaultBalanceAfter > vaultBalanceBefore ||
            brokerBalanceAfter > brokerBalanceBefore,
        "ripple::LoanPay::doApply",
        "vault and/or broker balance increased");
#endif

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

}  // namespace ripple

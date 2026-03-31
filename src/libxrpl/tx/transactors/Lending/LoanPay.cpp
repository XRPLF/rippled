#include <xrpl/tx/transactors/Lending/LoanPay.h>
//
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/STTakesAsset.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/tx/transactors/Lending/LendingHelpers.h>
#include <xrpl/tx/transactors/Lending/LoanManage.h>

#include <algorithm>
#include <bit>

namespace xrpl {

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

    // The loan payment flags are all mutually exclusive. If more than one is
    // set, the tx is malformed.
    static_assert(
        (tfLoanLatePayment | tfLoanFullPayment | tfLoanOverpayment) ==
        ~(tfLoanPayMask | tfUniversal));
    auto const flagsSet = ctx.tx.getFlags() & ~(tfLoanPayMask | tfUniversal);
    if (std::popcount(flagsSet) > 1)
    {
        JLOG(ctx.j.warn()) << "Only one LoanPay flag can be set per tx. " << flagsSet
                           << " is too many.";
        return temINVALID_FLAG;
    }

    return tesSUCCESS;
}

XRPAmount
LoanPay::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    using namespace Lending;

    auto const normalCost = Transactor::calculateBaseFee(view, tx);

    if (tx.isFlag(tfLoanFullPayment) || tx.isFlag(tfLoanLatePayment))
        // The loan will be making one set of calculations for one full or late
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
        // If the payment is late, and the late payment flag is not set, it'll
        // fail
        return normalCost;

    auto const brokerSle = view.read(keylet::loanbroker(loanSle->at(sfLoanBrokerID)));
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

    auto const regularPayment = roundPeriodicPayment(asset, loanSle->at(sfPeriodicPayment), scale) +
        loanSle->at(sfLoanServiceFee);

    // If making an overpayment, count it as a full payment because it will do
    // about the same amount of work, if not more.
    NumberRoundModeGuard mg(tx.isFlag(tfLoanOverpayment) ? Number::upward : Number::downward);
    // Estimate how many payments will be made
    Number const numPaymentEstimate = static_cast<std::int64_t>(amount / regularPayment);

    // Charge one base fee per paymentsPerFeeIncrement payments, rounding up.
    Number::setround(Number::upward);
    auto const feeIncrements = std::max(
        std::int64_t(1),
        static_cast<std::int64_t>(numPaymentEstimate / loanPaymentsPerFeeIncrement));

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
        JLOG(ctx.j.warn()) << "Requested overpayment on a loan that doesn't allow it";
        return temINVALID_FLAG;
    }

    auto const principalOutstanding = loanSle->at(sfPrincipalOutstanding);
    auto const paymentRemaining = loanSle->at(sfPaymentRemaining);

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
        JLOG(ctx.j.warn()) << "Vault pseudo-account can not receive funds (deep frozen).";
        return ret;
    }
    if (auto const ret = requireAuth(ctx.view, asset, account))
    {
        JLOG(ctx.j.warn()) << "Borrower account is not authorized.";
        return ret;
    }
    // Make sure the borrower has enough funds to make the payment!
    // Do not support "partial payments" - if the transaction says to pay X,
    // then the account must have X available, even if the loan payment takes
    // less.
    if (auto const balance = accountHolds(
            ctx.view,
            account,
            asset,
            fhZERO_IF_FROZEN,
            ahZERO_IF_UNAUTHORIZED,
            ctx.j,
            SpendableHandling::shFULL_BALANCE);
        balance < amount)
    {
        JLOG(ctx.j.warn()) << "Payment amount too large. Amount: " << to_string(amount.getJson())
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
    // _and_ if the owner can receive funds
    // _and_ if the broker is authorized to hold funds. If not, so as not to
    // block the payment, add it to the cover balance (send it to the broker
    // pseudo account).
    //
    // Normally freeze status is checked in preclaim, but we do it here to
    // avoid duplicating the check. It'll claim a fee either way.
    bool const sendBrokerFeeToOwner = [&]() {
        // Round the minimum required cover up to be conservative. This ensures
        // CoverAvailable never drops below the theoretical minimum, protecting
        // the broker's solvency.
        NumberRoundModeGuard mg(Number::upward);
        return coverAvailableProxy >=
            roundToAsset(
                   asset, tenthBipsOfValue(debtTotalProxy.value(), coverRateMinimum), loanScale) &&
            !isDeepFrozen(view, brokerOwner, asset) &&
            !requireAuth(view, asset, brokerOwner, AuthType::StrongAuth);
    }();

    auto const brokerPayee = sendBrokerFeeToOwner ? brokerOwner : brokerPseudoAccount;
    auto const brokerPayeeSle = view.peek(keylet::account(brokerPayee));
    if (!sendBrokerFeeToOwner)
    {
        // If we can't send the fee to the owner, and the pseudo-account is
        // frozen, then we have to fail the payment.
        if (auto const ret = checkDeepFrozen(view, brokerPayee, asset))
        {
            JLOG(j_.warn()) << "Both Loan Broker and Loan Broker pseudo-account "
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
        if (auto const ret = LoanManage::unimpairLoan(view, loanSle, vaultSle, asset, j_))
        {
            JLOG(j_.fatal()) << "Failed to unimpair loan before payment.";
            return ret;  // LCOV_EXCL_LINE
        }
    }

    LoanPaymentType const paymentType = [&tx]() {
        // preflight already checked that at most one flag is set.
        if (tx.isFlag(tfLoanLatePayment))
            return LoanPaymentType::late;
        if (tx.isFlag(tfLoanFullPayment))
            return LoanPaymentType::full;
        if (tx.isFlag(tfLoanOverpayment))
            return LoanPaymentType::overpayment;
        return LoanPaymentType::regular;
    }();

    Expected<LoanPaymentParts, TER> const paymentParts =
        loanMakePayment(asset, view, loanSle, brokerSle, amount, paymentType, j_);

    if (!paymentParts)
    {
        XRPL_ASSERT_PARTS(
            paymentParts.error(), "xrpl::LoanPay::doApply", "payment error is an error");
        return paymentParts.error();
    }

    // If the payment computation completed without error, the loanSle object
    // has been modified.
    view.update(loanSle);

    XRPL_ASSERT_PARTS(
        // It is possible to pay 0 principal
        paymentParts->principalPaid >= 0,
        "xrpl::LoanPay::doApply",
        "valid principal paid");
    XRPL_ASSERT_PARTS(
        // It is possible to pay 0 interest
        paymentParts->interestPaid >= 0,
        "xrpl::LoanPay::doApply",
        "valid interest paid");
    XRPL_ASSERT_PARTS(
        // It should not be possible to pay 0 total
        paymentParts->principalPaid + paymentParts->interestPaid > 0,
        "xrpl::LoanPay::doApply",
        "valid total paid");
    XRPL_ASSERT_PARTS(paymentParts->feePaid >= 0, "xrpl::LoanPay::doApply", "valid fee paid");

    if (paymentParts->principalPaid < 0 || paymentParts->interestPaid < 0 ||
        paymentParts->feePaid < 0)
    {
        // LCOV_EXCL_START
        JLOG(j_.fatal()) << "Loan payment computation returned invalid values.";
        return tecLIMIT_EXCEEDED;
        // LCOV_EXCL_STOP
    }

    JLOG(j_.debug()) << "Loan Pay: principal paid: " << paymentParts->principalPaid
                     << ", interest paid: " << paymentParts->interestPaid
                     << ", fee paid: " << paymentParts->feePaid
                     << ", value change: " << paymentParts->valueChange;

    //------------------------------------------------------
    // LoanBroker object state changes
    view.update(brokerSle);

    auto assetsAvailableProxy = vaultSle->at(sfAssetsAvailable);
    auto assetsTotalProxy = vaultSle->at(sfAssetsTotal);

    // The vault may be at a different scale than the loan. Reduce rounding
    // errors during the payment by rounding some of the values to that scale.
    auto const vaultScale = getAssetsTotalScale(vaultSle);

    auto const totalPaidToVaultRaw = paymentParts->principalPaid + paymentParts->interestPaid;
    auto const totalPaidToVaultRounded =
        roundToAsset(asset, totalPaidToVaultRaw, vaultScale, Number::downward);
    XRPL_ASSERT_PARTS(
        !asset.integral() || totalPaidToVaultRaw == totalPaidToVaultRounded,
        "xrpl::LoanPay::doApply",
        "rounding does nothing for integral asset");
    // Account for value changes when reducing the broker's debt:
    // - Positive value change (from full/late/overpayments): Subtract from the
    //   amount credited toward debt to avoid over-reducing the debt.
    // - Negative value change (from full/overpayments): Add to the amount
    //   credited toward debt,effectively increasing the debt reduction.
    auto const totalPaidToVaultForDebt = totalPaidToVaultRaw - paymentParts->valueChange;

    auto const totalPaidToBroker = paymentParts->feePaid;

    XRPL_ASSERT_PARTS(
        (totalPaidToVaultRaw + totalPaidToBroker) ==
            (paymentParts->principalPaid + paymentParts->interestPaid + paymentParts->feePaid),
        "xrpl::LoanPay::doApply",
        "payments add up");

    // Decrease LoanBroker Debt by the amount paid, add the Loan value change
    // (which might be negative). totalPaidToVaultForDebt may be negative,
    // increasing the debt
    XRPL_ASSERT_PARTS(
        isRounded(asset, totalPaidToVaultForDebt, loanScale),
        "xrpl::LoanPay::doApply",
        "totalPaidToVaultForDebt rounding good");
    // Despite our best efforts, it's possible for rounding errors to accumulate
    // in the loan broker's debt total. This is because the broker may have more
    // than one loan with significantly different scales.
    adjustImpreciseNumber(debtTotalProxy, -totalPaidToVaultForDebt, asset, vaultScale);

    //------------------------------------------------------
    // Vault object state changes
    view.update(vaultSle);

    Number const assetsAvailableBefore = *assetsAvailableProxy;
    Number const assetsTotalBefore = *assetsTotalProxy;
#if !NDEBUG
    {
        Number const pseudoAccountBalanceBefore = accountHolds(
            view,
            vaultPseudoAccount,
            asset,
            FreezeHandling::fhIGNORE_FREEZE,
            AuthHandling::ahIGNORE_AUTH,
            j_);

        XRPL_ASSERT_PARTS(
            assetsAvailableBefore == pseudoAccountBalanceBefore,
            "xrpl::LoanPay::doApply",
            "vault pseudo balance agrees before");
    }
#endif

    assetsAvailableProxy += totalPaidToVaultRounded;
    assetsTotalProxy += paymentParts->valueChange;

    XRPL_ASSERT_PARTS(
        *assetsAvailableProxy <= *assetsTotalProxy,
        "xrpl::LoanPay::doApply",
        "assets available must not be greater than assets outstanding");

    JLOG(j_.debug()) << "total paid to vault raw: " << totalPaidToVaultRaw
                     << ", total paid to vault rounded: " << totalPaidToVaultRounded
                     << ", total paid to broker: " << totalPaidToBroker
                     << ", amount from transaction: " << amount;

    // Move funds
    XRPL_ASSERT_PARTS(
        totalPaidToVaultRounded + totalPaidToBroker <= amount,
        "xrpl::LoanPay::doApply",
        "amount is sufficient");

    if (!sendBrokerFeeToOwner)
    {
        // If there is not enough first-loss capital, add the fee to First Loss
        // Cover Pool. Note that this moves the entire fee - it does not attempt
        // to split it. The broker can Withdraw it later if they want, or leave
        // it for future needs.
        coverAvailableProxy += totalPaidToBroker;
    }

    associateAsset(*loanSle, asset);
    associateAsset(*brokerSle, asset);
    associateAsset(*vaultSle, asset);

    // Duplicate some checks after rounding
    Number const assetsAvailableAfter = *assetsAvailableProxy;
    Number const assetsTotalAfter = *assetsTotalProxy;

    XRPL_ASSERT_PARTS(
        assetsAvailableAfter <= assetsTotalAfter,
        "xrpl::LoanPay::doApply",
        "assets available must not be greater than assets outstanding");
    if (assetsAvailableAfter == assetsAvailableBefore)
    {
        // An unchanged assetsAvailable indicates that the amount paid to the
        // vault was zero, or rounded to zero. That should be impossible, but I
        // can't rule it out for extreme edge cases, so fail gracefully if it
        // happens.
        //
        // LCOV_EXCL_START
        JLOG(j_.warn()) << "LoanPay: Vault assets available unchanged after rounding: "  //
                        << "Before: " << assetsAvailableBefore                           //
                        << ", After: " << assetsAvailableAfter;
        return tecPRECISION_LOSS;
        // LCOV_EXCL_STOP
    }
    if (paymentParts->valueChange != beast::zero && assetsTotalAfter == assetsTotalBefore)
    {
        // Non-zero valueChange with an unchanged assetsTotal indicates that the
        // actual value change rounded to zero. That should be impossible, but I
        // can't rule it out for extreme edge cases, so fail gracefully if it
        // happens.
        //
        // LCOV_EXCL_START
        JLOG(j_.warn())
            << "LoanPay: Vault assets expected change, but unchanged after rounding: "  //
            << "Before: " << assetsTotalBefore                                          //
            << ", After: " << assetsTotalAfter                                          //
            << ", ValueChange: " << paymentParts->valueChange;
        return tecPRECISION_LOSS;
        // LCOV_EXCL_STOP
    }
    if (paymentParts->valueChange == beast::zero && assetsTotalAfter != assetsTotalBefore)
    {
        // A change in assetsTotal when there was no valueChange indicates that
        // something really weird happened. That should be flat out impossible.
        //
        // LCOV_EXCL_START
        JLOG(j_.warn()) << "LoanPay: Vault assets changed unexpectedly after rounding: "  //
                        << "Before: " << assetsTotalBefore                                //
                        << ", After: " << assetsTotalAfter                                //
                        << ", ValueChange: " << paymentParts->valueChange;
        return tecINTERNAL;
        // LCOV_EXCL_STOP
    }
    if (assetsAvailableAfter > assetsTotalAfter)
    {
        // Assets available are not allowed to be larger than assets total.
        // LCOV_EXCL_START
        JLOG(j_.fatal()) << "LoanPay: Vault assets available must not be greater "
                            "than assets outstanding. Available: "
                         << assetsAvailableAfter << ", Total: " << assetsTotalAfter;
        return tecINTERNAL;
        // LCOV_EXCL_STOP
    }

    // These three values are used to check that funds are conserved after the transfers
    auto const accountBalanceBefore = accountHolds(
        view,
        account_,
        asset,
        fhIGNORE_FREEZE,
        ahIGNORE_AUTH,
        j_,
        SpendableHandling::shFULL_BALANCE);
    auto const vaultBalanceBefore = account_ == vaultPseudoAccount
        ? STAmount{asset, 0}
        : accountHolds(
              view,
              vaultPseudoAccount,
              asset,
              fhIGNORE_FREEZE,
              ahIGNORE_AUTH,
              j_,
              SpendableHandling::shFULL_BALANCE);
    auto const brokerBalanceBefore = account_ == brokerPayee
        ? STAmount{asset, 0}
        : accountHolds(
              view,
              brokerPayee,
              asset,
              fhIGNORE_FREEZE,
              ahIGNORE_AUTH,
              j_,
              SpendableHandling::shFULL_BALANCE);

    if (totalPaidToVaultRounded != beast::zero)
    {
        if (auto const ter = requireAuth(view, asset, vaultPseudoAccount, AuthType::StrongAuth))
            return ter;
    }

    if (totalPaidToBroker != beast::zero)
    {
        if (brokerPayee == account_)
        {
            // The broker may have deleted their holding. Recreate it if needed
            if (auto const ter = addEmptyHolding(
                    view, brokerPayee, brokerPayeeSle->at(sfBalance).value().xrp(), asset, j_);
                ter && ter != tecDUPLICATE)
                // ignore tecDUPLICATE. That means the holding already exists,
                // and is fine here
                return ter;
        }
        if (auto const ter = requireAuth(view, asset, brokerPayee, AuthType::StrongAuth))
            return ter;
    }

    if (auto const ter = accountSendMulti(
            view,
            account_,
            asset,
            {{vaultPseudoAccount, totalPaidToVaultRounded}, {brokerPayee, totalPaidToBroker}},
            j_,
            WaiveTransferFee::Yes))
        return ter;

#if !NDEBUG
    {
        Number const pseudoAccountBalanceAfter = accountHolds(
            view,
            vaultPseudoAccount,
            asset,
            FreezeHandling::fhIGNORE_FREEZE,
            AuthHandling::ahIGNORE_AUTH,
            j_);
        XRPL_ASSERT_PARTS(
            assetsAvailableAfter == pseudoAccountBalanceAfter,
            "xrpl::LoanPay::doApply",
            "vault pseudo balance agrees after");
    }
#endif

    // Check that funds are conserved
    auto const accountBalanceAfter = accountHolds(
        view,
        account_,
        asset,
        fhIGNORE_FREEZE,
        ahIGNORE_AUTH,
        j_,
        SpendableHandling::shFULL_BALANCE);
    auto const vaultBalanceAfter = account_ == vaultPseudoAccount
        ? STAmount{asset, 0}
        : accountHolds(
              view,
              vaultPseudoAccount,
              asset,
              fhIGNORE_FREEZE,
              ahIGNORE_AUTH,
              j_,
              SpendableHandling::shFULL_BALANCE);
    auto const brokerBalanceAfter = account_ == brokerPayee
        ? STAmount{asset, 0}
        : accountHolds(
              view,
              brokerPayee,
              asset,
              fhIGNORE_FREEZE,
              ahIGNORE_AUTH,
              j_,
              SpendableHandling::shFULL_BALANCE);
    auto const balanceScale = [&]() {
        // Find the maximum exponent of all the non-zero balances, before and after.
        // This is so ugly.
        std::vector<int> exponents;

        for (auto const& a : {
                 accountBalanceBefore,
                 vaultBalanceBefore,
                 brokerBalanceBefore,
                 accountBalanceAfter,
                 vaultBalanceAfter,
                 brokerBalanceAfter,
             })
        {
            // Exclude zeroes
            if (a != beast::zero)
                exponents.push_back(a.exponent());
        }
        auto const [minItr, maxItr] = std::minmax_element(exponents.begin(), exponents.end());
        auto const min = *minItr;
        auto const max = *maxItr;
        JLOG(j_.trace()) << "Min scale: " << min << ", max scale: " << max;
        // IOU rounding can be interesting. We want all the balance checks to agree, but don't want
        // to round to such an extreme that it becomes meaningless.  e.g. Everything rounds to one
        // digit. So add 1 to the max (reducing the number of digits after the decimal point by 1)
        // if the scales are not already all the same.
        return std::min(min == max ? max : max + 1, STAmount::cMaxOffset);
    }();

    auto const accountBalanceBeforeRounded = roundToScale(accountBalanceBefore, balanceScale);
    auto const vaultBalanceBeforeRounded = roundToScale(vaultBalanceBefore, balanceScale);
    auto const brokerBalanceBeforeRounded = roundToScale(brokerBalanceBefore, balanceScale);

    auto const totalBalanceBefore = accountBalanceBefore + vaultBalanceBefore + brokerBalanceBefore;
    auto const totalBalanceBeforeRounded = roundToScale(totalBalanceBefore, balanceScale);

    JLOG(j_.trace()) << "Before: "  //
                     << "account " << Number(accountBalanceBeforeRounded) << " ("
                     << Number(accountBalanceBefore) << ")"
                     << ", vault " << Number(vaultBalanceBeforeRounded) << " ("
                     << Number(vaultBalanceBefore) << ")"
                     << ", broker " << Number(brokerBalanceBeforeRounded) << " ("
                     << Number(brokerBalanceBefore) << ")"
                     << ", total " << Number(totalBalanceBeforeRounded) << " ("
                     << Number(totalBalanceBefore) << ")";

    auto const accountBalanceAfterRounded = roundToScale(accountBalanceAfter, balanceScale);
    auto const vaultBalanceAfterRounded = roundToScale(vaultBalanceAfter, balanceScale);
    auto const brokerBalanceAfterRounded = roundToScale(brokerBalanceAfter, balanceScale);

    auto const totalBalanceAfter = accountBalanceAfter + vaultBalanceAfter + brokerBalanceAfter;
    auto const totalBalanceAfterRounded = roundToScale(totalBalanceAfter, balanceScale);

    JLOG(j_.trace()) << "After: "  //
                     << "account " << Number(accountBalanceAfterRounded) << " ("
                     << Number(accountBalanceAfter) << ")"
                     << ", vault " << Number(vaultBalanceAfterRounded) << " ("
                     << Number(vaultBalanceAfter) << ")"
                     << ", broker " << Number(brokerBalanceAfterRounded) << " ("
                     << Number(brokerBalanceAfter) << ")"
                     << ", total " << Number(totalBalanceAfterRounded) << " ("
                     << Number(totalBalanceAfter) << ")";

    auto const accountBalanceChange = accountBalanceAfter - accountBalanceBefore;
    auto const vaultBalanceChange = vaultBalanceAfter - vaultBalanceBefore;
    auto const brokerBalanceChange = brokerBalanceAfter - brokerBalanceBefore;

    auto const totalBalanceChange = accountBalanceChange + vaultBalanceChange + brokerBalanceChange;
    auto const totalBalanceChangeRounded = roundToScale(totalBalanceChange, balanceScale);

    JLOG(j_.trace()) << "Changes: "                                    //
                     << "account " << to_string(accountBalanceChange)  //
                     << ", vault " << to_string(vaultBalanceChange)    //
                     << ", broker " << to_string(brokerBalanceChange)  //
                     << ", total " << to_string(totalBalanceChangeRounded) << " ("
                     << Number(totalBalanceChange) << ")";

    if (totalBalanceBeforeRounded != totalBalanceAfterRounded)
    {
        JLOG(j_.warn()) << "Total rounded balances don't match"
                        << (totalBalanceChangeRounded == beast::zero ? ", but total changes do"
                                                                     : "");
    }
    if (totalBalanceChangeRounded != beast::zero)
    {
        JLOG(j_.warn()) << "Total balance changes don't match"
                        << (totalBalanceBeforeRounded == totalBalanceAfterRounded
                                ? ", but total balances do"
                                : "");
    }

    // Rounding for IOUs can be weird, so check a few different ways to show
    // that funds are conserved.
    XRPL_ASSERT_PARTS(
        totalBalanceBeforeRounded == totalBalanceAfterRounded ||
            totalBalanceChangeRounded == beast::zero,
        "xrpl::LoanPay::doApply",
        "funds are conserved (with rounding)");

    XRPL_ASSERT_PARTS(
        accountBalanceAfter < accountBalanceBefore || account_ == asset.getIssuer(),
        "xrpl::LoanPay::doApply",
        "account balance decreased");
    XRPL_ASSERT_PARTS(
        vaultBalanceAfter >= beast::zero && brokerBalanceAfter >= beast::zero,
        "xrpl::LoanPay::doApply",
        "positive vault and broker balances");
    XRPL_ASSERT_PARTS(
        vaultBalanceAfter >= vaultBalanceBefore,
        "xrpl::LoanPay::doApply",
        "vault balance did not decrease");
    XRPL_ASSERT_PARTS(
        brokerBalanceAfter >= brokerBalanceBefore,
        "xrpl::LoanPay::doApply",
        "broker balance did not decrease");
    XRPL_ASSERT_PARTS(
        vaultBalanceAfter > vaultBalanceBefore || brokerBalanceAfter > brokerBalanceBefore,
        "xrpl::LoanPay::doApply",
        "vault and/or broker balance increased");

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

}  // namespace xrpl

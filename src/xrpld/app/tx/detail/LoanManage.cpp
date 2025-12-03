#include <xrpld/app/tx/detail/LoanManage.h>
//
#include <xrpld/app/misc/LendingHelpers.h>

#include <xrpl/protocol/TxFlags.h>

namespace ripple {

bool
LoanManage::checkExtraFeatures(PreflightContext const& ctx)
{
    return checkLendingProtocolDependencies(ctx);
}

std::uint32_t
LoanManage::getFlagsMask(PreflightContext const& ctx)
{
    return tfLoanManageMask;
}

NotTEC
LoanManage::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfLoanID] == beast::zero)
        return temINVALID;

    // Flags are mutually exclusive
    if (auto const flagField = ctx.tx[~sfFlags]; flagField && *flagField)
    {
        auto const flags = *flagField & tfUniversalMask;
        if ((flags & (flags - 1)) != 0)
        {
            JLOG(ctx.j.warn())
                << "LoanManage: Only one of tfLoanDefault, tfLoanImpair, or "
                   "tfLoanUnimpair can be set.";
            return temINVALID_FLAG;
        }
    }

    return tesSUCCESS;
}

TER
LoanManage::preclaim(PreclaimContext const& ctx)
{
    auto const& tx = ctx.tx;

    auto const account = tx[sfAccount];
    auto const loanID = tx[sfLoanID];

    auto const loanSle = ctx.view.read(keylet::loan(loanID));
    if (!loanSle)
    {
        JLOG(ctx.j.warn()) << "Loan does not exist.";
        return tecNO_ENTRY;
    }
    // Impairment only allows certain transitions.
    // 1. Once it's in default, it can't be changed.
    // 2. It can get worse: unimpaired -> impaired -> default
    //      or unimpaired -> default
    // 3. It can get better: impaired -> unimpaired
    // 4. If it's in a state, it can't be put in that state again.
    if (loanSle->isFlag(lsfLoanDefault))
    {
        JLOG(ctx.j.warn())
            << "Loan is in default. A defaulted loan can not be modified.";
        return tecNO_PERMISSION;
    }
    if (loanSle->isFlag(lsfLoanImpaired) && tx.isFlag(tfLoanImpair))
    {
        JLOG(ctx.j.warn())
            << "Loan is impaired. A loan can not be impaired twice.";
        return tecNO_PERMISSION;
    }
    if (!(loanSle->isFlag(lsfLoanImpaired) ||
          loanSle->isFlag(lsfLoanDefault)) &&
        (tx.isFlag(tfLoanUnimpair)))
    {
        JLOG(ctx.j.warn())
            << "Loan is unimpaired. Can not be unimpaired again.";
        return tecNO_PERMISSION;
    }
    if (loanSle->at(sfPaymentRemaining) == 0)
    {
        JLOG(ctx.j.warn()) << "Loan is fully paid. A loan can not be modified "
                              "after it is fully paid.";
        return tecNO_PERMISSION;
    }
    if (tx.isFlag(tfLoanDefault) &&
        !hasExpired(
            ctx.view,
            loanSle->at(sfNextPaymentDueDate) + loanSle->at(sfGracePeriod)))
    {
        JLOG(ctx.j.warn())
            << "A loan can not be defaulted before the next payment due date.";
        return tecTOO_SOON;
    }

    auto const loanBrokerID = loanSle->at(sfLoanBrokerID);
    auto const loanBrokerSle = ctx.view.read(keylet::loanbroker(loanBrokerID));
    if (!loanBrokerSle)
    {
        // should be impossible
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }
    if (loanBrokerSle->at(sfOwner) != account)
    {
        JLOG(ctx.j.warn())
            << "LoanBroker for Loan does not belong to the account. LoanModify "
               "can only be submitted by the Loan Broker.";
        return tecNO_PERMISSION;
    }

    return tesSUCCESS;
}

static Number
owedToVault(SLE::ref loanSle)
{
    // Spec section 3.2.3.2, defines the default amount as
    //
    // DefaultAmount = (Loan.PrincipalOutstanding + Loan.InterestOutstanding)
    //
    // Loan.InterestOutstanding is not stored directly on ledger.
    // It is computed as
    //
    // Loan.TotalValueOutstanding - Loan.PrincipalOutstanding -
    //      Loan.ManagementFeeOutstanding
    //
    // Add that to the original formula, and you get this:
    return loanSle->at(sfTotalValueOutstanding) -
        loanSle->at(sfManagementFeeOutstanding);
}

TER
LoanManage::defaultLoan(
    ApplyView& view,
    SLE::ref loanSle,
    SLE::ref brokerSle,
    SLE::ref vaultSle,
    Asset const& vaultAsset,
    beast::Journal j)
{
    // Calculate the amount of the Default that First-Loss Capital covers:

    std::int32_t const loanScale = loanSle->at(sfLoanScale);
    auto brokerDebtTotalProxy = brokerSle->at(sfDebtTotal);

    Number const totalDefaultAmount = owedToVault(loanSle);

    // Apply the First-Loss Capital to the Default Amount
    TenthBips32 const coverRateMinimum{brokerSle->at(sfCoverRateMinimum)};
    TenthBips32 const coverRateLiquidation{
        brokerSle->at(sfCoverRateLiquidation)};
    auto const defaultCovered = [&]() {
        // Always round the minimum required up.
        NumberRoundModeGuard mg(Number::upward);
        auto const minimumCover =
            tenthBipsOfValue(brokerDebtTotalProxy.value(), coverRateMinimum);
        // Round the liquidation amount up, too
        return roundToAsset(
            vaultAsset,
            /*
             * This formula is from the XLS-66 spec, section 3.2.3.2 (State
             * Changes), specifically "if the `tfLoanDefault` flag is set" /
             * "Apply the First-Loss Capital to the Default Amount"
             */
            std::min(
                tenthBipsOfValue(minimumCover, coverRateLiquidation),
                totalDefaultAmount),
            loanScale);
    }();

    auto const vaultDefaultAmount = totalDefaultAmount - defaultCovered;

    // Update the Vault object:

    // The vault may be at a different scale than the loan. Reduce rounding
    // errors during the accounting by rounding some of the values to that
    // scale.
    auto const vaultScale = getVaultScale(vaultSle);

    {
        // Decrease the Total Value of the Vault:
        auto vaultTotalProxy = vaultSle->at(sfAssetsTotal);
        auto vaultAvailableProxy = vaultSle->at(sfAssetsAvailable);

        if (vaultTotalProxy < vaultDefaultAmount)
        {
            // LCOV_EXCL_START
            JLOG(j.warn())
                << "Vault total assets is less than the vault default amount";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }

        auto const vaultDefaultRounded = roundToAsset(
            vaultAsset, vaultDefaultAmount, vaultScale, Number::downward);
        vaultTotalProxy -= vaultDefaultRounded;
        // Increase the Asset Available of the Vault by liquidated First-Loss
        // Capital and any unclaimed funds amount:
        vaultAvailableProxy += defaultCovered;
        if (*vaultAvailableProxy > *vaultTotalProxy && !vaultAsset.integral())
        {
            auto const difference = vaultAvailableProxy - vaultTotalProxy;
            JLOG(j.debug())
                << "Vault assets available: " << *vaultAvailableProxy << "("
                << vaultAvailableProxy.value().exponent()
                << "), Total: " << *vaultTotalProxy << "("
                << vaultTotalProxy.value().exponent()
                << "), Difference: " << difference << "("
                << difference.exponent() << ")";
            if (vaultAvailableProxy.value().exponent() - difference.exponent() >
                13)
            {
                // If the difference is dust, bring the total up to match
                // the available
                JLOG(j.debug())
                    << "Difference between vault assets available and total is "
                       "dust. Set both to the larger value.";
                vaultTotalProxy = vaultAvailableProxy;
            }
        }
        if (*vaultAvailableProxy > *vaultTotalProxy)
        {
            JLOG(j.warn()) << "Vault assets available must not be greater "
                              "than assets outstanding. Available: "
                           << *vaultAvailableProxy
                           << ", Total: " << *vaultTotalProxy;
            return tecLIMIT_EXCEEDED;
        }

        // The loss has been realized
        if (loanSle->isFlag(lsfLoanImpaired))
        {
            auto vaultLossUnrealizedProxy = vaultSle->at(sfLossUnrealized);
            if (vaultLossUnrealizedProxy < totalDefaultAmount)
            {
                // LCOV_EXCL_START
                JLOG(j.warn())
                    << "Vault unrealized loss is less than the default amount";
                return tefBAD_LEDGER;
                // LCOV_EXCL_STOP
            }
            vaultLossUnrealizedProxy -= totalDefaultAmount;
        }
        view.update(vaultSle);
    }

    // Update the LoanBroker object:

    {
        auto const asset = *vaultSle->at(sfAsset);

        // Decrease the Debt of the LoanBroker:
        adjustImpreciseNumber(
            brokerDebtTotalProxy, -totalDefaultAmount, asset, vaultScale);
        // Decrease the First-Loss Capital Cover Available:
        auto coverAvailableProxy = brokerSle->at(sfCoverAvailable);
        if (coverAvailableProxy < defaultCovered)
        {
            // LCOV_EXCL_START
            JLOG(j.warn())
                << "LoanBroker cover available is less than amount covered";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }
        coverAvailableProxy -= defaultCovered;
        view.update(brokerSle);
    }

    // Update the Loan object:
    loanSle->setFlag(lsfLoanDefault);

    loanSle->at(sfTotalValueOutstanding) = 0;
    loanSle->at(sfPaymentRemaining) = 0;
    loanSle->at(sfPrincipalOutstanding) = 0;
    loanSle->at(sfManagementFeeOutstanding) = 0;
    // Zero out the next due date. Since it's default, it'll be removed from
    // the object.
    loanSle->at(sfNextPaymentDueDate) = 0;
    view.update(loanSle);

    // Return funds from the LoanBroker pseudo-account to the
    // Vault pseudo-account:
    return accountSend(
        view,
        brokerSle->at(sfAccount),
        vaultSle->at(sfAccount),
        STAmount{vaultAsset, defaultCovered},
        j,
        WaiveTransferFee::Yes);
}

TER
LoanManage::impairLoan(
    ApplyView& view,
    SLE::ref loanSle,
    SLE::ref vaultSle,
    beast::Journal j)
{
    Number const lossUnrealized = owedToVault(loanSle);

    // Update the Vault object(set "paper loss")
    auto vaultLossUnrealizedProxy = vaultSle->at(sfLossUnrealized);
    vaultLossUnrealizedProxy += lossUnrealized;
    if (vaultLossUnrealizedProxy >
        vaultSle->at(sfAssetsTotal) - vaultSle->at(sfAssetsAvailable))
    {
        // Having a loss greater than the vault's unavailable assets
        // will leave the vault in an invalid / inconsistent state.
        JLOG(j.warn()) << "Vault unrealized loss is too large, and will "
                          "corrupt the vault.";
        return tecLIMIT_EXCEEDED;
    }
    view.update(vaultSle);

    // Update the Loan object
    loanSle->setFlag(lsfLoanImpaired);
    auto loanNextDueProxy = loanSle->at(sfNextPaymentDueDate);
    if (!hasExpired(view, loanNextDueProxy))
    {
        // loan payment is not yet late -
        // move the next payment due date to now
        loanNextDueProxy = view.parentCloseTime().time_since_epoch().count();
    }
    view.update(loanSle);

    return tesSUCCESS;
}

TER
LoanManage::unimpairLoan(
    ApplyView& view,
    SLE::ref loanSle,
    SLE::ref vaultSle,
    beast::Journal j)
{
    // Update the Vault object(clear "paper loss")
    auto vaultLossUnrealizedProxy = vaultSle->at(sfLossUnrealized);
    Number const lossReversed = owedToVault(loanSle);
    if (vaultLossUnrealizedProxy < lossReversed)
    {
        // LCOV_EXCL_START
        JLOG(j.warn())
            << "Vault unrealized loss is less than the amount to be cleared";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }
    vaultLossUnrealizedProxy -= lossReversed;
    view.update(vaultSle);

    // Update the Loan object
    loanSle->clearFlag(lsfLoanImpaired);
    auto const paymentInterval = loanSle->at(sfPaymentInterval);
    auto const normalPaymentDueDate =
        std::max(loanSle->at(sfPreviousPaymentDate), loanSle->at(sfStartDate)) +
        paymentInterval;
    if (!hasExpired(view, normalPaymentDueDate))
    {
        // loan was unimpaired within the payment interval
        loanSle->at(sfNextPaymentDueDate) = normalPaymentDueDate;
    }
    else
    {
        // loan was unimpaired after the original payment due date
        loanSle->at(sfNextPaymentDueDate) =
            view.parentCloseTime().time_since_epoch().count() + paymentInterval;
    }
    view.update(loanSle);

    return tesSUCCESS;
}

TER
LoanManage::doApply()
{
    auto const& tx = ctx_.tx;
    auto& view = ctx_.view();

    auto const loanID = tx[sfLoanID];
    auto const loanSle = view.peek(keylet::loan(loanID));
    if (!loanSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    auto const brokerID = loanSle->at(sfLoanBrokerID);
    auto const brokerSle = view.peek(keylet::loanbroker(brokerID));
    if (!brokerSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    auto const vaultSle = view.peek(keylet ::vault(brokerSle->at(sfVaultID)));
    if (!vaultSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    auto const vaultAsset = vaultSle->at(sfAsset);

    // Valid flag combinations are checked in preflight. No flags is valid -
    // just a noop.
    if (tx.isFlag(tfLoanDefault))
    {
        if (auto const ter =
                defaultLoan(view, loanSle, brokerSle, vaultSle, vaultAsset, j_))
            return ter;
    }
    else if (tx.isFlag(tfLoanImpair))
    {
        if (auto const ter = impairLoan(view, loanSle, vaultSle, j_))
            return ter;
    }
    else if (tx.isFlag(tfLoanUnimpair))
    {
        if (auto const ter = unimpairLoan(view, loanSle, vaultSle, j_))
            return ter;
    }

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

}  // namespace ripple

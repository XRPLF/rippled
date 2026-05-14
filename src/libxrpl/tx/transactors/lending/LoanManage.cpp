/** @file LoanManage.cpp
 *  Implements the `LoanManage` transactor (ttLOAN_MANAGE, XLS-66).
 *
 *  Manages the credit-quality lifecycle of a loan — impairment, impairment
 *  reversal, and formal default — on behalf of the `LoanBroker` that issued
 *  the loan.  Multi-object accounting across the `Loan`, `LoanBroker`, and
 *  `Vault` ledger entries is the core concern.  Payment processing lives in
 *  `LoanPay`; loan creation lives in `LoanSet`.
 */
#include <xrpl/tx/transactors/lending/LoanManage.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTakesAsset.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <algorithm>
#include <cstdint>
#include <memory>

namespace xrpl {

bool
LoanManage::checkExtraFeatures(PreflightContext const& ctx)
{
    return checkLendingProtocolDependencies(ctx.rules, ctx.tx);
}

std::uint32_t
LoanManage::getFlagsMask(PreflightContext const& ctx)
{
    return tfLoanManageMask;
}

NotTEC
LoanManage::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfLoanID] == beast::kZERO)
        return temINVALID;

    // Flags are mutually exclusive
    if (auto const flagField = ctx.tx[~sfFlags]; flagField && (*flagField != 0u))
    {
        auto const flags = *flagField & tfUniversalMask;
        if ((flags & (flags - 1)) != 0)
        {
            JLOG(ctx.j.warn()) << "LoanManage: Only one of tfLoanDefault, tfLoanImpair, or "
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
    // State-machine transitions: normal→impaired, normal→default,
    // impaired→normal, impaired→default. Default is terminal; re-impair and
    // un-impair-of-normal are blocked.
    if (loanSle->isFlag(lsfLoanDefault))
    {
        JLOG(ctx.j.warn()) << "Loan is in default. A defaulted loan can not be modified.";
        return tecNO_PERMISSION;
    }
    if (loanSle->isFlag(lsfLoanImpaired) && tx.isFlag(tfLoanImpair))
    {
        JLOG(ctx.j.warn()) << "Loan is impaired. A loan can not be impaired twice.";
        return tecNO_PERMISSION;
    }
    if (!(loanSle->isFlag(lsfLoanImpaired) || loanSle->isFlag(lsfLoanDefault)) &&
        (tx.isFlag(tfLoanUnimpair)))
    {
        JLOG(ctx.j.warn()) << "Loan is unimpaired. Can not be unimpaired again.";
        return tecNO_PERMISSION;
    }
    if (loanSle->at(sfPaymentRemaining) == 0)
    {
        JLOG(ctx.j.warn()) << "Loan is fully paid. A loan can not be modified "
                              "after it is fully paid.";
        return tecNO_PERMISSION;
    }
    if (tx.isFlag(tfLoanDefault) &&
        !hasExpired(ctx.view, loanSle->at(sfNextPaymentDueDate) + loanSle->at(sfGracePeriod)))
    {
        JLOG(ctx.j.warn()) << "A loan can not be defaulted before the next payment due date.";
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
        JLOG(ctx.j.warn()) << "LoanBroker for Loan does not belong to the account. LoanManage "
                              "can only be submitted by the Loan Broker.";
        return tecNO_PERMISSION;
    }

    return tesSUCCESS;
}

/** Compute the amount the vault is owed by a loan (XLS-66 §3.2.3.2).
 *
 *  The vault is owed principal plus its share of interest; management fees
 *  accrue entirely to the broker.  `sfInterestOutstanding` is not stored
 *  directly, so it is derived:
 *
 *  ```
 *  InterestOutstanding = TotalValueOutstanding
 *                        - PrincipalOutstanding
 *                        - ManagementFeeOutstanding
 *
 *  owedToVault = PrincipalOutstanding + InterestOutstanding
 *              = TotalValueOutstanding - ManagementFeeOutstanding
 *  ```
 *
 *  This identity drives both the impairment paper-loss and the default
 *  settlement amount.
 *
 *  @param loanSle  Read-only SLE for the Loan object.
 *  @return         The amount the vault is owed, expressed as a `Number`.
 */
static Number
owedToVault(SLE::ref loanSle)
{
    return loanSle->at(sfTotalValueOutstanding) - loanSle->at(sfManagementFeeOutstanding);
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
    std::int32_t const loanScale = loanSle->at(sfLoanScale);
    auto brokerDebtTotalProxy = brokerSle->at(sfDebtTotal);

    Number const totalDefaultAmount = owedToVault(loanSle);

    TenthBips32 const coverRateMinimum{brokerSle->at(sfCoverRateMinimum)};
    TenthBips32 const coverRateLiquidation{brokerSle->at(sfCoverRateLiquidation)};
    auto const defaultCovered = [&]() {
        // Always round upward — the broker must not under-cover the vault.
        NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
        auto const minimumCover = tenthBipsOfValue(brokerDebtTotalProxy.value(), coverRateMinimum);
        // Round the liquidation amount up, too
        auto const covered = roundToAsset(
            vaultAsset,
            /*
             * This formula is from the XLS-66 spec, section 3.2.3.2 (State
             * Changes), specifically "if the `tfLoanDefault` flag is set" /
             * "Apply the First-Loss Capital to the Default Amount"
             */
            std::min(tenthBipsOfValue(minimumCover, coverRateLiquidation), totalDefaultAmount),
            loanScale);
        auto const coverAvailable = *brokerSle->at(sfCoverAvailable);

        return std::min(covered, coverAvailable);
    }();

    auto const vaultDefaultAmount = totalDefaultAmount - defaultCovered;

    // The vault may be at a different scale than the loan. Reduce rounding
    // errors during the accounting by rounding some of the values to that
    // scale.
    auto const vaultScale = getAssetsTotalScale(vaultSle);

    {
        // Decrease the Total Value of the Vault:
        auto vaultTotalProxy = vaultSle->at(sfAssetsTotal);
        auto vaultAvailableProxy = vaultSle->at(sfAssetsAvailable);

        if (vaultTotalProxy < vaultDefaultAmount)
        {
            // LCOV_EXCL_START
            JLOG(j.warn()) << "Vault total assets is less than the vault default amount";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }

        auto const vaultDefaultRounded = roundToAsset(
            vaultAsset, vaultDefaultAmount, vaultScale, Number::RoundingMode::Downward);
        vaultTotalProxy -= vaultDefaultRounded;
        // Add back the first-loss capital recovered from the broker.
        vaultAvailableProxy += defaultCovered;
        if (*vaultAvailableProxy > *vaultTotalProxy && !vaultAsset.integral())
        {
            auto const difference = vaultAvailableProxy - vaultTotalProxy;
            JLOG(j.debug()) << "Vault assets available: " << *vaultAvailableProxy << "("
                            << vaultAvailableProxy.value().exponent()
                            << "), Total: " << *vaultTotalProxy << "("
                            << vaultTotalProxy.value().exponent() << "), Difference: " << difference
                            << "(" << difference.exponent() << ")";
            if (vaultAvailableProxy.value().exponent() - difference.exponent() > 13)
            {
                // If the difference is dust, bring the total up to match
                // the available
                JLOG(j.debug()) << "Difference between vault assets available and total is "
                                   "dust. Set both to the larger value.";
                vaultTotalProxy = vaultAvailableProxy;
            }
        }
        if (*vaultAvailableProxy > *vaultTotalProxy)
        {
            // LCOV_EXCL_START
            JLOG(j.fatal()) << "Vault assets available must not be greater "
                               "than assets outstanding. Available: "
                            << *vaultAvailableProxy << ", Total: " << *vaultTotalProxy;
            return tecINTERNAL;
            // LCOV_EXCL_STOP
        }

        // If previously impaired, convert the recorded paper loss to realized.
        if (loanSle->isFlag(lsfLoanImpaired))
        {
            auto vaultLossUnrealizedProxy = vaultSle->at(sfLossUnrealized);
            if (vaultLossUnrealizedProxy < totalDefaultAmount)
            {
                // LCOV_EXCL_START
                JLOG(j.warn()) << "Vault unrealized loss is less than the default amount";
                return tefBAD_LEDGER;
                // LCOV_EXCL_STOP
            }
            adjustImpreciseNumber(
                vaultLossUnrealizedProxy, -totalDefaultAmount, vaultAsset, vaultScale);
        }
        view.update(vaultSle);
    }

    {
        adjustImpreciseNumber(brokerDebtTotalProxy, -totalDefaultAmount, vaultAsset, vaultScale);
        auto coverAvailableProxy = brokerSle->at(sfCoverAvailable);
        if (coverAvailableProxy < defaultCovered)
        {
            // LCOV_EXCL_START
            JLOG(j.warn()) << "LoanBroker cover available is less than amount covered";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }
        coverAvailableProxy -= defaultCovered;
        view.update(brokerSle);
    }

    loanSle->setFlag(lsfLoanDefault);

    loanSle->at(sfTotalValueOutstanding) = 0;
    loanSle->at(sfPaymentRemaining) = 0;
    loanSle->at(sfPrincipalOutstanding) = 0;
    loanSle->at(sfManagementFeeOutstanding) = 0;
    // Zero the due date — a defaulted loan has no future payment schedule;
    // a zero value causes the field to be removed from the serialised object.
    loanSle->at(sfNextPaymentDueDate) = 0;
    view.update(loanSle);

    // Transfer the covered amount from the broker pseudo-account to the
    // vault pseudo-account. WaiveTransferFee::Yes is required because neither
    // account is a regular user account.
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
    Asset const& vaultAsset,
    beast::Journal j)
{
    Number const lossUnrealized = owedToVault(loanSle);

    // The vault may be at a different scale than the loan. Reduce rounding
    // errors during the accounting by rounding values to the vault's scale.
    auto const vaultScale = getAssetsTotalScale(vaultSle);

    auto vaultLossUnrealizedProxy = vaultSle->at(sfLossUnrealized);
    adjustImpreciseNumber(vaultLossUnrealizedProxy, lossUnrealized, vaultAsset, vaultScale);
    if (vaultLossUnrealizedProxy > vaultSle->at(sfAssetsTotal) - vaultSle->at(sfAssetsAvailable))
    {
        // Having a loss greater than the vault's unavailable assets
        // will leave the vault in an invalid / inconsistent state.
        JLOG(j.warn()) << "Vault unrealized loss is too large, and will "
                          "corrupt the vault.";
        return tecLIMIT_EXCEEDED;
    }
    view.update(vaultSle);

    loanSle->setFlag(lsfLoanImpaired);
    auto loanNextDueProxy = loanSle->at(sfNextPaymentDueDate);
    if (!hasExpired(view, loanNextDueProxy))
    {
        // Payment is not yet overdue — accelerate the due date to now so the
        // grace period clock starts immediately rather than from the original
        // scheduled date.
        loanNextDueProxy = view.parentCloseTime().time_since_epoch().count();
    }
    view.update(loanSle);

    return tesSUCCESS;
}

[[nodiscard]] TER
LoanManage::unimpairLoan(
    ApplyView& view,
    SLE::ref loanSle,
    SLE::ref vaultSle,
    Asset const& vaultAsset,
    beast::Journal j)
{
    // The vault may be at a different scale than the loan. Reduce rounding
    // errors during the accounting by rounding values to the vault's scale.
    auto const vaultScale = getAssetsTotalScale(vaultSle);

    auto vaultLossUnrealizedProxy = vaultSle->at(sfLossUnrealized);
    Number const lossReversed = owedToVault(loanSle);
    if (vaultLossUnrealizedProxy < lossReversed)
    {
        // LCOV_EXCL_START
        JLOG(j.warn()) << "Vault unrealized loss is less than the amount to be cleared";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }
    adjustImpreciseNumber(vaultLossUnrealizedProxy, -lossReversed, vaultAsset, vaultScale);

    view.update(vaultSle);

    loanSle->clearFlag(lsfLoanImpaired);
    auto const paymentInterval = loanSle->at(sfPaymentInterval);
    auto const normalPaymentDueDate =
        std::max(loanSle->at(sfPreviousPaymentDueDate), loanSle->at(sfStartDate)) + paymentInterval;
    if (!hasExpired(view, normalPaymentDueDate))
    {
        // Original due date is still in the future — restore the amortization
        // schedule unchanged so the borrower loses no payment window.
        loanSle->at(sfNextPaymentDueDate) = normalPaymentDueDate;
    }
    else
    {
        // Original due date has passed — give the borrower a fresh interval
        // from now rather than leaving the loan immediately overdue.
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

    auto const vaultSle = view.peek(keylet::vault(brokerSle->at(sfVaultID)));
    if (!vaultSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    auto const vaultAsset = vaultSle->at(sfAsset);

    auto const result = [&]() -> TER {
        // Mutual exclusivity of flags is enforced in preflight. No flags is a
        // valid no-op form (e.g., used to trigger associateAsset post-amendment).
        if (tx.isFlag(tfLoanDefault))
            return defaultLoan(view, loanSle, brokerSle, vaultSle, vaultAsset, j_);
        if (tx.isFlag(tfLoanImpair))
            return impairLoan(view, loanSle, vaultSle, vaultAsset, j_);
        if (tx.isFlag(tfLoanUnimpair))
            return unimpairLoan(view, loanSle, vaultSle, vaultAsset, j_);
        return tesSUCCESS;
    }();

    // Pre-amendment, associateAsset was only called on the noop (no flags)
    // path. Post-amendment, we call associateAsset on all successful paths.
    if (view.rules().enabled(fixSecurity3_1_3) && isTesSuccess(result))
    {
        associateAsset(*loanSle, vaultAsset);
        associateAsset(*brokerSle, vaultAsset);
        associateAsset(*vaultSle, vaultAsset);
    }

    return result;
}

void
LoanManage::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
LoanManage::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

//------------------------------------------------------------------------------

}  // namespace xrpl

#include <xrpl/tx/transactors/lending/LoanManage.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
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
    if (ctx.tx[sfLoanID] == beast::kZero)
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
    // Impairment only allows certain transitions.
    // 1. Once it's in default, it can't be changed.
    // 2. It can get worse: unimpaired -> impaired -> default
    //      or unimpaired -> default
    // 3. It can get better: impaired -> unimpaired
    // 4. If it's in a state, it can't be put in that state again.
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
        !hasExpired(
            ctx.view,
            loanSle->at(sfNextPaymentDueDate) + loanSle->at(sfGracePeriod),
            ctx.view.rules().enabled(fixCleanup3_4_0) ? ExpiryComparison::Exclusive
                                                      : ExpiryComparison::Inclusive))
    {
        JLOG(ctx.j.warn()) << "A loan can not be defaulted before the next payment due date.";
        return tecTOO_SOON;
    }

    auto const loanBrokerID = loanSle->at(sfLoanBrokerID);
    auto const loanBrokerSle = ctx.view.read(keylet::loanBroker(loanBrokerID));
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

    Number const totalDefaultAmount = loanVaultExposure(vaultSle, loanSle);

    // Apply the First-Loss Capital to the Default Amount
    TenthBips32 const coverRateMinimum{brokerSle->at(sfCoverRateMinimum)};
    TenthBips32 const coverRateLiquidation{brokerSle->at(sfCoverRateLiquidation)};
    auto const defaultCovered = [&]() {
        // Always round the minimum required up.
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

    // Update the Vault object:

    // The vault may be at a different scale than the loan. Reduce rounding
    // errors during the accounting by rounding some of the values to that
    // scale.
    auto const vaultScale = getAssetsTotalScale(vaultSle);

    // Under fixCleanup3_4_0, both vault-side fields are mutated through a
    // single asset-typed STAmount pair. `amount = STAmount{vaultAsset,
    // defaultCovered}` is applied to both sfAssetsTotal and
    // sfAssetsAvailable, and `writeOff = STAmount{vaultAsset,
    // totalDefaultAmount}` is applied to sfAssetsTotal only. Because the
    // shared `amount` is normalized to STAmount precision exactly once and
    // absorbed by both fields symmetrically, sfAssetsAvailable cannot
    // overshoot sfAssetsTotal from arithmetic alone -- the residual is
    // exactly `-writeOff` on Total. Pre-amendment, the two fields were
    // adjusted via values obtained through different Number->STAmount
    // paths (vaultDefaultAmount rounded down to vaultScale on one side,
    // defaultCovered kept at the finer loan scale on the other), so the
    // normalization was asymmetric and a dust-reconciliation snap was
    // required to paper over the resulting cross-side mismatch -- a snap
    // that itself minted phantom assets on sfAssetsTotal. See the
    // sibling change in LoanPay for the same fix on the payment path.
    //
    // The Number-precision guard `T - A >= totalDefaultAmount` below is
    // exactly sufficient: loanVaultExposure returns a difference of
    // STNumber fields that are whole multiples of the same immutable
    // `10^sfLoanScale` and never outgrow its 16 digits, so its STAmount
    // promotion round-trips losslessly with no writeOff slack.
    bool const useUnifiedAssetArithmetic = view.rules().enabled(fixCleanup3_4_0);

    if (useUnifiedAssetArithmetic)
    {
        // Prior to realizing the default, the vault must already carry
        // at least this loan's exposure. A violation here is a corrupt
        // ledger, not a transaction-input error.
        //
        // This guard is strictly stronger than the pre-amendment
        // `vaultTotalBefore < vaultDefaultAmount` form, but activation is
        // safe: the ValidVault invariant continuously enforces Total >=
        // Available (so Total - Available >= 0 on every ledger), and the
        // pre-amendment cross-scale rounding could only inflate the
        // difference Total - Available, never deflate it. Any ledger
        // reaching this point post-activation will therefore already
        // satisfy the stronger form.
        Number const vaultTotalBefore = *vaultSle->at(sfAssetsTotal);
        Number const vaultAvailableBefore = *vaultSle->at(sfAssetsAvailable);
        if (vaultTotalBefore - vaultAvailableBefore < totalDefaultAmount)
        {
            // LCOV_EXCL_START
            JLOG(j.warn()) << "Vault exposure is less than the loan default amount";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }

        // Mutate the two vault fields through a single asset-typed pair
        // (see the block comment on useUnifiedAssetArithmetic above for
        // why this is safe):
        //   (1) write off totalDefaultAmount from Total only;
        //   (2) add defaultCovered symmetrically to both fields.
        STAmount const amount{vaultAsset, defaultCovered};
        STAmount const writeOff{vaultAsset, totalDefaultAmount};

        vaultSle->at(sfAssetsTotal) += amount - writeOff;
        vaultSle->at(sfAssetsAvailable) += amount;

        XRPL_ASSERT_PARTS(
            *vaultSle->at(sfAssetsAvailable) <= *vaultSle->at(sfAssetsTotal),
            "xrpl::LoanManage::defaultLoan",
            "assets available must not be greater than assets outstanding");
    }
    else
    {
        // Pre-amendment behavior.
        auto const vaultDefaultAmount = totalDefaultAmount - defaultCovered;

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
        // Increase the Asset Available of the Vault by liquidated
        // First-Loss Capital and any unclaimed funds amount:
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
                // If the difference is dust, bring the total up to
                // match the available
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
    }

    // The loss has been realized
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

    // Update the LoanBroker object:

    {
        // Decrease the Debt of the LoanBroker:
        adjustImpreciseNumber(brokerDebtTotalProxy, -totalDefaultAmount, vaultAsset, vaultScale);
        // Decrease the First-Loss Capital Cover Available:
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
        {},
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
    bool const fixEnabled340 = view.rules().enabled(fixCleanup3_4_0);

    if (fixEnabled340 && !isPaymentLate(view, loanSle))
    {
        JLOG(j.warn()) << "Cannot impair a loan that is not late";
        return tecTOO_SOON;
    }

    Number const lossUnrealized = loanVaultExposure(vaultSle, loanSle);

    // The vault may be at a different scale than the loan. Reduce rounding
    // errors during the accounting by rounding some of the values to that
    // scale.
    auto const vaultScale = getAssetsTotalScale(vaultSle);

    // Update the Vault object(set "paper loss")
    auto vaultLossUnrealizedProxy = vaultSle->at(sfLossUnrealized);
    adjustImpreciseNumber(vaultLossUnrealizedProxy, lossUnrealized, vaultAsset, vaultScale);
    if (vaultLossUnrealizedProxy > vaultSle->at(sfAssetsTotal) - vaultSle->at(sfAssetsAvailable))
    {
        // Having a loss greater than the vault's unavailable assets
        // will leave the vault in an invalid / inconsistent state.
        JLOG(j.warn()) << "Vault unrealized loss is too large, and will corrupt the vault.";
        return tecLIMIT_EXCEEDED;
    }
    view.update(vaultSle);

    // Update the Loan object
    loanSle->setFlag(lsfLoanImpaired);

    if (!fixEnabled340)
    {
        auto loanNextDueProxy = loanSle->at(sfNextPaymentDueDate);
        if (!isPaymentLate(view, loanSle))
        {
            // loan payment is not yet late move the next payment due date to now
            loanNextDueProxy = view.parentCloseTime().time_since_epoch().count();
        }
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
    // errors during the accounting by rounding some of the values to that
    // scale.
    auto const vaultScale = getAssetsTotalScale(vaultSle);

    // Update the Vault object(clear "paper loss")
    auto vaultLossUnrealizedProxy = vaultSle->at(sfLossUnrealized);
    Number const lossReversed = loanVaultExposure(vaultSle, loanSle);
    if (vaultLossUnrealizedProxy < lossReversed)
    {
        // LCOV_EXCL_START
        JLOG(j.warn()) << "Vault unrealized loss is less than the amount to be cleared";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }
    // Reverse the "paper loss"
    adjustImpreciseNumber(vaultLossUnrealizedProxy, -lossReversed, vaultAsset, vaultScale);

    view.update(vaultSle);

    // Update the Loan object
    loanSle->clearFlag(lsfLoanImpaired);
    if (!view.rules().enabled(fixCleanup3_4_0))
    {
        auto const paymentInterval = loanSle->at(sfPaymentInterval);
        auto const normalPaymentDueDate =
            std::max(loanSle->at(sfPreviousPaymentDueDate), loanSle->at(sfStartDate)) +
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
    auto const brokerSle = view.peek(keylet::loanBroker(brokerID));
    if (!brokerSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    auto const vaultSle = view.peek(keylet::vault(brokerSle->at(sfVaultID)));
    if (!vaultSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    auto const vaultAsset = vaultSle->at(sfAsset);

    auto const result = [&]() -> TER {
        // Valid flag combinations are checked in preflight. No flags is valid -
        // just a noop.
        if (tx.isFlag(tfLoanDefault))
            return defaultLoan(view, loanSle, brokerSle, vaultSle, vaultAsset, j_);
        if (tx.isFlag(tfLoanImpair))
            return impairLoan(view, loanSle, vaultSle, vaultAsset, j_);
        if (tx.isFlag(tfLoanUnimpair))
            return unimpairLoan(view, loanSle, vaultSle, vaultAsset, j_);
        // NoOp, as described above.
        return tesSUCCESS;
    }();

    // Pre-amendment, associateAsset was only called on the noop (no flags)
    // path. Post-amendment, we call associateAsset on all successful paths.
    if (view.rules().enabled(fixCleanup3_1_3) && isTesSuccess(result))
    {
        associateAsset(*loanSle, vaultAsset);
        associateAsset(*brokerSle, vaultAsset);
        associateAsset(*vaultSle, vaultAsset);
    }

    return result;
}

void
LoanManage::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
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

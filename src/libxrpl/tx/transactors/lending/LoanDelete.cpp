#include <xrpl/tx/transactors/lending/LoanDelete.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>  // IWYU pragma: keep
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>  // IWYU pragma: keep
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTakesAsset.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

namespace {

/**
 * Reverse the bookkeeping that LoanSet recorded when the pending loan was
 * proposed, and release the owner reserve charged to the LoanBroker owner.
 * A pending loan was never linked into the borrower's directory and the
 * borrower was never charged a reserve.
 */
TER
reversePendingLoan(
    ApplyView& view,
    SLE::const_ref loanSle,
    SLE::ref brokerSle,
    SLE::ref vaultSle,
    beast::Journal const& j)
{
    auto const vaultAsset = vaultSle->at(sfAsset);

    auto const brokerOwner = brokerSle->at(sfOwner);
    auto const brokerOwnerSle = view.peek(keylet::account(brokerOwner));
    if (!brokerOwnerSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    auto const vaultScale = getAssetsTotalScale(vaultSle);
    Number const principalOutstanding = loanSle->at(sfPrincipalOutstanding);
    auto const state = constructLoanState(loanSle);

    // Reverse exactly the accounting the proposal recognised: dispatch through
    // loanOriginationDeltas so cash-basis vaults (which never accrued the
    // interest at proposal time) do not have a phantom interestDue subtracted
    // here.
    auto const [assetsTotalDelta, debtTotalDelta] =
        loanOriginationDeltas(vaultSle, principalOutstanding, state.interestDue);

    // Reverse the vault bookkeeping from the proposal.
    vaultSle->at(sfAssetsAvailable) += principalOutstanding;
    vaultSle->at(sfAssetsReserved) -= principalOutstanding;
    vaultSle->at(sfAssetsTotal) -= assetsTotalDelta;
    view.update(vaultSle);

    // Reverse the broker debt.
    adjustImpreciseNumber(brokerSle->at(sfDebtTotal), -debtTotalDelta, vaultAsset, vaultScale);

    // Release the reserve from the Loan Broker: Decrement
    // AccountRoot(LoanBroker.Owner).OwnerCount by 1.
    decreaseOwnerCount(view, brokerOwnerSle, {}, 1, j);

    return tesSUCCESS;
}

/**
 * Unlink an active (accepted) loan from the borrower and release the
 * borrower's owner reserve.
 */
TER
releaseBorrower(ApplyView& view, SLE::ref loanSle, beast::Journal const& j)
{
    auto const borrower = loanSle->at(sfBorrower);
    auto const borrowerSle = view.peek(keylet::account(borrower));
    if (!borrowerSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    // Remove LoanID from Directory of the Borrower.
    if (!view.dirRemove(
            keylet::ownerDir(borrower), loanSle->at(sfOwnerNode), loanSle->key(), false))
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    // Decrement the borrower's owner count
    decreaseOwnerCountForObject(view, borrowerSle, loanSle, 1, j);

    return tesSUCCESS;
}

/**
 * Release the loan from the LoanBroker: decrement its owner count and, if this
 * was the broker's last loan, forgive whatever debt is left.
 *
 * Rounding at vault scale can leave dust in DebtTotal when a broker has had
 * loans with different scales, and once no loans remain there is no other way
 * to pay it back. This applies to the pending and active paths alike: if an
 * active loan is deleted while a pending one is still outstanding, the dust
 * survives that deletion, so the pending loan's deletion has to clear it, or
 * LoanBrokerDelete will refuse the broker.
 */
void
releaseLoanFromBroker(
    ApplyView& view,
    SLE::ref brokerSle,
    SLE::const_ref vaultSle,
    beast::Journal const& j)
{
    adjustLoanBrokerOwnerCount(view, brokerSle, -1, j);

    if (brokerSle->at(sfOwnerCount) != 0)
        return;

    auto debtTotalProxy = brokerSle->at(sfDebtTotal);
    if (*debtTotalProxy == beast::kZero)
        return;

    XRPL_ASSERT_PARTS(
        roundToAsset(
            vaultSle->at(sfAsset),
            debtTotalProxy,
            getAssetsTotalScale(vaultSle),
            Number::RoundingMode::TowardsZero) == beast::kZero,
        "xrpl::LoanDelete::releaseLoanFromBroker",
        "last loan, remaining debt rounds to zero");
    JLOG(j.debug()) << "LoanDelete: forgiving residual DebtTotal " << *debtTotalProxy
                    << " on last loan of LoanBroker " << to_string(brokerSle->key());
    debtTotalProxy = 0;
    view.update(brokerSle);
}

}  // namespace

bool
LoanDelete::checkExtraFeatures(PreflightContext const& ctx)
{
    return checkLendingProtocolDependencies(ctx.rules, ctx.tx);
}

NotTEC
LoanDelete::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfLoanID] == beast::kZero)
        return temINVALID;

    return tesSUCCESS;
}

TER
LoanDelete::preclaim(PreclaimContext const& ctx)
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
    // A pending loan (created in the two-step flow) can be deleted at any time
    // by either the LoanBroker owner or the Borrower, regardless of remaining
    // payments. An active loan can only be deleted once it is fully paid.
    if (!isLoanPending(loanSle) && loanSle->at(sfPaymentRemaining) > 0)
    {
        JLOG(ctx.j.warn()) << "Active loan can not be deleted.";
        return tecHAS_OBLIGATIONS;
    }

    auto const loanBrokerID = loanSle->at(sfLoanBrokerID);
    auto const loanBrokerSle = ctx.view.read(keylet::loanBroker(loanBrokerID));
    if (!loanBrokerSle)
    {
        // should be impossible
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }
    if (loanBrokerSle->at(sfOwner) != account && loanSle->at(sfBorrower) != account)
    {
        JLOG(ctx.j.warn()) << "Account is not Loan Broker Owner or Loan Borrower.";
        return tecNO_PERMISSION;
    }

    return tesSUCCESS;
}

TER
LoanDelete::doApply()
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

    auto const brokerPseudoAccount = brokerSle->at(sfAccount);
    Asset const vaultAsset = vaultSle->at(sfAsset);

    // Remove LoanID from Directory of the LoanBroker pseudo-account.
    if (!view.dirRemove(
            keylet::ownerDir(brokerPseudoAccount), loanSle->at(sfLoanBrokerNode), loanID, false))
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    // A pending loan reverses the bookkeeping performed by LoanSet at proposal
    // time and releases the owner reserve charged to the LoanBroker owner. An
    // active loan is also linked into the borrower's directory and charged the
    // borrower's reserve, both of which are released here.
    if (auto const ter = isLoanPending(loanSle)
            ? reversePendingLoan(view, loanSle, brokerSle, vaultSle, j_)
            : releaseBorrower(view, loanSle, j_))
        return ter;

    // Delete the Loan object
    view.erase(loanSle);

    releaseLoanFromBroker(view, brokerSle, vaultSle, j_);

    associateAsset(*brokerSle, vaultAsset);
    associateAsset(*vaultSle, vaultAsset);

    return tesSUCCESS;
}

void
LoanDelete::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
LoanDelete::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

//------------------------------------------------------------------------------

}  // namespace xrpl

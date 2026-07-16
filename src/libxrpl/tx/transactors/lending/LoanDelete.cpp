#include <xrpl/tx/transactors/lending/LoanDelete.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>  // IWYU pragma: keep
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/ledger/helpers/SLEWrappers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>  // IWYU pragma: keep
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTakesAsset.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

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
    if (loanSle->at(sfPaymentRemaining) > 0)
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
    LoanEntry<ApplyView> loanSle{keylet::loan(loanID), view, j_};
    if (!loanSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    auto const borrower = loanSle->at(sfBorrower);
    auto const borrowerSle = view.peek(keylet::account(borrower));
    if (!borrowerSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    auto const brokerID = loanSle->at(sfLoanBrokerID);
    auto const brokerSle = view.peek(keylet::loanBroker(brokerID));
    if (!brokerSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    auto const vaultSle = view.peek(keylet::vault(brokerSle->at(sfVaultID)));
    if (!vaultSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    auto const vaultAsset = vaultSle->at(sfAsset);

    // Unlink the loan from the LoanBroker pseudo-account's directory and the
    // borrower's directory, and erase it. See LoanEntry::destroy().
    if (auto const ter = loanSle.destroy(); !isTesSuccess(ter))
        return ter;  // LCOV_EXCL_LINE

    // Decrement the LoanBroker's owner count.
    adjustLoanBrokerOwnerCount(view, brokerSle, -1, j_);

    // If there are no loans left, then any remaining debt must be forgiven,
    // because there is no other way to pay it back.
    if (brokerSle->at(sfOwnerCount) == 0)
    {
        auto debtTotalProxy = brokerSle->at(sfDebtTotal);
        if (*debtTotalProxy != beast::kZero)
        {
            XRPL_ASSERT_PARTS(
                roundToAsset(
                    vaultSle->at(sfAsset),
                    debtTotalProxy,
                    getAssetsTotalScale(VaultEntry<ReadView>{vaultSle, view}),
                    Number::RoundingMode::TowardsZero) == beast::kZero,
                "xrpl::LoanDelete::doApply",
                "last loan, remaining debt rounds to zero");
            debtTotalProxy = 0;
        }
    }
    // Decrement the borrower's owner count
    decreaseOwnerCountForObject(view, borrowerSle, loanSle.mutableSle(), 1, j_);

    // These associations shouldn't do anything, but do them just to be safe
    associateAsset(*loanSle, vaultAsset);
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

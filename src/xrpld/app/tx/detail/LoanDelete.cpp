#include <xrpld/app/tx/detail/LoanDelete.h>
//
#include <xrpld/app/misc/LendingHelpers.h>

namespace ripple {

bool
LoanDelete::checkExtraFeatures(PreflightContext const& ctx)
{
    return checkLendingProtocolDependencies(ctx);
}

NotTEC
LoanDelete::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfLoanID] == beast::zero)
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
    auto const loanBrokerSle = ctx.view.read(keylet::loanbroker(loanBrokerID));
    if (!loanBrokerSle)
    {
        // should be impossible
        return tecINTERNAL;  // LCOV_EXCL_LINE
    }
    if (loanBrokerSle->at(sfOwner) != account &&
        loanSle->at(sfBorrower) != account)
    {
        JLOG(ctx.j.warn())
            << "Account is not Loan Broker Owner or Loan Borrower.";
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
    auto const borrower = loanSle->at(sfBorrower);
    auto const borrowerSle = view.peek(keylet::account(borrower));
    if (!borrowerSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    auto const brokerID = loanSle->at(sfLoanBrokerID);
    auto const brokerSle = view.peek(keylet::loanbroker(brokerID));
    if (!brokerSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    auto const brokerPseudoAccount = brokerSle->at(sfAccount);

    auto const vaultSle = view.peek(keylet ::vault(brokerSle->at(sfVaultID)));
    if (!vaultSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    // Remove LoanID from Directory of the LoanBroker pseudo-account.
    if (!view.dirRemove(
            keylet::ownerDir(brokerPseudoAccount),
            loanSle->at(sfLoanBrokerNode),
            loanID,
            false))
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    // Remove LoanID from Directory of the Borrower.
    if (!view.dirRemove(
            keylet::ownerDir(borrower),
            loanSle->at(sfOwnerNode),
            loanID,
            false))
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    // Delete the Loan object
    view.erase(loanSle);

    // Decrement the LoanBroker's owner count.
    // The broker's owner count is solely for the number of outstanding loans,
    // and is distinct from the broker's pseudo-account's owner count
    adjustOwnerCount(view, brokerSle, -1, j_);
    // If there are no loans left, then any remaining debt must be forgiven,
    // because there is no other way to pay it back.
    if (brokerSle->at(sfOwnerCount) == 0)
    {
        auto debtTotalProxy = brokerSle->at(sfDebtTotal);
        if (*debtTotalProxy != beast::zero)
        {
            XRPL_ASSERT_PARTS(
                roundToAsset(
                    vaultSle->at(sfAsset),
                    debtTotalProxy,
                    getVaultScale(vaultSle),
                    Number::towards_zero) == beast::zero,
                "ripple::LoanDelete::doApply",
                "last loan, remaining debt rounds to zero");
            debtTotalProxy = 0;
        }
    }
    // Decrement the borrower's owner count
    adjustOwnerCount(view, borrowerSle, -1, j_);

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

}  // namespace ripple

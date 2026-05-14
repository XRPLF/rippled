/** @file LoanDelete.cpp
 *  Implements the `LoanDelete` transactor for the XRP Ledger lending protocol
 *  (XLS-66).
 *
 *  The teardown sequence requires two ordered steps:
 *  1. All `Loan` objects for a broker are deleted via `LoanDelete` — each call
 *     decrements `sfOwnerCount` on the `LoanBroker` SLE.
 *  2. Once `sfOwnerCount` reaches zero the `LoanBroker` itself may be removed
 *     via `LoanBrokerDelete`.
 *
 *  A critical edge case handled here: accumulated sub-precision rounding dust
 *  in `sfDebtTotal` is forgiven when the last loan is deleted. No future
 *  payment path can reduce it further, and leaving a non-zero `sfDebtTotal`
 *  would permanently block `LoanBrokerDelete` (which requires that the debt
 *  round to zero). The `XRPL_ASSERT_PARTS` guard confirms the residual is
 *  already representationally zero before the assignment.
 */
#include <xrpl/tx/transactors/lending/LoanDelete.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>  // IWYU pragma: keep
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
#include <xrpl/tx/Transactor.h>

#include <memory>

namespace xrpl {

bool
LoanDelete::checkExtraFeatures(PreflightContext const& ctx)
{
    return checkLendingProtocolDependencies(ctx.rules, ctx.tx);
}

NotTEC
LoanDelete::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfLoanID] == beast::kZERO)
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
    auto const borrower = loanSle->at(sfBorrower);
    auto const borrowerSle = view.peek(keylet::account(borrower));
    if (!borrowerSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    auto const brokerID = loanSle->at(sfLoanBrokerID);
    auto const brokerSle = view.peek(keylet::loanbroker(brokerID));
    if (!brokerSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    auto const brokerPseudoAccount = brokerSle->at(sfAccount);

    auto const vaultSle = view.peek(keylet::vault(brokerSle->at(sfVaultID)));
    if (!vaultSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    auto const vaultAsset = vaultSle->at(sfAsset);

    if (!view.dirRemove(
            keylet::ownerDir(brokerPseudoAccount), loanSle->at(sfLoanBrokerNode), loanID, false))
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    if (!view.dirRemove(keylet::ownerDir(borrower), loanSle->at(sfOwnerNode), loanID, false))
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    view.erase(loanSle);

    // The broker's sfOwnerCount tracks outstanding loans only — it is distinct
    // from the broker's pseudo-account owner count, which governs XRP reserves.
    // LoanBrokerDelete decrements the pseudo-account count by two; LoanDelete
    // only touches this broker-level count.
    adjustOwnerCount(view, brokerSle, -1, j_);
    // If no loans remain, forgive any residual sfDebtTotal. Rounding dust
    // accumulates over many payment cycles and cannot be recovered once there
    // are no more loans to repay against. Leaving a non-zero value would
    // permanently block LoanBrokerDelete, which requires debt to round to zero.
    if (brokerSle->at(sfOwnerCount) == 0)
    {
        auto debtTotalProxy = brokerSle->at(sfDebtTotal);
        if (*debtTotalProxy != beast::kZERO)
        {
            XRPL_ASSERT_PARTS(
                roundToAsset(
                    vaultSle->at(sfAsset),
                    debtTotalProxy,
                    getAssetsTotalScale(vaultSle),
                    Number::RoundingMode::TowardsZero) == beast::kZERO,
                "xrpl::LoanDelete::doApply",
                "last loan, remaining debt rounds to zero");
            debtTotalProxy = 0;
        }
    }
    adjustOwnerCount(view, borrowerSle, -1, j_);

    // associateAsset is a lending-transactor convention: STNumber / STTakesAsset
    // fields carry asset-precision metadata that must remain consistent. Even on
    // deletion paths where no write-back of these fields is expected, the
    // defensive call ensures correctness if the convention ever changes.
    associateAsset(*loanSle, vaultAsset);
    associateAsset(*brokerSle, vaultAsset);
    associateAsset(*vaultSle, vaultAsset);

    return tesSUCCESS;
}

void
LoanDelete::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
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

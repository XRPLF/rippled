#include <xrpl/tx/transactors/lending/LoanAccept.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTakesAsset.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

bool
LoanAccept::checkExtraFeatures(PreflightContext const& ctx)
{
    return checkLendingProtocolDependencies(ctx.rules, ctx.tx);
}

NotTEC
LoanAccept::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfLoanID] == beast::kZero)
        return temINVALID;

    return tesSUCCESS;
}

TER
LoanAccept::preclaim(PreclaimContext const& ctx)
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

    if (!loanSle->isFlag(lsfLoanPending))
    {
        JLOG(ctx.j.warn()) << "Loan is not pending acceptance.";
        return tecNO_PERMISSION;
    }

    if (loanSle->at(sfBorrower) != account)
    {
        JLOG(ctx.j.warn()) << "LoanAccept can only be submitted by the Borrower.";
        return tecNO_PERMISSION;
    }

    if (hasExpired(ctx.view, loanSle->at(sfStartDate)))
    {
        JLOG(ctx.j.warn()) << "Loan proposal has expired.";
        return tecEXPIRED;
    }

    auto const brokerSle = ctx.view.read(keylet::loanBroker(loanSle->at(sfLoanBrokerID)));
    if (!brokerSle)
        return tecINTERNAL;  // LCOV_EXCL_LINE
    auto const brokerOwner = brokerSle->at(sfOwner);
    auto const brokerPseudo = brokerSle->at(sfAccount);

    auto const vaultSle = ctx.view.read(keylet::vault(brokerSle->at(sfVaultID)));
    if (!vaultSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    Asset const asset = vaultSle->at(sfAsset);
    auto const vaultPseudo = vaultSle->at(sfAccount);

    if (auto const ter = checkLoanFreeze(
            ctx.view, asset, vaultPseudo, brokerPseudo, account, brokerOwner, ctx.j))
        return ter;

    return tesSUCCESS;
}

TER
LoanAccept::doApply()
{
    auto const& tx = ctx_.tx;
    auto& view = ctx_.view();

    auto const loanID = tx[sfLoanID];
    auto loanSle = view.peek(keylet::loan(loanID));
    if (!loanSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    auto const brokerSle = view.peek(keylet::loanBroker(loanSle->at(sfLoanBrokerID)));
    if (!brokerSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    auto const brokerOwner = brokerSle->at(sfOwner);
    auto const brokerOwnerSle = view.peek(keylet::account(brokerOwner));
    if (!brokerOwnerSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    auto const vaultSle = view.peek(keylet::vault(brokerSle->at(sfVaultID)));
    if (!vaultSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    Asset const vaultAsset = vaultSle->at(sfAsset);
    auto const vaultPseudo = vaultSle->at(sfAccount);

    auto const borrower = loanSle->at(sfBorrower);
    auto const borrowerSle = view.peek(keylet::account(borrower));
    if (!borrowerSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    Number const principalOutstanding = loanSle->at(sfPrincipalOutstanding);
    Number const originationFee = loanSle->at(sfLoanOriginationFee);
    auto const loanAssetsToBorrower = principalOutstanding - originationFee;

    // The loan is no longer pending; it becomes active.
    loanSle->clearFlag(lsfLoanPending);

    auto applyViewContext = ctx_.getApplyViewContext();
    // Release the owner reserve that was charged to the LoanBroker.Owner when
    // the loan was proposed, and charge it to the borrower instead.
    decreaseOwnerCount(view, brokerOwnerSle, {}, 1, j_);
    if (auto const ter =
            reserveLoanOwner(view, borrower, borrowerSle, accountID_, preFeeBalance_, j_))
        return ter;

    // Disburse the principal to the borrower and the origination fee, if any,
    // to the broker owner.
    if (auto const ter = disburseLoan(
            applyViewContext,
            borrower,
            borrowerSle,
            brokerOwner,
            brokerOwnerSle,
            vaultPseudo,
            vaultAsset,
            loanAssetsToBorrower,
            originationFee,
            accountID_,
            brokerOwner,
            j_))
        return ter;

    // Release the reserved principal now that it has been paid out.
    auto vaultAssetReservedProxy = vaultSle->at(sfAssetsReserved);
    vaultAssetReservedProxy -= principalOutstanding;
    view.update(vaultSle);

    // Make the borrower the owner of the loan.
    if (auto const ter = linkLoanBorrower(view, borrower, loanSle))
        return ter;
    view.update(loanSle);

    associateAsset(*loanSle, vaultAsset);
    associateAsset(*brokerSle, vaultAsset);
    associateAsset(*vaultSle, vaultAsset);

    return tesSUCCESS;
}

void
LoanAccept::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
LoanAccept::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

//------------------------------------------------------------------------------

}  // namespace xrpl

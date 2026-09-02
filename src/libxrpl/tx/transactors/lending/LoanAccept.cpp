#include <xrpl/tx/transactors/lending/LoanAccept.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
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
    // 3.9.3.1.1 LoanID is zero. (temINVALID)
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
    // 3.9.3.2.1 The Loan object with the specified LoanID does not exist on the ledger.
    // (tecNO_ENTRY)
    if (!loanSle)
    {
        JLOG(ctx.j.warn()) << "Loan does not exist.";
        return tecNO_ENTRY;
    }

    // 3.9.3.2.2 The Loan object does not have the lsfLoanPending flag set. (tecNO_PERMISSION)
    if (!isLoanPending(loanSle))
    {
        JLOG(ctx.j.warn()) << "Loan is not pending acceptance.";
        return tecNO_PERMISSION;
    }

    // 3.9.3.2.3 The Account submitting the transaction is not the Loan.Borrower. (tecNO_PERMISSION)
    if (loanSle->at(sfBorrower) != account)
    {
        JLOG(ctx.j.warn()) << "LoanAccept can only be submitted by the Borrower.";
        return tecNO_PERMISSION;
    }

    // 3.9.3.2.4 The current ledger timestamp is greater than or equal to Loan.StartDate (the
    // proposal has expired). (tecEXPIRED)
    if (hasExpired(ctx.view, loanSle->at(sfStartDate)))
    {
        JLOG(ctx.j.warn()) << "Loan proposal has expired.";
        return tecEXPIRED;
    }

    auto const brokerSle = ctx.view.read(keylet::loanBroker(loanSle->at(sfLoanBrokerID)));
    if (!brokerSle)
    {
        // LCOV_EXCL_START
        JLOG(ctx.j.fatal()) << "LoanAccept: LoanBroker does not exist.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }
    auto const brokerOwner = brokerSle->at(sfOwner);
    auto const brokerPseudo = brokerSle->at(sfAccount);

    auto const vaultSle = ctx.view.read(keylet::vault(brokerSle->at(sfVaultID)));
    if (!vaultSle)
    {
        // LCOV_EXCL_START
        JLOG(ctx.j.fatal()) << "LoanAccept: Vault does not exist.";
        return tefBAD_LEDGER;
        // LCOV_EXCL_STOP
    }
    Asset const asset = vaultSle->at(sfAsset);
    auto const vaultPseudo = vaultSle->at(sfAccount);

    // Closed-ended vault gate: acceptance is only meaningful during the
    // Investment phase. If the vault is still in Subscription, the loan is
    // being accepted before its funds are formally in the investment pool;
    // if it has entered Redemption, the vault is winding down and can no
    // longer hand principal out to a borrower.
    switch (getVaultPhase(ctx.view, vaultSle))
    {
        case VaultPhase::Subscription:
            JLOG(ctx.j.warn()) << "Vault is still in the subscription phase.";
            return tecTOO_SOON;
        case VaultPhase::Redemption:
            JLOG(ctx.j.warn()) << "Vault has entered the redemption phase.";
            return tecEXPIRED;
        case VaultPhase::NoPhase:
        case VaultPhase::Investment:
            break;
    }

    // 3.9.3.2.6 The Vault pseudo-account is frozen for the asset. (tecFROZEN for IOUs, tecLOCKED
    // for MPTs)
    // 3.9.3.2.7 The LoanBroker pseudo-account is deep frozen for the asset. (tecFROZEN for IOUs,
    // tecLOCKED for MPTs)
    // 3.9.3.2.8 The Borrower is frozen for the asset. (tecFROZEN for IOUs, tecLOCKED for MPTs)
    // 3.9.3.2.9 The LoanBroker.Owner is deep frozen for the asset. (tecFROZEN for IOUs, tecLOCKED
    // for MPTs)
    // 3.9.3.2.10 Cannot add asset holding for the Vault.Asset (e.g., MPToken or TrustLine issues).
    // (tecNO_PERMISSION)
    if (auto const ter = checkLoanFreeze(
            ctx.view, asset, vaultPseudo, brokerPseudo, account, brokerOwner, ctx.j))
        return ter;

    // canAddHolding does not look at existing lines. After fixCleanup3_4_0,
    // addEmptyHolding is a no-op when the destination already holds the
    // asset, so only run the creation gate for a holding that is absent.
    Number const originationFee = loanSle->at(sfLoanOriginationFee);
    if (!ctx.view.rules().enabled(fixCleanup3_4_0) || !holdingExists(ctx.view, account, asset) ||
        (originationFee != beast::kZero && !holdingExists(ctx.view, brokerOwner, asset)))
    {
        if (auto const ter = canAddHolding(ctx.view, asset))
            return ter;
    }

    // Re-verify that the borrower and broker owner (the two accounts that
    // receive funds at disbursement) are authorised to hold the vault asset.
    // WeakAuth is used because the holdings need not exist yet; they are
    // created at disbursement.
    // 3.9.3.2.11 The Borrower is not authorized for the asset. (tecNO_AUTH)
    if (auto const ter = requireAuth(ctx.view, asset, account, AuthType::WeakAuth))
        return ter;
    // 3.9.3.2.12 The LoanBroker.Owner is not authorized for the asset. (tecNO_AUTH)
    if (auto const ter = requireAuth(ctx.view, asset, brokerOwner, AuthType::WeakAuth))
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

    // 3.9.4.1 Clear the lsfLoanPending flag on the Loan object.
    loanSle->clearFlag(lsfLoanPending);

    // 3.9.4.2 Release the reserve from the Loan Broker: Decrement
    // AccountRoot(LoanBroker.Owner).OwnerCount by 1.
    decreaseOwnerCount(view, brokerOwnerSle, {}, 1, j_);

    // 3.9.4.3 Charge the reserve to the Borrower: Increment AccountRoot(Borrower).OwnerCount by 1.
    // 3.9.3.2.5 The Borrower does not have sufficient reserve for the Loan object.
    // (tecINSUFFICIENT_RESERVE)
    if (auto const ter =
            reserveLoanOwner(view, borrower, borrowerSle, accountID_, preFeeBalance_, j_))
        return ter;

    // 3.9.4.4 - 3.9.4.6 Disburse the principal to the borrower and the origination fee, if any, to
    // the broker owner.
    auto applyViewContext = ctx_.getApplyViewContext();
    if (auto const ter = disburseLoan(
            applyViewContext,
            borrowerSle,
            brokerOwnerSle,
            vaultPseudo,
            vaultAsset,
            loanAssetsToBorrower,
            originationFee,
            accountID_,
            brokerOwner,
            j_))
        return ter;

    // 3.9.4.7 Update Vault object: Decrease Vault.AssetsReserved by Loan.PrincipalOutstanding.
    vaultSle->at(sfAssetsReserved) -= principalOutstanding;
    view.update(vaultSle);

    // 3.9.4.8 Make the borrower the owner of the loan.
    if (auto const ter = dirLink(view, borrower, loanSle, sfOwnerNode))
        return ter;  // LCOV_EXCL_LINE
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

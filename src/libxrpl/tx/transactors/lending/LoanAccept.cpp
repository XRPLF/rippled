#include <xrpl/tx/transactors/lending/LoanAccept.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/Asset.h>
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

    if (!isLoanPending(loanSle))
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

    // The proposal booked no interest (see LoanSet::createLoan): it only
    // lands in the vault here, in AssetsTotal under instant recognition or in
    // YieldUnrealized on a FixedPrecision vault. LoanSet's capacity guards
    // passed at proposal time, but loans originated since then do not see the
    // pending interest and may have used up the headroom they relied on, so
    // re-run the guards against the vault as it stands now.
    auto const vaultVersion = getVaultVersion(vaultSle);
    auto const state = constructLoanState(loanSle);
    if (vaultVersion == VaultVersion::Legacy &&
        loanOriginationExceedsVaultMaximum(
            vaultSle, vaultSle->at(sfAssetsTotal), state.interestDue))
    {
        JLOG(ctx.j.warn()) << "Loan interest would exceed the maximum assets of the vault.";
        return tecLIMIT_EXCEEDED;
    }
    // A coarsened vault fails this check too: its AssetsTotal is at least
    // 10^(16 + baseScale), above the Open limit of 9 * 10^(15 + baseScale).
    if (vaultVersion == VaultVersion::FixedPrecision &&
        vaultOpenZoneCapacity(vaultSle, state.interestDue) > getVaultOpenLimit(vaultSle))
    {
        JLOG(ctx.j.warn()) << "Loan interest would exceed the FixedPrecision Vault's Open zone.";
        return tecLIMIT_EXCEEDED;
    }

    if (auto const ter = checkLoanFreeze(
            ctx.view,
            asset,
            loanSle->at(sfLoanOriginationFee),
            vaultPseudo,
            brokerPseudo,
            account,
            brokerOwner,
            ctx.j))
        return ter;

    // Re-verify the disbursement recipients: LoanSet checked them when the
    // pending loan was created, but authorisation can be revoked in between.
    if (auto const ter = checkLoanRecipientAuth(ctx.view, asset, account, brokerOwner))
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

    auto const brokerSle = view.read(keylet::loanBroker(loanSle->at(sfLoanBrokerID)));
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
    auto const state = constructLoanState(loanSle);

    loanSle->clearFlag(lsfLoanPending);

    decreaseOwnerCount(view, brokerOwnerSle, {}, 1, j_);

    if (auto const ter =
            reserveLoanOwner(view, borrower, borrowerSle, accountID_, preFeeBalance_, j_))
        return ter;

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

    if (getVaultVersion(vaultSle) == VaultVersion::FixedPrecision)
    {
        if (auto ter = adjustVaultBalances(
                vaultSle,
                {
                    .yield = state.interestDue,
                    .reserved = -principalOutstanding,
                },
                j_);
            !isTesSuccess(ter))
        {
            return ter;
        }
    }
    else
    {
        // Book the interest the proposal deferred. The dispatcher yields a
        // zero delta on a cash-basis vault, which recognizes interest only as
        // it is paid.
        auto const assetsTotalDelta =
            loanOriginationDeltas(vaultSle, principalOutstanding, state.interestDue)
                .assetsTotalDelta;
        vaultSle->at(sfAssetsTotal) += assetsTotalDelta;
        vaultSle->at(sfAssetsReserved) -= principalOutstanding;
    }
    view.update(vaultSle);

    if (auto const ter = dirLink(view, borrower, loanSle, sfOwnerNode))
        return ter;  // LCOV_EXCL_LINE
    view.update(loanSle);

    associateAsset(*loanSle, vaultAsset);
    associateAsset(*vaultSle, vaultAsset);

    return tesSUCCESS;
}

void
LoanAccept::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
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

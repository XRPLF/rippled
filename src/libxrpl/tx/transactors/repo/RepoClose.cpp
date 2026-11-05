#include <xrpl/tx/transactors/repo/RepoClose.h>

#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/helpers/RepoHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>

namespace xrpl {

std::uint32_t
RepoClose::getFlagsMask(PreflightContext const& ctx)
{
    return tfUniversalMask;
}

NotTEC
RepoClose::preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

TER
RepoClose::preclaim(PreclaimContext const& ctx)
{
    auto const sleRepo = ctx.view.read(keylet::repo(ctx.tx[sfRepoID]));
    if (!sleRepo)
        return tecNO_ENTRY;

    // A closed repo holds the repurchase amount for the buyer, who alone
    // collects it once it can receive it.
    if (repo::isClosed(sleRepo))
    {
        if ((*sleRepo)[sfCounterparty] != ctx.tx[sfAccount])
            return tecNO_PERMISSION;
        return repo::checkRelease(ctx.view, ctx.tx[sfAccount], (*sleRepo)[sfAmount]);
    }

    if (!repo::isActive(sleRepo))
        return tecREPO_PENDING;

    // Only the seller repurchases.
    if ((*sleRepo)[sfAccount] != ctx.tx[sfAccount])
        return tecNO_PERMISSION;

    auto const closeTime = ctx.view.parentCloseTime().time_since_epoch().count();
    if (closeTime > repo::defaultTime(sleRepo))
        return tecEXPIRED;

    if (auto const ter =
            repo::checkRelease(ctx.view, (*sleRepo)[sfAccount], (*sleRepo)[sfCollateralAmount]);
        !isTesSuccess(ter))
        return ter;

    // The repurchase must be payable, or the collateral stays locked. A buyer
    // that cannot receive it does not block the close: the seller pays the
    // amount into the repo for the buyer instead, which is a payment to the
    // cash issuer.
    STAmount const owed = repo::repurchaseAmount(sleRepo, closeTime);
    AccountID const buyer = (*sleRepo)[sfCounterparty];
    AccountID const payee =
        repo::canReceive(ctx.view, buyer, owed.asset()) ? buyer : owed.getIssuer();
    return repo::checkPayment(
        ctx.view, ctx.tx[sfAccount], payee, owed, ctx.tx[sfFee].xrp(), AuthType::WeakAuth, ctx.j);
}

TER
RepoClose::doApply()
{
    auto const sleRepo = view().peek(keylet::repo(ctx_.tx[sfRepoID]));
    if (!sleRepo)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    if (repo::isClosed(sleRepo))
        return repo::collect(ctx_.getApplyViewContext(), sleRepo, j_);

    auto const closeTime = view().parentCloseTime().time_since_epoch().count();
    STAmount const owed = repo::repurchaseAmount(sleRepo, closeTime);
    AccountID const counterparty = (*sleRepo)[sfCounterparty];

    if (!repo::canReceive(view(), counterparty, owed.asset()))
        return repo::closeForBuyer(ctx_.getApplyViewContext(), sleRepo, owed, j_);

    // The buyer may have removed its holding of the cash since accepting; the
    // repurchase opens it again rather than letting the buyer block it.
    if (auto const ter =
            repo::ensureHolding(ctx_.getApplyViewContext(), counterparty, owed.asset(), j_);
        !isTesSuccess(ter))
        return ter;  // LCOV_EXCL_LINE

    // The gate: the collateral is released only because this payment landed.
    // Preclaim checked it is payable.
    if (auto const ter = accountSend(view(), accountID_, counterparty, owed, j_);
        !isTesSuccess(ter))
        return ter;  // LCOV_EXCL_LINE

    return repo::releaseAndDelete(ctx_.getApplyViewContext(), sleRepo, accountID_, j_);
}

void
RepoClose::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
{
}

bool
RepoClose::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    return true;
}

}  // namespace xrpl

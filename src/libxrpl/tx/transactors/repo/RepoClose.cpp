#include <xrpl/tx/transactors/repo/RepoClose.h>

#include <xrpl/basics/Log.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/RepoHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>

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

    if (!repo::isActive(sleRepo))
        return tecREPO_PENDING;

    // Only the seller repurchases.
    if ((*sleRepo)[sfAccount] != ctx.tx[sfAccount])
        return tecNO_PERMISSION;

    auto const closeTime = ctx.view.parentCloseTime().time_since_epoch().count();
    if (closeTime > (*sleRepo)[sfMaturityDate] + (*sleRepo)[sfGracePeriod])
        return tecEXPIRED;

    return tesSUCCESS;
}

TER
RepoClose::doApply()
{
    auto const sleRepo = view().peek(keylet::repo(ctx_.tx[sfRepoID]));
    if (!sleRepo)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const closeTime = view().parentCloseTime().time_since_epoch().count();
    STAmount const owed = repo::repurchaseAmount(sleRepo, closeTime);
    AccountID const counterparty = (*sleRepo)[sfCounterparty];

    // The gate: the collateral is released only because this payment landed.
    // If it fails, the whole transaction fails and the collateral stays locked.
    if (auto const ter = accountSend(view(), accountID_, counterparty, owed, j_);
        !isTesSuccess(ter))
        return ter;

    return repo::releaseAndDelete(ctx_.getApplyViewContext(), sleRepo, accountID_, j_);
}

void
RepoClose::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
}

bool
RepoClose::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    return true;
}

}  // namespace xrpl

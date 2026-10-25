#include <xrpl/tx/transactors/repo/RepoCancel.h>

#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/helpers/RepoHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>

namespace xrpl {

std::uint32_t
RepoCancel::getFlagsMask(PreflightContext const& ctx)
{
    return tfUniversalMask;
}

NotTEC
RepoCancel::preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

TER
RepoCancel::preclaim(PreclaimContext const& ctx)
{
    auto const sleRepo = ctx.view.read(keylet::repo(ctx.tx[sfRepoID]));
    if (!sleRepo)
        return tecNO_ENTRY;

    if (repo::isActive(sleRepo))
        return tecREPO_ACTIVE;

    // The seller may withdraw the offer at any time; anyone may clear it once
    // it has expired.
    if ((*sleRepo)[sfAccount] != ctx.tx[sfAccount] &&
        ctx.view.parentCloseTime().time_since_epoch().count() < (*sleRepo)[sfExpiration])
        return tecNO_PERMISSION;

    return repo::checkRelease(ctx.view, (*sleRepo)[sfAccount], (*sleRepo)[sfCollateralAmount]);
}

TER
RepoCancel::doApply()
{
    auto const sleRepo = view().peek(keylet::repo(ctx_.tx[sfRepoID]));
    if (!sleRepo)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    // No cash moved, so the collateral simply goes back.
    return repo::releaseAndDelete(ctx_.getApplyViewContext(), sleRepo, (*sleRepo)[sfAccount], j_);
}

void
RepoCancel::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
{
}

bool
RepoCancel::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    return true;
}

}  // namespace xrpl

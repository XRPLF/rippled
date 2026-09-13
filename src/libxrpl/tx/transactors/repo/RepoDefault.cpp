#include <xrpl/tx/transactors/repo/RepoDefault.h>

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
RepoDefault::getFlagsMask(PreflightContext const& ctx)
{
    return tfUniversalMask;
}

NotTEC
RepoDefault::preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

TER
RepoDefault::preclaim(PreclaimContext const& ctx)
{
    auto const sleRepo = ctx.view.read(keylet::repo(ctx.tx[sfRepoID]));
    if (!sleRepo)
        return tecNO_ENTRY;

    if (!repo::isActive(sleRepo))
        return tecREPO_PENDING;

    // Anyone may close out a defaulted repo, but only once the seller's time
    // to repurchase has run out.
    auto const closeTime = ctx.view.parentCloseTime().time_since_epoch().count();
    if (closeTime <= (*sleRepo)[sfMaturityDate] + (*sleRepo)[sfGracePeriod])
        return tecTOO_SOON;

    return tesSUCCESS;
}

TER
RepoDefault::doApply()
{
    auto const sleRepo = view().peek(keylet::repo(ctx_.tx[sfRepoID]));
    if (!sleRepo)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    // The buyer keeps the collateral; no cash moves.
    return repo::releaseAndDelete(
        ctx_.getApplyViewContext(), sleRepo, (*sleRepo)[sfCounterparty], j_);
}

void
RepoDefault::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
}

bool
RepoDefault::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    return true;
}

}  // namespace xrpl

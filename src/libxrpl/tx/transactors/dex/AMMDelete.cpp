#include <xrpl/tx/transactors/dex/AMMDelete.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/AMMHelpers.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <memory>

namespace xrpl {

bool
AMMDelete::checkExtraFeatures(PreflightContext const& ctx)
{
    if (!ammEnabled(ctx.rules))
        return false;

    return ctx.rules.enabled(featureMPTokensV2) ||
        (!ctx.tx[sfAsset].holds<MPTIssue>() && !ctx.tx[sfAsset2].holds<MPTIssue>());
}

NotTEC
AMMDelete::preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

TER
AMMDelete::preclaim(PreclaimContext const& ctx)
{
    auto const ammSle = ctx.view.read(keylet::amm(ctx.tx[sfAsset], ctx.tx[sfAsset2]));
    if (!ammSle)
    {
        JLOG(ctx.j.debug()) << "AMM Delete: Invalid asset pair.";
        return terNO_AMM;
    }

    auto const lpTokensBalance = (*ammSle)[sfLPTokenBalance];
    if (lpTokensBalance != beast::kZero)
        return tecAMM_NOT_EMPTY;

    return tesSUCCESS;
}

TER
AMMDelete::doApply()
{
    // This is the ledger view that we work against. Transactions are applied
    // as we go on processing transactions.
    Sandbox sb(&ctx_.view());

    auto const ter = deleteAMMAccount(sb, ctx_.tx[sfAsset], ctx_.tx[sfAsset2], j_);
    if (isTesSuccess(ter) || ter == tecINCOMPLETE)
        sb.apply(ctx_.rawView());

    return ter;
}

void
AMMDelete::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
AMMDelete::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl

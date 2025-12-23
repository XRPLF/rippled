#include <xrpld/app/misc/AMMUtils.h>
#include <xrpld/app/tx/detail/AMMDelete.h>

#include <xrpl/ledger/Sandbox.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

bool
AMMDelete::checkExtraFeatures(PreflightContext const& ctx)
{
    return ammEnabled(ctx.rules);
}

NotTEC
AMMDelete::preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
}

TER
AMMDelete::preclaim(PreclaimContext const& ctx)
{
    auto const ammSle =
        ctx.view.read(keylet::amm(ctx.tx[sfAsset], ctx.tx[sfAsset2]));
    if (!ammSle)
    {
        JLOG(ctx.j.debug()) << "AMM Delete: Invalid asset pair.";
        return terNO_AMM;
    }

    auto const lpTokensBalance = (*ammSle)[sfLPTokenBalance];
    if (lpTokensBalance != beast::zero)
        return tecAMM_NOT_EMPTY;

    return tesSUCCESS;
}

TER
AMMDelete::doApply()
{
    // This is the ledger view that we work against. Transactions are applied
    // as we go on processing transactions.
    Sandbox sb(&ctx_.view());

    auto const ter = deleteAMMAccount(
        sb, ctx_.tx[sfAsset].get<Issue>(), ctx_.tx[sfAsset2].get<Issue>(), j_);
    if (ter == tesSUCCESS || ter == tecINCOMPLETE)
        sb.apply(ctx_.rawView());

    return ter;
}

}  // namespace ripple

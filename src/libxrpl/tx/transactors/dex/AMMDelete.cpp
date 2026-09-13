#include <xrpl/tx/transactors/dex/AMMDelete.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/AMMHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
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

#include <cstdint>
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
    auto const curveType = ctx.tx.isFieldPresent(sfCurveType) ? ctx.tx.getFieldU8(sfCurveType)
                                                              : std::uint8_t(CtConstantProduct);
    auto const ammSle = ctx.view.read(keylet::amm(ctx.tx[sfAsset], ctx.tx[sfAsset2], curveType));
    if (!ammSle)
    {
        JLOG(ctx.j.debug()) << "AMM Delete: Invalid asset pair.";
        return terNO_AMM;
    }

    auto const lpTokensBalance = (*ammSle)[sfLPTokenBalance];
    if (lpTokensBalance != beast::kZero)
        return tecAMM_NOT_EMPTY;

    // ConcentratedLiquidity pools have no fungible LP token supply, so
    // the LPTokenBalance check above is a no-op for them. Reject delete
    // while any positions still reference the pool — otherwise the
    // pool's outstanding ltAMM_POSITION and ltAMM_TICK SLEs (plus the
    // asset balances on the AMM's trustlines that back them) would
    // orphan.
    if (curveType == CtConcentratedLiquidity &&
        ammSle->getFieldU32(sfPositionCount) > 0)
        return tecHAS_OBLIGATIONS;

    // Binned pools: refuse delete while any bin SLE still exists. The
    // bin SLEs own per-bin MPT issuance references; deleting the AMM
    // without first running AMMBinDestroy on every bin would orphan
    // issuance SLEs and leave LPs holding MPTokens against a deleted
    // issuer account. Caller must AMMWithdraw all positions, then
    // AMMBinDestroy each bin, then AMMDelete.
    if (curveType == CtBinned)
    {
        bool anyBin = false;
        forEachItem(ctx.view, ammSle->getAccountID(sfAccount),
            [&](std::shared_ptr<SLE const> const& s) {
                if (anyBin)
                    return;
                if (s && s->getType() == ltAMM_BIN)
                    anyBin = true;
            });
        if (anyBin)
            return tecHAS_OBLIGATIONS;
    }

    return tesSUCCESS;
}

TER
AMMDelete::doApply()
{
    // This is the ledger view that we work against. Transactions are applied
    // as we go on processing transactions.
    Sandbox sb(&ctx_.view());

    auto const curveType =
        ctx_.tx.isFieldPresent(sfCurveType) ? ctx_.tx.getFieldU8(sfCurveType) : std::uint8_t(0);
    auto const ter = deleteAMMAccount(sb, ctx_.tx[sfAsset], ctx_.tx[sfAsset2], j_, curveType);
    if (isTesSuccess(ter) || ter == tecINCOMPLETE)
        sb.apply(ctx_.rawView());

    return ter;
}

void
AMMDelete::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
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

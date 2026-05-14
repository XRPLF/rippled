/** @file
 *  Implements the `AMMDelete` transactor, which permanently removes a
 *  fully-drained AMM pool from the XRP Ledger.
 *
 *  This file is intentionally thin: it owns only the three-phase validation
 *  logic (`checkExtraFeatures`, `preflight`, `preclaim`) and the
 *  `Sandbox`/commit pattern in `doApply`. The actual ledger-mutation
 *  sequence — iterating the AMM owner directory, deleting trustlines and
 *  MPToken objects, and erasing the `ltAMM` SLE and pseudo-account — lives
 *  in `deleteAMMAccount()` in `AMMHelpers.cpp`, enabling the same deletion
 *  logic to be reused by `AMMWithdraw` when a withdrawal drains a pool to
 *  zero.
 *
 *  @note Deletion is chunked: each `doApply` removes at most
 *      `maxDeletableAMMTrustLines` (512) trustlines and commits that
 *      partial progress even when `tecINCOMPLETE` is returned, so the
 *      submitter must re-submit until the pool is fully erased.
 *  @see AMMHelpers.h, AMMWithdraw
 */
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
    if (lpTokensBalance != beast::kZERO)
        return tecAMM_NOT_EMPTY;

    return tesSUCCESS;
}

TER
AMMDelete::doApply()
{
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

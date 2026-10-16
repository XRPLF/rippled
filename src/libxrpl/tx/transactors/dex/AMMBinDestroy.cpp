// Decommission an empty bin (and its per-bin MPT issuance) in a
// CtBinned AMM pool. Counterpart to AMMBinCreate. Required so pools
// that churn many bins don't accumulate stranded SLEs forever.

#include <xrpl/tx/transactors/dex/AMMBinDestroy.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/AMMCurve.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

bool
AMMBinDestroy::checkExtraFeatures(PreflightContext const& ctx)
{
    return ctx.rules.enabled(featureAMMCurves);
}

NotTEC
AMMBinDestroy::preflight(PreflightContext const& ctx)
{
    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;
    if (!ctx.tx.isFieldPresent(sfAsset) || !ctx.tx.isFieldPresent(sfAsset2))
        return temMALFORMED;
    if (!ctx.tx.isFieldPresent(sfBinID))
        return temMALFORMED;
    auto const binID = ctx.tx.getFieldI32(sfBinID);
    if (binID < minBinID || binID > maxBinID)
        return temMALFORMED;
    return tesSUCCESS;
}

TER
AMMBinDestroy::preclaim(PreclaimContext const& ctx)
{
    auto const asset = ctx.tx[sfAsset];
    auto const asset2 = ctx.tx[sfAsset2];
    auto const ammSle = ctx.view.read(keylet::amm(asset, asset2, CtBinned));
    if (!ammSle)
        return terNO_AMM;
    if (getCurveType(*ammSle) != CtBinned)
        return tecAMM_FAILED;

    auto const binID = ctx.tx.getFieldI32(sfBinID);
    auto const binSle = ctx.view.read(keylet::ammBin(ammSle->key(), binID));
    if (!binSle)
        return tecNO_ENTRY;

    // Can't destroy the active bin — the AMM SLE would dangle.
    if (ammSle->isFieldPresent(sfActiveBinID) &&
        ammSle->getFieldI32(sfActiveBinID) == binID)
    {
        // Allowed only if the active bin AND every other bin is empty
        // (whole pool is empty). Otherwise caller must drain into a
        // different bin first so AMMWithdraw can advance activeBinID.
        bool anyOtherBinHasShares = false;
        forEachItem(ctx.view, ammSle->getAccountID(sfAccount),
            [&](std::shared_ptr<SLE const> const& s) {
                if (anyOtherBinHasShares)
                    return;
                if (!s || s->getType() != ltAMM_BIN)
                    return;
                if (!s->isFieldPresent(sfAMMID) ||
                    s->getFieldH256(sfAMMID) != ammSle->key())
                    return;
                if (s->getFieldI32(sfBinID) == binID)
                    return;
                if (s->getFieldU64(sfOutstandingAmount) > 0)
                    anyOtherBinHasShares = true;
            });
        if (anyOtherBinHasShares)
            return tecAMM_FAILED;
    }

    // Bin must be empty.
    if (binSle->getFieldU64(sfOutstandingAmount) != 0)
        return tecAMM_FAILED;
    if (binSle->getFieldAmount(sfReserve0) > beast::kZero ||
        binSle->getFieldAmount(sfReserve1) > beast::kZero)
        return tecAMM_FAILED;

    // The MPT issuance must have zero outstanding too (defensive —
    // outstanding shares track the bin's, but check explicitly).
    if (binSle->isFieldPresent(sfMPTokenIssuanceID))
    {
        auto const mptId = binSle->getFieldH192(sfMPTokenIssuanceID);
        auto const iss = ctx.view.read(keylet::mptokenIssuance(mptId));
        if (iss && iss->getFieldU64(sfOutstandingAmount) != 0)
            return tecAMM_FAILED;
    }

    return tesSUCCESS;
}

TER
AMMBinDestroy::doApply()
{
    Sandbox sb(&ctx_.view());

    auto const asset = ctx_.tx[sfAsset].get<Issue>();
    auto const asset2 = ctx_.tx[sfAsset2].get<Issue>();
    auto const binID = ctx_.tx.getFieldI32(sfBinID);

    auto ammSle = sb.peek(keylet::amm(asset, asset2, CtBinned));
    if (!ammSle)
        return tecINTERNAL;
    auto const ammAccountID = (*ammSle)[sfAccount];

    auto binSle = sb.peek(keylet::ammBin(ammSle->key(), binID));
    if (!binSle)
        return tecNO_ENTRY;

    // Destroy the per-bin MPT issuance first (if present).
    if (binSle->isFieldPresent(sfMPTokenIssuanceID))
    {
        auto const mptId = binSle->getFieldH192(sfMPTokenIssuanceID);
        auto issSle = sb.peek(keylet::mptokenIssuance(mptId));
        if (issSle)
        {
            if (issSle->getFieldU64(sfOutstandingAmount) != 0)
                return tecAMM_FAILED;
            // Remove the issuance from the AMM pseudo-account's
            // owner directory.
            auto const issOwnerNode = issSle->getFieldU64(sfOwnerNode);
            if (!sb.dirRemove(
                    keylet::ownerDir(ammAccountID),
                    issOwnerNode,
                    issSle->key(),
                    true))
                return tecINTERNAL;
            sb.erase(issSle);
            // AMM pseudo-account has no reserve to adjust.
        }
    }

    // Remove the bin SLE from the AMM's owner directory.
    auto const binOwnerNode = binSle->getFieldU64(sfOwnerNode);
    if (!sb.dirRemove(
            keylet::ownerDir(ammAccountID),
            binOwnerNode,
            binSle->key(),
            true))
        return tecINTERNAL;
    sb.erase(binSle);

    sb.apply(ctx_.rawView());
    return tesSUCCESS;
}

void
AMMBinDestroy::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
}

bool
AMMBinDestroy::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl

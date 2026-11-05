// Provision a bin within a kCtBinned AMM pool and create its per-bin
// MPTokenIssuance. Split out of AMMDeposit so the MPT-create privilege
// (CreateMptIssuance) can sit on this transactor without restricting
// AMMDeposit's general-deposit semantics.

#include <xrpl/tx/transactors/dex/AMMBinCreate.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/AMMCurve.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/transactors/token/MPTokenIssuanceCreate.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace xrpl {

XRPAmount
AMMBinCreate::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    // Anti-spam: each bin SLE + MPT issuance is two new state entries;
    // charging one owner reserve as fee makes pool-state inflation
    // expensive enough that an attacker can't trivially fill ±221818 bins.
    return calculateOwnerReserveFee(view, tx);
}

NotTEC
AMMBinCreate::preflight(PreflightContext const& ctx)
{
    auto const binID = ctx.tx.getFieldI32(sfBinID);
    if (binID < minBinID || binID > maxBinID)
        return temMALFORMED;

    return tesSUCCESS;
}

TER
AMMBinCreate::preclaim(PreclaimContext const& ctx)
{
    auto const asset = ctx.tx[sfAsset];
    auto const asset2 = ctx.tx[sfAsset2];
    auto const ammKeylet = keylet::amm(asset, asset2, kCtBinned);
    auto const ammSle = ctx.view.read(ammKeylet);
    if (!ammSle)
        return terNO_AMM;

    auto const binID = ctx.tx.getFieldI32(sfBinID);
    auto const binKeylet = keylet::ammBin(ammSle->key(), binID);
    if (ctx.view.read(binKeylet))
        return tecAMM_FAILED;  // already exists

    return tesSUCCESS;
}

TER
AMMBinCreate::doApply()
{
    Sandbox sb(&ctx_.view());

    auto const asset = ctx_.tx[sfAsset];
    auto const asset2 = ctx_.tx[sfAsset2];
    auto const binID = ctx_.tx.getFieldI32(sfBinID);

    auto const ammSle = sb.peek(keylet::amm(asset, asset2, kCtBinned));
    if (!ammSle)
        return tecINTERNAL;
    auto const ammAccountID = (*ammSle)[sfAccount];
    auto const ammAsset0 = (*ammSle)[sfAsset];
    auto const ammAsset1 = (*ammSle)[sfAsset2];

    auto const binKeylet = keylet::ammBin(ammSle->key(), binID);
    if (sb.read(binKeylet))
        return tecAMM_FAILED;

    // Create the per-bin MPT issuance with the AMM pseudo-account as
    // issuer. Sequence = binID-derived (offset into positive uint32) so
    // each bin in a pool gets a unique MPT ID.
    auto const mptSequence = static_cast<std::uint32_t>(
        static_cast<std::int64_t>(binID) - static_cast<std::int64_t>(minBinID) + 1);
    auto const maybeMpt = MPTokenIssuanceCreate::create(
        ApplyViewContext{.view = sb, .tx = ctx_.tx},
        ctx_.journal,
        {
            .priorBalance = std::nullopt,
            .account = ammAccountID,
            .sequence = mptSequence,
            .flags = static_cast<std::uint32_t>(tfMPTCanTransfer),
        });
    if (!maybeMpt)
        return maybeMpt.error();
    auto const mptIssuanceID = *maybeMpt;

    // MPTokenIssuanceCreate::create unconditionally bumps the AMM
    // pseudo-account's owner count by +1. The bin's MPT issuance is
    // protocol-managed (the AMM pseudo-account cannot fund reserves);
    // cancel that increment so per-bin state inflation doesn't
    // accumulate against AMMDelete's "owner count must be zero"
    // invariant. AMMBinDestroy erases the issuance without an
    // adjustOwnerCount call, mirroring this exemption: the AMM
    // pseudo-account never carries owner-count contribution from
    // bins across their full lifecycle.
    exemptAMMOwnedSLE(sb, ammAccountID, ctx_.journal);

    auto const binSle = std::make_shared<SLE>(binKeylet);
    (*binSle)[sfAMMID] = ammSle->key();
    binSle->setFieldI32(sfBinID, binID);
    binSle->setFieldAmount(sfReserve0, STAmount{ammAsset0, 0});
    binSle->setFieldAmount(sfReserve1, STAmount{ammAsset1, 0});
    binSle->setFieldNumber(sfFeeGrowthBin0, STNumber{sfFeeGrowthBin0, Number{0}});
    binSle->setFieldNumber(sfFeeGrowthBin1, STNumber{sfFeeGrowthBin1, Number{0}});
    binSle->setFieldU64(sfOutstandingAmount, 0);
    binSle->setFieldH192(sfMPTokenIssuanceID, mptIssuanceID);
    sb.insert(binSle);

    auto const page =
        sb.dirInsert(keylet::ownerDir(ammAccountID), binKeylet, describeOwnerDir(ammAccountID));
    if (!page)
        return tecDIR_FULL;
    (*binSle)[sfOwnerNode] = *page;
    sb.update(binSle);

    sb.apply(ctx_.rawView());
    return tesSUCCESS;
}

void
AMMBinCreate::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
}

bool
AMMBinCreate::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl

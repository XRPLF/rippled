// Transfer ownership of a CL position SLE from one account to another.
// The position keylet itself does not change (it's content-addressed by
// the original creator); only sfAccount, owner-directory membership, and
// the source/destination owner-reserve counters change.

#include <xrpl/tx/transactors/dex/AMMPositionTransfer.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
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
AMMPositionTransfer::checkExtraFeatures(PreflightContext const& ctx)
{
    return ctx.rules.enabled(featureAMMCurves);
}

NotTEC
AMMPositionTransfer::preflight(PreflightContext const& ctx)
{
    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    if (!ctx.tx.isFieldPresent(sfPositionID))
        return temMALFORMED;

    if (!ctx.tx.isFieldPresent(sfDestination))
        return temMALFORMED;

    auto const src = ctx.tx[sfAccount];
    auto const dst = ctx.tx[sfDestination];
    if (src == dst)
        return temREDUNDANT;

    return tesSUCCESS;
}

TER
AMMPositionTransfer::preclaim(PreclaimContext const& ctx)
{
    auto const src = ctx.tx[sfAccount];
    auto const dst = ctx.tx[sfDestination];
    auto const positionID = ctx.tx[sfPositionID];

    auto const posSle = ctx.view.read(keylet::ammPosition(positionID));
    if (!posSle || posSle->getType() != ltAMM_POSITION)
        return tecNO_ENTRY;

    if ((*posSle)[sfAccount] != src)
        return tecNO_PERMISSION;

    auto const sleDst = ctx.view.read(keylet::account(dst));
    if (!sleDst)
        return tecNO_DST;

    // DepositAuth: destination can require source to be pre-authorized
    // before receiving anything that affects its owner directory.
    if (sleDst->isFlag(lsfDepositAuth))
    {
        if (!ctx.view.exists(keylet::depositPreauth(dst, src)))
            return tecNO_PERMISSION;
    }

    return tesSUCCESS;
}

TER
AMMPositionTransfer::doApply()
{
    Sandbox sb(&ctx_.view());

    auto const src = ctx_.tx[sfAccount];
    auto const dst = ctx_.tx[sfDestination];
    auto const positionID = ctx_.tx[sfPositionID];

    auto posSle = sb.peek(keylet::ammPosition(positionID));
    if (!posSle || posSle->getType() != ltAMM_POSITION)
        return tecNO_ENTRY;
    if ((*posSle)[sfAccount] != src)
        return tecNO_PERMISSION;

    auto sleDst = sb.peek(keylet::account(dst));
    if (!sleDst)
        return tecNO_DST;

    // Reserve check on destination before mutating anything: incrementing
    // owner count must not push the destination below its reserve threshold.
    {
        auto const ownerCount = sleDst->getFieldU32(sfOwnerCount);
        auto const newReserve =
            sb.fees().accountReserve(static_cast<std::uint32_t>(ownerCount) + 1, 1);
        auto const dstBalance = sleDst->getFieldAmount(sfBalance).xrp();
        if (dstBalance < newReserve)
            return tecINSUFFICIENT_RESERVE;
    }

    // Remove from source owner directory using the stored page.
    auto const srcOwnerDirKeylet = keylet::ownerDir(src);
    auto const srcOwnerNode = posSle->getFieldU64(sfOwnerNode);
    if (!sb.dirRemove(srcOwnerDirKeylet, srcOwnerNode, posSle->key(), true))
    {
        JLOG(j_.error()) << "AMMPositionTransfer: failed to remove position "
                            "from source owner directory.";
        return tecINTERNAL;
    }

    // Insert into destination owner directory.
    auto const dstPage = sb.dirInsert(
        keylet::ownerDir(dst), posSle->key(), describeOwnerDir(dst));
    if (!dstPage)
        return tecDIR_FULL;

    // Update SLE: new owner, new directory page.
    (*posSle)[sfAccount] = dst;
    (*posSle)[sfOwnerNode] = *dstPage;
    sb.update(posSle);

    // Owner count adjustments. Reserve was already validated above.
    decreaseOwnerCount(sb, src, std::nullopt, 1, ctx_.journal);
    increaseOwnerCount(sb, sleDst, SLE::pointer(), 1, ctx_.journal);

    sb.apply(ctx_.rawView());
    return tesSUCCESS;
}

void
AMMPositionTransfer::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
}

bool
AMMPositionTransfer::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl

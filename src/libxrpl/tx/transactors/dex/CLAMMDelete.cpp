#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/CLAMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/tx/transactors/dex/CLAMMDelete.h>

namespace xrpl {

namespace {

// Maximum directory entries to delete per CLAMMDelete transaction.
// Covers ticks, bitmaps, trust lines owned by the pool pseudo-account.
constexpr std::uint16_t maxDeletableCLAMMEntries = 512;

}  // namespace

bool
CLAMMDelete::checkExtraFeatures(PreflightContext const& ctx)
{
    return clammEnabled(ctx.rules);
}

NotTEC
CLAMMDelete::preflight(PreflightContext const& ctx)
{
    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    return tesSUCCESS;
}

TER
CLAMMDelete::preclaim(PreclaimContext const& ctx)
{
    auto const& asset = ctx.tx[sfAsset].get<Issue>();
    auto const& asset2 = ctx.tx[sfAsset2].get<Issue>();
    auto const feeTier = ctx.tx[sfFeeTier];

    if (!isValidCLAMMFeeTier(feeTier))
    {
        JLOG(ctx.j.debug()) << "CLAMM Delete: invalid fee tier.";
        return temBAD_FEE;
    }

    auto const sleClamm =
        ctx.view.read(keylet::clamm(asset, asset2, feeTier));
    if (!sleClamm)
    {
        JLOG(ctx.j.debug()) << "CLAMM Delete: pool not found.";
        return tecNO_ENTRY;
    }

    // Pool must have zero active liquidity
    if (sleClamm->isFieldPresent(sfLiquidityAmount))
    {
        auto const liq = sleClamm->getFieldH128(sfLiquidityAmount);
        if (liq != base_uint<128>{})
        {
            JLOG(ctx.j.debug()) << "CLAMM Delete: pool not empty.";
            return tecAMM_NOT_EMPTY;
        }
    }

    // Check for out-of-range positions: scan pool directory for ticks
    // with non-zero liquidityGross (indicating positions still reference them).
    auto const ammAccountID = sleClamm->getAccountID(sfAccount);
    bool hasPositions = false;
    forEachItem(
        ctx.view,
        ammAccountID,
        [&](std::shared_ptr<SLE const> const& sle) {
            if (hasPositions)
                return;
            if (sle->getType() == ltCLAMM_TICK)
            {
                auto const gross =
                    sle->getFieldH128(sfLiquidityGross);
                if (gross != base_uint<128>{})
                    hasPositions = true;
            }
        });
    if (hasPositions)
    {
        JLOG(ctx.j.debug())
            << "CLAMM Delete: pool has outstanding positions.";
        return tecAMM_NOT_EMPTY;
    }

    return tesSUCCESS;
}

TER
CLAMMDelete::doApply()
{
    auto const& asset = ctx_.tx[sfAsset].get<Issue>();
    auto const& asset2 = ctx_.tx[sfAsset2].get<Issue>();
    auto const feeTier = ctx_.tx[sfFeeTier];

    Sandbox sb(&ctx_.view());

    auto const clammKeylet = keylet::clamm(asset, asset2, feeTier);
    auto sleClamm = sb.peek(clammKeylet);
    if (!sleClamm)
        return tecNO_ENTRY;

    auto const ammAccountID = sleClamm->getAccountID(sfAccount);
    auto sleAMMRoot = sb.peek(keylet::account(ammAccountID));
    if (!sleAMMRoot)
    {
        JLOG(j_.error())
            << "CLAMM Delete: pool pseudo-account not found.";
        return tecINTERNAL;
    }

    // Use cleanupOnAccountDelete to iterate the pseudo-account's
    // owner directory and delete ticks, bitmaps, and trust lines.
    auto const ownerDirKeylet = keylet::ownerDir(ammAccountID);
    auto const ter = cleanupOnAccountDelete(
        sb,
        ownerDirKeylet,
        [&](LedgerEntryType nodeType,
            uint256 const&,
            std::shared_ptr<SLE>& sleItem) -> std::pair<TER, SkipEntry> {
            // Skip the CLAMM pool SLE itself (deleted separately)
            if (nodeType == ltCLAMM)
                return {tesSUCCESS, SkipEntry::Yes};

            // Ticks and bitmaps: just erase
            if (nodeType == ltCLAMM_TICK ||
                nodeType == ltCLAMM_TICK_BITMAP)
            {
                return {tesSUCCESS, SkipEntry::No};
            }

            // Trust lines: use the AMM trust line deletion helper
            if (nodeType == ltRIPPLE_STATE)
            {
                if (sleItem->getFieldAmount(sfBalance) != beast::zero)
                {
                    JLOG(j_.error())
                        << "CLAMM Delete: trust line has non-zero balance.";
                    return {tecINTERNAL, SkipEntry::No};
                }
                return {
                    deleteAMMTrustLine(sb, sleItem, ammAccountID, j_),
                    SkipEntry::No};
            }

            // Unexpected entry type
            JLOG(j_.warn())
                << "CLAMM Delete: unexpected entry type "
                << static_cast<int>(nodeType);
            return {tesSUCCESS, SkipEntry::No};
        },
        j_,
        maxDeletableCLAMMEntries);

    if (ter != tesSUCCESS && ter != tecINCOMPLETE)
        return ter;

    if (ter == tecINCOMPLETE)
    {
        // Not all entries deleted yet -- commit progress and return
        sb.apply(ctx_.rawView());
        return tecINCOMPLETE;
    }

    // All directory entries cleared. Now remove the pool SLE and account.
    // The CLAMM SLE is in the pseudo-account's owner directory (inserted
    // there by CLAMMCreate). sfOwnerNode stores the page within that directory.
    auto const ownerNode = sleClamm->getFieldU64(sfOwnerNode);
    if (!sb.dirRemove(
            ownerDirKeylet, ownerNode, sleClamm->key(), false))
    {
        JLOG(j_.debug())
            << "CLAMM Delete: dir remove failed (may be already empty).";
    }

    if (sb.exists(ownerDirKeylet) && !sb.emptyDirDelete(ownerDirKeylet))
    {
        JLOG(j_.error())
            << "CLAMM Delete: cannot delete root dir node of pool.";
        return tecINTERNAL;
    }

    sb.erase(sleClamm);
    sb.erase(sleAMMRoot);

    sb.apply(ctx_.rawView());
    return tesSUCCESS;
}

}  // namespace xrpl

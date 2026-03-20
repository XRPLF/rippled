#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/CLAMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/tx/transactors/dex/CLAMMHelpers.h>
#include <xrpl/tx/transactors/dex/CLAMMVote.h>

namespace xrpl {

namespace {

// Compute the total liquidity a given account has in a specific CLAMM pool
// by scanning their owner directory for CLAMMPosition entries.
clamm::uint128
computeAccountLiquidity(
    ReadView const& view,
    AccountID const& account,
    uint256 const& poolID)
{
    clamm::uint128 totalLiquidity = 0;
    forEachItem(view, account, [&](std::shared_ptr<SLE const> const& sle) {
        if (sle->getType() == ltCLAMM_POSITION &&
            sle->getFieldH256(sfPoolID) == poolID)
        {
            totalLiquidity +=
                clamm::fromSLEField(sle->getFieldH128(sfLiquidityAmount));
        }
    });
    return totalLiquidity;
}

}  // namespace

bool
CLAMMVote::checkExtraFeatures(PreflightContext const& ctx)
{
    return clammEnabled(ctx.rules);
}

NotTEC
CLAMMVote::preflight(PreflightContext const& ctx)
{
    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    if (ctx.tx[sfTradingFee] > 10000)
    {
        JLOG(ctx.j.debug()) << "CLAMM Vote: invalid trading fee.";
        return temBAD_FEE;
    }

    // Validate DiscountedFee if present
    if (ctx.tx.isFieldPresent(sfDiscountedFee))
    {
        auto const discountedFee = ctx.tx[sfDiscountedFee];
        if (discountedFee >= ctx.tx[sfTradingFee])
        {
            JLOG(ctx.j.debug())
                << "CLAMM Vote: discounted fee exceeds trading fee.";
            return temBAD_FEE;
        }
    }
    return tesSUCCESS;
}

TER
CLAMMVote::preclaim(PreclaimContext const& ctx)
{
    if (auto const poolID = resolvePoolID(ctx.tx))
    {
        if (!ctx.view.read(keylet::clamm(*poolID)))
        {
            JLOG(ctx.j.debug()) << "CLAMM Vote: pool not found.";
            return tecNO_ENTRY;
        }
    }
    return tesSUCCESS;
}

TER
CLAMMVote::doApply()
{
    auto const account = ctx_.tx[sfAccount];
    auto const optPoolID = resolvePoolID(ctx_.tx);
    if (!optPoolID)
    {
        JLOG(j_.debug()) << "CLAMM Vote: no pool identifier provided.";
        return temMALFORMED;
    }
    auto const poolID = *optPoolID;
    auto const feeNew = ctx_.tx[sfTradingFee];

    Sandbox sb(&ctx_.view());

    auto const clammKeylet = keylet::clamm(poolID);
    auto sleClamm = sb.peek(clammKeylet);
    if (!sleClamm)
        return tecNO_ENTRY;

    // Enforce per-tier fee bounds
    auto const feeTier = sleClamm->getFieldU8(sfFeeTier);
    auto const maxFee = clammFeeTiers[feeTier].tradingFee;
    if (feeNew > maxFee)
    {
        JLOG(j_.debug())
            << "CLAMM Vote: fee exceeds tier maximum (" << maxFee << ").";
        return tecNO_PERMISSION;
    }

    // Compute the voter's total liquidity in this pool.
    // Vote weight is proportional to liquidity provided.
    auto const voterLiquidity = computeAccountLiquidity(sb, account, poolID);
    if (voterLiquidity == 0)
    {
        JLOG(j_.debug())
            << "CLAMM Vote: account has no liquidity in this pool.";
        return tecNO_PERMISSION;
    }

    // Use full liquidity (as int64) for weighted average computation.
    // The stored sfVoteWeight is capped to uint32 for display/eviction only.
    auto const liquidityNew = static_cast<std::int64_t>(std::min<clamm::uint128>(
        voterLiquidity,
        clamm::uint128(std::numeric_limits<std::int64_t>::max())));
    auto const weightNew = static_cast<std::uint32_t>(std::min<clamm::uint128>(
        voterLiquidity,
        clamm::uint128(std::numeric_limits<std::uint32_t>::max())));

    STArray updatedVoteSlots;
    Number num{0};
    Number den{0};
    bool foundAccount = false;

    // Track minimum for eviction
    std::optional<std::uint32_t> minWeight;
    std::size_t minPos = 0;
    AccountID minAccount;
    std::uint16_t minFee = 0;

    if (sleClamm->isFieldPresent(sfVoteSlots))
    {
        for (auto const& entry : sleClamm->getFieldArray(sfVoteSlots))
        {
            auto const entryAccount = entry[sfAccount];
            auto entryFee = entry[~sfTradingFee].value_or(0);

            // Recompute each voter's liquidity and capped weight
            auto const entryLiquidity =
                computeAccountLiquidity(sb, entryAccount, poolID);
            // Full-precision liquidity for weighted average
            auto const entryLiq = static_cast<std::int64_t>(
                std::min<clamm::uint128>(
                    entryLiquidity,
                    clamm::uint128(
                        std::numeric_limits<std::int64_t>::max())));
            // Capped weight for stored field and eviction
            auto const weight =
                static_cast<std::uint32_t>(std::min<clamm::uint128>(
                    entryLiquidity,
                    clamm::uint128(
                        std::numeric_limits<std::uint32_t>::max())));

            // Skip voters who no longer have liquidity
            if (entryLiq == 0 && entryAccount != account)
                continue;

            if (entryAccount == account)
            {
                entryFee = feeNew;
                foundAccount = true;
                // Use full-precision liquidity for weighted average
                num += Number(entryFee) * liquidityNew;
                den += liquidityNew;

                STObject newEntry = STObject::makeInnerObject(sfVoteEntry);
                newEntry.setAccountID(sfAccount, entryAccount);
                if (entryFee != 0)
                    newEntry.setFieldU16(sfTradingFee, entryFee);
                newEntry.setFieldU32(sfVoteWeight, weightNew);
                updatedVoteSlots.push_back(std::move(newEntry));
            }
            else
            {
                num += Number(entryFee) * entryLiq;
                den += entryLiq;

                STObject newEntry = STObject::makeInnerObject(sfVoteEntry);
                newEntry.setAccountID(sfAccount, entryAccount);
                if (entryFee != 0)
                    newEntry.setFieldU16(sfTradingFee, entryFee);
                newEntry.setFieldU32(sfVoteWeight, weight);
                updatedVoteSlots.push_back(std::move(newEntry));
            }

            auto const w = (entryAccount == account) ? weightNew : weight;
            auto const f = entryFee;
            if (!minWeight ||
                (w < *minWeight ||
                 (w == *minWeight &&
                  (f < minFee ||
                   (f == minFee && entryAccount < minAccount)))))
            {
                minWeight = w;
                minPos = updatedVoteSlots.size() - 1;
                minAccount = entryAccount;
                minFee = f;
            }
        }
    }

    if (!foundAccount)
    {
        auto addVote = [&](std::optional<std::size_t> pos = std::nullopt) {
            STObject newEntry = STObject::makeInnerObject(sfVoteEntry);
            newEntry.setAccountID(sfAccount, account);
            if (feeNew != 0)
                newEntry.setFieldU16(sfTradingFee, feeNew);
            newEntry.setFieldU32(sfVoteWeight, weightNew);

            if (pos)
            {
                // Replace evicted entry -- subtract its full-precision
                // liquidity contribution from the weighted average.
                auto const& evicted = updatedVoteSlots[*pos];
                auto const evictedFee =
                    evicted[~sfTradingFee].value_or(0);
                auto const evictedLiq =
                    computeAccountLiquidity(
                        sb, evicted[sfAccount], poolID);
                auto const evictedLiqNum = static_cast<std::int64_t>(
                    std::min<clamm::uint128>(
                        evictedLiq,
                        clamm::uint128(
                            std::numeric_limits<std::int64_t>::max())));
                num -= Number(evictedFee) * evictedLiqNum;
                den -= evictedLiqNum;
                updatedVoteSlots[*pos] = std::move(newEntry);
            }
            else
            {
                updatedVoteSlots.push_back(std::move(newEntry));
            }
            num += Number(feeNew) * liquidityNew;
            den += liquidityNew;
        };

        if (updatedVoteSlots.size() < CLAMM_VOTE_MAX_SLOTS)
        {
            addVote();
        }
        else if (minWeight && weightNew > *minWeight)
        {
            addVote(minPos);
        }
        else
        {
            // Cannot evict anyone — vote rejected
            return tecNO_PERMISSION;
        }
    }

    sleClamm->setFieldArray(sfVoteSlots, updatedVoteSlots);

    // Update trading fee as weighted average
    if (den != Number(0))
    {
        auto const fee = static_cast<std::uint16_t>(
            static_cast<std::int64_t>(num / den));
        sleClamm->setFieldU16(sfTradingFee, fee);

        // Sync auction slot discounted fee
        if (sleClamm->isFieldPresent(sfAuctionSlot))
        {
            auto& auctionSlot = sleClamm->peekFieldObject(sfAuctionSlot);
            if (auto const discountedFee = fee / 10)
                auctionSlot.setFieldU16(sfDiscountedFee, discountedFee);
            else if (auctionSlot.isFieldPresent(sfDiscountedFee))
                auctionSlot.makeFieldAbsent(sfDiscountedFee);
        }
    }

    sleClamm->setFieldH256(sfPreviousTxnID, ctx_.tx.getTransactionID());
    sleClamm->setFieldU32(sfPreviousTxnLgrSeq, ctx_.view().seq());
    sb.update(sleClamm);

    sb.apply(ctx_.rawView());
    return tesSUCCESS;
}

}  // namespace xrpl

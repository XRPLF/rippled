#include <xrpld/app/paths/CLAMMLiquidity.h>
#include <xrpld/app/paths/CLAMMOffer.h>

#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>

namespace xrpl {

template <typename TIn, typename TOut>
CLAMMLiquidity<TIn, TOut>::CLAMMLiquidity(
    ReadView const& view,
    Issue const& in,
    Issue const& out,
    CLAMMContext& clammContext,
    beast::Journal j)
    : clammContext_(clammContext), issueIn_(in), issueOut_(out), j_(j)
{
    // Discover pools across all fee tiers.
    // keylet::clamm handles canonical ordering internally,
    // and Asset has implicit conversion from Issue.
    for (std::uint8_t tier = 0;
         tier < static_cast<std::uint8_t>(clammFeeTiers.size()); ++tier)
    {
        auto const clammKeylet = keylet::clamm(
            Asset(in), Asset(out), tier);

        if (auto sle = view.read(clammKeylet))
        {
            auto const& ref = std::as_const(*sle);
            auto const issue0 = ref[sfAsset].get<Issue>();
            auto const liquidity =
                clamm::fromSLEField(sle->getFieldH128(sfLiquidityAmount));

            if (liquidity == 0)
                continue;

            CLAMMPoolInfo pool;
            pool.poolID = clammKeylet.key;
            pool.ammAccountID = sle->getAccountID(sfAccount);
            pool.feeTier = tier;
            pool.tradingFee = sle->getFieldU16(sfTradingFee);
            pool.tickSpacing = sle->getFieldU16(sfTickSpacing);
            pool.sqrtPrice =
                clamm::fromSLEField(sle->getFieldH128(sfSqrtPrice));
            pool.currentTick = sle->getFieldI32(sfCurrentTick);
            pool.liquidity = liquidity;
            pool.feeGrowthGlobal0 =
                clamm::fromSLEField(sle->getFieldH128(sfFeeGrowthGlobal0));
            pool.feeGrowthGlobal1 =
                clamm::fromSLEField(sle->getFieldH128(sfFeeGrowthGlobal1));
            pool.zeroForOne = (in == issue0);

            pools_.push_back(std::move(pool));
        }
    }
}

template <typename TIn, typename TOut>
std::optional<CLAMMPoolInfo>
CLAMMLiquidity<TIn, TOut>::refreshPool(
    ReadView const& view,
    CLAMMPoolInfo const& pool) const
{
    auto sle = view.read(keylet::clamm(pool.poolID));
    if (!sle)
        return std::nullopt;

    auto const liquidity =
        clamm::fromSLEField(sle->getFieldH128(sfLiquidityAmount));
    if (liquidity == 0)
        return std::nullopt;

    CLAMMPoolInfo updated = pool;
    updated.sqrtPrice =
        clamm::fromSLEField(sle->getFieldH128(sfSqrtPrice));
    updated.currentTick = sle->getFieldI32(sfCurrentTick);
    updated.liquidity = liquidity;
    updated.tradingFee = sle->getFieldU16(sfTradingFee);
    updated.feeGrowthGlobal0 =
        clamm::fromSLEField(sle->getFieldH128(sfFeeGrowthGlobal0));
    updated.feeGrowthGlobal1 =
        clamm::fromSLEField(sle->getFieldH128(sfFeeGrowthGlobal1));
    return updated;
}

template <typename TIn, typename TOut>
Quality
CLAMMLiquidity<TIn, TOut>::poolQuality(CLAMMPoolInfo const& pool) const
{
    // Approximate spot quality from sqrtPrice.
    // sqrtPrice is Q64.96, so sqrtPrice^2 >> 192 gives the price ratio.
    auto const sqrtPrice256 = clamm::uint256(pool.sqrtPrice);
    auto const priceSq = sqrtPrice256 * sqrtPrice256;
    auto const scale = clamm::uint256(1) << 192;

    // Apply fee discount
    auto const feeMultiplier = clamm::uint256(1000000 - pool.tradingFee);
    auto const feeDenom = clamm::uint256(1000000);

    // Quality = out/in. Higher is better.
    // For zeroForOne: out = token1, in = token0. Rate ~ sqrtPrice^2 / 2^192
    // For oneForZero: out = token0, in = token1. Rate ~ 2^192 / sqrtPrice^2
    if (pool.zeroForOne)
    {
        auto const effectiveRate =
            (priceSq * feeMultiplier) / (scale * feeDenom);
        auto const outVal = std::max(
            static_cast<std::uint64_t>(effectiveRate), std::uint64_t(1));
        return Quality(Amounts(
            STAmount(issueIn_, 1, 0),
            STAmount(issueOut_, outVal, 0)));
    }
    else
    {
        auto const effectiveRate =
            (scale * feeMultiplier) / (priceSq * feeDenom);
        auto const outVal = std::max(
            static_cast<std::uint64_t>(effectiveRate), std::uint64_t(1));
        return Quality(Amounts(
            STAmount(issueIn_, 1, 0),
            STAmount(issueOut_, outVal, 0)));
    }
}

template <typename TIn, typename TOut>
std::optional<CLAMMOffer<TIn, TOut>>
CLAMMLiquidity<TIn, TOut>::getOffer(
    ReadView const& view,
    std::optional<Quality> const& clobQuality) const
{
    if (clammContext_.maxItersReached())
        return std::nullopt;

    if (pools_.empty())
        return std::nullopt;

    // Find the best pool by simulating a probe swap through each.
    constexpr std::uint64_t probeAmount = 10000;

    std::optional<CLAMMPoolInfo> bestPool;
    Quality bestQuality;

    for (auto const& pool : pools_)
    {
        auto refreshed = refreshPool(view, pool);
        if (!refreshed)
            continue;

        auto const sim = clamm::simulateSwap(
            view,
            refreshed->poolID,
            refreshed->sqrtPrice,
            refreshed->currentTick,
            refreshed->liquidity,
            refreshed->tickSpacing,
            refreshed->tradingFee,
            probeAmount,
            refreshed->zeroForOne);

        if (sim.amountOut == 0)
            continue;

        Quality const q(Amounts(
            STAmount(issueIn_, sim.amountIn, 0),
            STAmount(issueOut_, sim.amountOut, 0)));

        if (clobQuality && q < *clobQuality)
            continue;

        if (!bestPool || q > bestQuality)
        {
            bestPool = refreshed;
            bestQuality = q;
        }
    }

    if (!bestPool)
        return std::nullopt;

    // Determine offer size based on payment mode.
    std::uint64_t offerAmountIn;
    if (clammContext_.multiPath())
    {
        // Fibonacci-like sizing (matches AMMContext pattern)
        constexpr std::uint32_t fib[] = {
            1,      1,      2,      3,      5,      8,
            13,     21,     34,     55,     89,     144,
            233,    377,    610,    987,    1597,   2584,
            4181,   6765,   10946,  17711,  28657,  46368,
            75025,  121393, 196418, 317811, 514229, 832040};
        auto const iter = clammContext_.curIters();
        auto const idx = std::min(
            static_cast<std::size_t>(iter),
            std::size(fib) - 1);
        offerAmountIn = probeAmount * fib[idx];
    }
    else if (clobQuality)
    {
        // Binary search for the input amount that brings post-swap
        // quality to match CLOB quality.
        std::uint64_t lo = 1, hi = 1000000000ULL;
        while (lo < hi)
        {
            auto const mid = lo + (hi - lo + 1) / 2;
            auto const sim = clamm::simulateSwap(
                view,
                bestPool->poolID,
                bestPool->sqrtPrice,
                bestPool->currentTick,
                bestPool->liquidity,
                bestPool->tickSpacing,
                bestPool->tradingFee,
                mid,
                bestPool->zeroForOne);
            if (sim.amountOut == 0)
            {
                hi = mid - 1;
                continue;
            }
            Quality const q(Amounts(
                STAmount(issueIn_, sim.amountIn, 0),
                STAmount(issueOut_, sim.amountOut, 0)));
            if (q >= *clobQuality)
                lo = mid;
            else
                hi = mid - 1;
        }
        offerAmountIn = lo;
    }
    else
    {
        // No CLOB competition -- max offer
        offerAmountIn = 1000000000ULL;
    }

    // Simulate final swap with computed input amount
    auto const finalSim = clamm::simulateSwap(
        view,
        bestPool->poolID,
        bestPool->sqrtPrice,
        bestPool->currentTick,
        bestPool->liquidity,
        bestPool->tickSpacing,
        bestPool->tradingFee,
        offerAmountIn,
        bestPool->zeroForOne);

    if (finalSim.amountOut == 0 || finalSim.amountIn == 0)
        return std::nullopt;

    auto const offerIn = clamm::makeSTAmount(issueIn_, finalSim.amountIn);
    auto const offerOut =
        clamm::makeSTAmount(issueOut_, finalSim.amountOut);

    TAmounts<TIn, TOut> amounts{
        toAmount<TIn>(offerIn), toAmount<TOut>(offerOut)};

    Quality const offerQuality(Amounts(offerIn, offerOut));

    return CLAMMOffer<TIn, TOut>(
        *this, *bestPool, amounts, offerQuality, j_);
}

template class CLAMMLiquidity<STAmount, STAmount>;
template class CLAMMLiquidity<IOUAmount, IOUAmount>;
template class CLAMMLiquidity<XRPAmount, IOUAmount>;
template class CLAMMLiquidity<IOUAmount, XRPAmount>;

}  // namespace xrpl

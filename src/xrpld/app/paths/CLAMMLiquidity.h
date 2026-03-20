#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/CLAMMCore.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/tx/transactors/dex/CLAMMContext.h>
#include <xrpl/tx/transactors/dex/CLAMMHelpers.h>

#include <optional>
#include <vector>

namespace xrpl {

template <typename TIn, typename TOut>
class CLAMMOffer;

/** Information about a single CLAMM pool (one fee tier). */
struct CLAMMPoolInfo
{
    uint256 poolID;
    AccountID ammAccountID;
    std::uint8_t feeTier;
    std::uint16_t tradingFee;
    std::uint16_t tickSpacing;
    clamm::uint128 sqrtPrice;
    std::int32_t currentTick;
    clamm::uint128 liquidity;
    clamm::uint128 feeGrowthGlobal0;
    clamm::uint128 feeGrowthGlobal1;
    bool zeroForOne;
};

/** CLAMMLiquidity class provides CLAMM offers to BookStep class.
 * Discovers up to 4 CLAMM pools (one per fee tier) for the given
 * asset pair and generates synthetic offers via swap simulation.
 * The offer with the best effective quality is selected.
 */
template <typename TIn, typename TOut>
class CLAMMLiquidity
{
private:
    CLAMMContext& clammContext_;
    Issue const issueIn_;
    Issue const issueOut_;
    std::vector<CLAMMPoolInfo> pools_;
    beast::Journal const j_;

public:
    CLAMMLiquidity(
        ReadView const& view,
        Issue const& in,
        Issue const& out,
        CLAMMContext& clammContext,
        beast::Journal j);
    ~CLAMMLiquidity() = default;
    CLAMMLiquidity(CLAMMLiquidity const&) = delete;
    CLAMMLiquidity&
    operator=(CLAMMLiquidity const&) = delete;
    CLAMMLiquidity(CLAMMLiquidity&&) = default;

    /** Generate CLAMM offer from the best pool.
     * Returns nullopt if clobQuality is provided and all pools have
     * worse quality, or if max iterations reached.
     */
    std::optional<CLAMMOffer<TIn, TOut>>
    getOffer(
        ReadView const& view,
        std::optional<Quality> const& clobQuality) const;

    /** Returns true if no pools were discovered. */
    bool
    empty() const
    {
        return pools_.empty();
    }

    bool
    multiPath() const
    {
        return clammContext_.multiPath();
    }

    CLAMMContext&
    context() const
    {
        return clammContext_;
    }

    Issue const&
    issueIn() const
    {
        return issueIn_;
    }

    Issue const&
    issueOut() const
    {
        return issueOut_;
    }

private:
    /** Refresh pool state from the current view. */
    std::optional<CLAMMPoolInfo>
    refreshPool(ReadView const& view, CLAMMPoolInfo const& pool) const;

    /** Compute effective quality for a pool. */
    Quality
    poolQuality(CLAMMPoolInfo const& pool) const;
};

}  // namespace xrpl

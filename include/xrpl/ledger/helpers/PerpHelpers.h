#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TER.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>

namespace xrpl::perp {

/**
 * Work bounds and protocol constants of the PerpetualFutures amendment.
 * Rates are in tenth-bips: 100 000 is one.
 */
constexpr std::uint32_t kRateUnity = 100'000;
constexpr std::size_t kMaxPerpOracles = 5;
constexpr std::size_t kMaxMarginTiers = 3;
constexpr std::size_t kMaxMarginPositions = 16;
constexpr std::size_t kMaxPerpMarkets = 64;
constexpr std::size_t kMaxPerpFills = 64;
constexpr std::uint32_t kLiquidationChunk = 20'000;
constexpr std::uint32_t kMaxTradingFee = 10'000;
constexpr std::uint32_t kFundingInterestClamp = 50;
constexpr std::uint32_t kMaxFundingCap = 4'000;
constexpr std::chrono::seconds kMinParameterDelay{86'400};
constexpr std::chrono::seconds kMinSettleStaleAfter{3'600};
constexpr std::chrono::seconds kSettlementGrace{86'400};

/**
 * Significant digits a book quality key holds, and so the most an order price may carry.
 */
constexpr int kMaxPriceDigits = 16;

/**
 * Whether a positive price has at most kMaxPriceDigits significant digits.
 */
[[nodiscard]] inline bool
fitsQualityKey(Number const& price)
{
    auto mantissa = price.mantissa();
    if (mantissa < 0)
        mantissa = -mantissa;
    while (mantissa != 0 && mantissa % 10 == 0)
        mantissa /= 10;
    int digits = 0;
    for (; mantissa != 0; mantissa /= 10)
        ++digits;
    return digits <= kMaxPriceDigits;
}

/**
 * Seconds of funding one sfFundingRate covers: rates are quoted per 8 hours.
 */
constexpr std::uint32_t kFundingInterval = 28'800;

/**
 * Values of a market's sfMarketStatus.
 */
constexpr std::uint8_t kMarketLive = 0;
constexpr std::uint8_t kMarketStale = 1;
constexpr std::uint8_t kMarketSettled = 2;

/**
 * Unix time of the view's parent close, the time t of every price computation.
 */
[[nodiscard]] std::uint32_t
refreshTime(ReadView const& view);

/**
 * The value of one index oracle's quote for the market's pair, when it is fresh (spec 5.1).
 *
 * @param view The view the oracle is read from.
 * @param market The ltPERP_MARKET entry, for its pair and freshness bounds.
 * @param indexOracle One sfIndexOracle entry of the market's sfIndexOracles.
 * @return The positive quote, or nullopt when the oracle is missing, has no price for the
 *         pair, or is older than sfIndexMaxLedgers ledgers or sfIndexMaxAge seconds.
 */
[[nodiscard]] std::optional<Number>
freshQuote(ReadView const& view, STLedgerEntry const& market, STObject const& indexOracle);

/**
 * The median of the market's fresh quotes (spec 5.2), rounded to nearest.
 *
 * @return The index, or tecPERP_STALE_PRICE when fewer than a majority of the oracles are fresh.
 */
[[nodiscard]] std::expected<Number, TER>
computeIndex(ReadView const& view, STLedgerEntry const& market);

/**
 * Inputs of one mark computation (spec 5.3). Rates are in tenth-bips.
 */
struct MarkInput
{
    Number index;
    /**
     * The mark of the previous refresh; zero before the first, which skips the move cap.
     */
    Number prevMark;
    Number basisEMA;
    /**
     * Seconds since the previous refresh.
     */
    std::uint32_t elapsed{};
    std::optional<Number> bid;
    std::optional<Number> ask;
    std::uint32_t basisWindow{};
    std::uint32_t midSpreadMax{};
    std::uint32_t markMoveMax{};
    std::uint32_t markBand{};
};

/**
 * The outputs of a mark computation.
 */
struct MarkResult
{
    Number mark;
    Number basisEMA;
};

/**
 * The basis EMA and the mark: the median of index, index plus basis and the book mid, moved at
 * most sfMarkMoveMax from the previous mark and then held within sfMarkBand of the index
 * (spec 5.3). Rounds to nearest.
 */
[[nodiscard]] MarkResult
computeMark(MarkInput const& input);

/**
 * The 8-hour funding rate: premium plus the clamped interest term, capped (spec 6.1). Rounds to
 * nearest.
 *
 * @param mark The new mark.
 * @param index The new index, positive.
 * @param interestRate sfFundingInterestRate in tenth-bips, signed.
 * @param fundingCap sfFundingCap in tenth-bips.
 */
[[nodiscard]] Number
computeFundingRate(
    Number const& mark,
    Number const& index,
    std::int32_t interestRate,
    std::uint32_t fundingCap);

/**
 * The increase of sfCumulativeFunding over elapsed seconds at rate and index, with elapsed
 * capped at kFundingInterval (spec 5.4 step 2). Rounds to nearest.
 */
[[nodiscard]] Number
fundingAccrual(Number const& rate, Number const& index, std::uint32_t elapsed);

/**
 * The limit price of the first order in one side of a market's book: the highest bid or the
 * lowest ask.
 */
[[nodiscard]] std::optional<Number>
bestPrice(ReadView const& view, uint256 const& marketID, bool sell);

/**
 * Moves a queued parameter change into the entry once its effective time has come (spec 11).
 *
 * Applies to an ltPERP_EXCHANGE or an ltPERP_MARKET. Each field of sfPendingParameters
 * replaces the entry's field; sfPendingParameters and sfParameterEffectiveTime are then
 * removed. The caller writes the entry back to its view.
 *
 * @return Whether a change was applied.
 */
bool
applyPendingParameters(STLedgerEntry& entry, std::uint32_t now);

/**
 * Refreshes a market once per ledger (spec 5.4 steps 1-7): applies due parameter changes of the
 * market and its exchange, accrues funding while Live, and recomputes the index, mark and
 * funding rate, moving the exchange's sfOpenInterestNotional with the mark. Without index
 * quorum the market turns Stale, unless Settled, and keeps its last prices. Does nothing when
 * the market was already refreshed in this ledger.
 *
 * @param view The view both entries were peeked from.
 * @param market The ltPERP_MARKET entry.
 * @param exchange The market's ltPERP_EXCHANGE entry.
 */
void
refreshMarket(ApplyView& view, SLE::Ref market, SLE::Ref exchange, beast::Journal j);

/**
 * The margin requirement of a notional under a market's tiers, continuous at each boundary
 * (spec 7.2). Rounds upward.
 *
 * @param notional The non-negative position notional.
 * @param tiers sfMarginTiers, ascending in sfNotionalMax.
 * @param initial Whether to use sfInitialMarginRate rather than sfMaintenanceMarginRate.
 * @return The requirement, or nullopt when the notional exceeds the last tier.
 */
[[nodiscard]] std::optional<Number>
tierRequirement(Number const& notional, STArray const& tiers, bool initial);

}  // namespace xrpl::perp

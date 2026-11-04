#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
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
 * The time a change queued at now takes effect: now plus delay, saturating at the largest time.
 */
[[nodiscard]] std::uint32_t
parameterEffectiveTime(std::uint32_t now, std::uint32_t delay);

/**
 * A copy of an ltPERP_EXCHANGE or ltPERP_MARKET with its queued parameter change applied: the
 * parameters the entry holds once the change takes effect.
 */
[[nodiscard]] STLedgerEntry
eventualParameters(STLedgerEntry const& entry);

/**
 * Whether replacing a market's margin tiers loosens them (spec 4.3): the new tiers allow a larger
 * notional, or require less initial or maintenance margin at some notional both allow.
 *
 * @param current sfMarginTiers of the market, ascending in sfNotionalMax.
 * @param proposed The replacement, ascending in sfNotionalMax.
 */
[[nodiscard]] bool
tiersLoosen(STArray const& current, STArray const& proposed);

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

/**
 * A collateral amount on the exchange's grid (spec 16): drops for XRP, units for an MPT and
 * multiples of 10^-sfScale for an IOU.
 *
 * @param exchange The ltPERP_EXCHANGE, for sfAsset and sfScale.
 * @param mode Upward for an amount an account pays, Downward for one it is credited.
 */
[[nodiscard]] Number
roundCollateral(STLedgerEntry const& exchange, Number const& value, Number::RoundingMode mode);

/**
 * Whether an account may act on a permissioned exchange (spec 13), read-only: the domain owner
 * always may, and an expired credential passes so that verifyDomain can delete it.
 *
 * @return tesSUCCESS, or tecNO_AUTH when the domain is missing or the account holds no accepted
 *         credential it lists.
 */
[[nodiscard]] TER
checkDomain(ReadView const& view, uint256 const& domainID, AccountID const& account);

/**
 * The doApply form of checkDomain: deletes the account's expired credentials for the domain.
 *
 * @return tesSUCCESS; tecEXPIRED when only expired credentials were found, which keeps their
 *         deletion; or tecNO_AUTH.
 */
[[nodiscard]] TER
verifyDomain(ApplyView& view, uint256 const& domainID, AccountID const& account, beast::Journal j);

/**
 * The funding and loss charges a position owes since its last settlement (spec 6.3). Each is on
 * the collateral grid, rounded upward; a negative funding charge is paid to the account.
 */
struct SettlementCharge
{
    Number funding;
    Number loss;
};

/**
 * The charges of one position without writing them: the read-only settlement of spec 6.3.
 *
 * @param exchange The position's ltPERP_EXCHANGE, for the collateral grid.
 * @param market The position's ltPERP_MARKET, for its funding and loss indexes.
 * @param position The ltMARGIN_POSITION.
 */
[[nodiscard]] SettlementCharge
settlementCharge(
    STLedgerEntry const& exchange,
    STLedgerEntry const& market,
    STLedgerEntry const& position);

/**
 * Settles a margin account (spec 6.3). For each market of sfMarkets, refreshes the market, moves
 * the position's charges from sfCash and the exchange's sfCashTotal to its sfSettlementBalance,
 * and snapshots the position's funding and loss indexes. When no position has a size, then
 * repays waterfall credit (spec 9.7). A position in a Settled market is settled and stays open.
 *
 * @param view The view the account and exchange were peeked from.
 * @param account The ltMARGIN_ACCOUNT.
 * @param exchange The account's ltPERP_EXCHANGE.
 * @return tesSUCCESS, or tefBAD_LEDGER when a listed market or position is missing.
 */
[[nodiscard]] TER
settleAccount(ApplyView& view, SLE::Ref account, SLE::Ref exchange, beast::Journal j);

/**
 * Repays waterfall credit (spec 9.7): min(sfCash, sfWaterfallCredit) moves from the account's
 * cash to the insurance fund, reducing bad debt first. The account must hold no position.
 */
void
repayWaterfallCredit(ApplyView& view, SLE::Ref account, SLE::Ref exchange);

/**
 * Reduces the exchange's sfBadDebt by up to amount, as every insurance-fund inflow and cover
 * deposit does first (spec 9.6 step 5). The caller writes the entry back.
 *
 * @return The part of amount left after the bad debt.
 */
[[nodiscard]] Number
absorbBadDebt(STLedgerEntry& exchange, Number const& amount);

/**
 * The part of a margin withdrawal kept against bad debt (spec 9.6 step 5): amount times
 * h = min(1, sfBadDebt / sfCashTotal), rounded upward onto the collateral grid and at most
 * sfBadDebt.
 *
 * @return The haircut, zero without bad debt; tecPERP_INSUFFICIENT_COVER when there is bad debt
 *         and sfCashTotal is not positive, so that withdrawals wait.
 */
[[nodiscard]] std::expected<Number, TER>
withdrawalHaircut(STLedgerEntry const& exchange, Number const& amount);

/**
 * The initial and maintenance requirements of a margin account (spec 7.2), rounded upward.
 */
struct MarginRequirements
{
    Number initial;
    Number maintenance;
};

/**
 * A margin account's equity (spec 7.1): sfCash less its unsettled charges plus the unrealized
 * PnL of each position at its market's mark. Rounds downward. Reads only.
 *
 * @return The equity, tecPERP_STALE_PRICE when a position's market is not Live, or
 *         tefBAD_LEDGER when a listed entry is missing.
 */
[[nodiscard]] std::expected<Number, TER>
accountEquity(ReadView const& view, STLedgerEntry const& account);

/**
 * A margin account's requirements (spec 7.2) at each market's mark. The initial requirement
 * counts resting orders at their worst case. A notional above the last tier is charged the last
 * tier's rate. Reads only.
 *
 * @return The requirements, or the errors of accountEquity.
 */
[[nodiscard]] std::expected<MarginRequirements, TER>
accountRequirements(ReadView const& view, STLedgerEntry const& account);

/**
 * The most a margin account may withdraw (spec 7.4): min(cash, cash + sum of negative
 * unrealized PnL - initial requirement), with cash less its unsettled charges, rounded downward
 * onto the collateral grid. Reads only.
 *
 * @return The amount, or the errors of accountEquity.
 */
[[nodiscard]] std::expected<Number, TER>
withdrawable(ReadView const& view, STLedgerEntry const& account);

}  // namespace xrpl::perp

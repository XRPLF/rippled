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
#include <vector>

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
 * Whether value is a whole multiple of a positive step.
 */
[[nodiscard]] bool
isMultiple(Number const& value, Number const& step);

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

/**
 * A margin account's equity and requirements (spec 7.1, 7.2), read together.
 */
struct MarginSummary
{
    Number equity;
    MarginRequirements requirements;
};

/**
 * The equity and requirements of a margin account, as accountEquity and accountRequirements
 * compute them. Reads only.
 *
 * @param view The view the account's markets and positions are read from.
 * @param account The ltMARGIN_ACCOUNT, or a copy of it holding values not yet written.
 * @param position When set, an ltMARGIN_POSITION of the account used instead of the view's entry
 *        for its market, so that a fill can be checked before it is written.
 * @return The summary, or the errors of accountEquity.
 */
[[nodiscard]] std::expected<MarginSummary, TER>
marginSummary(
    ReadView const& view,
    STLedgerEntry const& account,
    STLedgerEntry const* position = nullptr);

/**
 * The price liquidation values a market's positions at (spec 9.1): the settlement price in a
 * Settled market, else the last sfMarkPrice, which a Stale market keeps.
 */
[[nodiscard]] Number
liquidationPrice(STLedgerEntry const& market);

/**
 * The equity and requirements of a margin account for liquidation (spec 9.1): as marginSummary,
 * except that a position in a Stale market is valued at the market's last sfMarkPrice and one in a
 * Settled market at its sfSettlementPrice. Reads only.
 *
 * @return The summary, or tefBAD_LEDGER when a listed entry is missing.
 */
[[nodiscard]] std::expected<MarginSummary, TER>
liquidationSummary(ReadView const& view, STLedgerEntry const& account);

/**
 * The book quality of an order price: the price for an ask and its inverse for a bid, so the best
 * order of either side has the lowest key (spec 3.5).
 */
[[nodiscard]] std::uint64_t
orderQuality(Number const& price, bool sell);

/**
 * Walks one side of a market's book from its best price, and within a price in arrival order.
 *
 * The caller deletes or partly fills each order it is given before stepping again; an order left
 * in the book is given again, as with BookTip.
 */
class PerpBookTip
{
public:
    /**
     * @param view The view the book is read from.
     * @param marketID The ltPERP_MARKET.
     * @param sell The asks when true, the bids when false.
     */
    PerpBookTip(ApplyView& view, uint256 const& marketID, bool sell);

    /**
     * Moves to the first order of the best price level.
     *
     * @return false when the side holds no order.
     */
    bool
    step();

    /**
     * The order step moved to; null when the book lists an order the ledger lacks.
     */
    [[nodiscard]] SLE::pointer const&
    order() const
    {
        return order_;
    }

private:
    ApplyView& view_;
    uint256 book_;
    uint256 end_;
    SLE::pointer order_;
};

/**
 * The fields of an order that rests on the book (spec 3.5).
 */
struct RestingOrder
{
    AccountID account;
    std::uint32_t sequence{};
    uint256 marginAccountID;
    uint256 marketID;
    Number price;
    Number size;
    bool sell{};
    bool reduceOnly{};
    std::optional<std::uint32_t> expiration;
    std::optional<AccountID> builder;
    std::optional<std::uint32_t> builderFee;
};

/**
 * Rests an order: creates its ltPERP_ORDER in the owner directory and at the end of its price
 * level, raises the owner count and the position's and market's sfOrderCount, and counts a
 * non-reduce-only order in sfOpenBuy or sfOpenSell. The caller checks the reserve.
 *
 * @return tesSUCCESS; tecDIR_FULL; or tefBAD_LEDGER when the position, market or owner is
 *         missing.
 */
[[nodiscard]] TER
insertOrder(ApplyView& view, RestingOrder const& order, beast::Journal j);

/**
 * Deletes a resting order, reversing insertOrder, then deletes its position when that holds no
 * size and no other order.
 *
 * @return tesSUCCESS, or tefBAD_LEDGER when a directory, position or market is missing.
 */
[[nodiscard]] TER
removeOrder(ApplyView& view, SLE::Ref order, beast::Journal j);

/**
 * Deletes a position that holds no size and no resting order: removes it from its account's
 * sfMarkets and its owner directory and lowers the owner count. Does nothing to any other position.
 *
 * @return tesSUCCESS, or tefBAD_LEDGER when the account or the directory entry is missing.
 */
[[nodiscard]] TER
deletePositionIfEmpty(ApplyView& view, SLE::Ref position, beast::Journal j);

/**
 * Creates an account's position in a market within the position limits (spec 4.7 step 5): at most
 * kMaxMarginPositions positions, and a position in an isolated-only market only as the account's
 * single position. The caller checks the reserve.
 *
 * @return The position; tecPERP_POSITION_LIMIT; or the error of createPosition.
 */
[[nodiscard]] std::expected<SLE::pointer, TER>
openPosition(ApplyView& view, SLE::Ref account, STLedgerEntry const& market, beast::Journal j);

/**
 * Creates an empty position of a margin account in a market, listed in the account's sfMarkets
 * and in its owner's directory, raising the owner count. The caller checks the reserve and the
 * position limits.
 *
 * @return The position, or the error of the directory insertion.
 */
[[nodiscard]] std::expected<SLE::pointer, TER>
createPosition(ApplyView& view, SLE::Ref account, STLedgerEntry const& market, beast::Journal j);

/**
 * Settles one position (spec 6.3): moves its charges from the account's sfCash and the exchange's
 * sfCashTotal to its sfSettlementBalance and snapshots its indexes.
 */
void
settlePosition(
    ApplyView& view,
    SLE::Ref account,
    SLE::Ref position,
    STLedgerEntry const& market,
    SLE::Ref exchange);

/**
 * Trades a signed quantity at a price into a settled position (spec 7.5), without writing it.
 * Snapshots the funding and loss indexes when the size leaves zero or changes sign.
 *
 * @param position The ltMARGIN_POSITION, changed in place.
 * @param market The position's ltPERP_MARKET, for its indexes.
 * @param exchange The ltPERP_EXCHANGE, for the collateral grid.
 * @param quantity Positive for a buy, negative for a sell.
 * @return The realized PnL of the closed part, on the collateral grid rounded downward.
 */
[[nodiscard]] Number
tradeIntoPosition(
    STLedgerEntry& position,
    STLedgerEntry const& market,
    STLedgerEntry const& exchange,
    Number const& quantity,
    Number const& price);

/**
 * One fill of a position (spec 7.5): settles the position, trades quantity at price into it,
 * moves realized PnL between sfCash, sfCashTotal and sfSettlementBalance, and moves the market's
 * open interest and the exchange's sfOpenInterestNotional with the change in size.
 *
 * @param quantity Positive for a buy, negative for a sell.
 */
void
applyFill(
    ApplyView& view,
    SLE::Ref account,
    SLE::Ref position,
    SLE::Ref market,
    SLE::Ref exchange,
    Number const& quantity,
    Number const& price);

/**
 * The incoming order of a match. matchOrder lowers remaining as it fills.
 */
struct Taker
{
    /**
     * The taker's ltMARGIN_ACCOUNT and its ltMARGIN_POSITION in the market.
     */
    SLE::pointer account;
    SLE::pointer position;
    AccountID owner;
    Number limitPrice;
    Number remaining;
    bool sell{};
    bool reduceOnly{};
    /**
     * Stops at the first fill instead of making it.
     */
    bool postOnly{};
    std::optional<AccountID> builder;
    std::uint32_t builderFee{};
    /**
     * Charges the taker no trading fee, as a book-mode liquidation (spec 9.3).
     */
    bool noTakerFee{};
};

/**
 * How a match ended.
 */
struct MatchResult
{
    Number filled;
    /**
     * A post-only taker reached an order it would fill.
     */
    bool crossed{};
    /**
     * A fill would have raised sfOpenInterestLong above sfOpenInterestMax.
     */
    bool capped{};
    /**
     * kMaxPerpFills maker orders were visited while the book still crossed.
     */
    bool exhausted{};
};

/**
 * Matches a taker against the opposite side of a market's book (spec 8), at most kMaxPerpFills
 * maker orders. Expired and self-trade makers are deleted, as are makers whose fill would leave
 * their equity below zero, or below their initial requirement when their size grows; reduce-only
 * makers are trimmed to their position. Each fill is made at the maker's price, charges both
 * sides' trading and builder fees (spec 12), and reduces the taker's counted open size. The
 * vault's fee share is paid into the bound vault when it has shares, else to the insurance fund.
 *
 * @param market The ltPERP_MARKET, refreshed in this ledger.
 * @param exchange The market's ltPERP_EXCHANGE.
 * @return The result, or a tef or tec error that fails the transaction.
 */
[[nodiscard]] std::expected<MatchResult, TER>
matchOrder(ApplyView& view, SLE::Ref market, SLE::Ref exchange, Taker& taker, beast::Journal j);

/**
 * A market that bears a waterfall shortfall through its loss index (spec 9.6 step 4).
 */
struct LossMarket
{
    /**
     * The ltPERP_MARKET, peeked from the waterfall's view.
     */
    SLE::pointer market;
    /**
     * The account's share of the shortfall in this market: its position's notional at the mark.
     */
    Number weight;
    /**
     * Whether the account's position in the market was long, so that the shorts pay.
     */
    bool wasLong{};
};

/**
 * Pays a margin account's shortfall (spec 9.6). The sources are taken in order, each giving
 * min(remaining, what it has): the insurance fund; the owner's cover; the bound vault, at most
 * sfVaultLossLimit of its assets per sfVaultLossWindow and at most its sfAssetsAvailable; the loss
 * index of the opposite side of each loss market, split by weight; and bad debt. The whole
 * shortfall is credited to the account's sfCash and the exchange's sfCashTotal, and the amount the
 * first four sources paid to its sfWaterfallCredit.
 *
 * @param account The ltMARGIN_ACCOUNT.
 * @param exchange The account's ltPERP_EXCHANGE.
 * @param shortfall The positive amount to pay, on the collateral grid.
 * @param lossMarkets The markets whose opposite side bears the loss index step.
 * @return tesSUCCESS, or the error of the vault transfer.
 */
[[nodiscard]] TER
runWaterfall(
    ApplyView& view,
    SLE::Ref account,
    SLE::Ref exchange,
    Number const& shortfall,
    std::vector<LossMarket> const& lossMarkets,
    beast::Journal j);

}  // namespace xrpl::perp

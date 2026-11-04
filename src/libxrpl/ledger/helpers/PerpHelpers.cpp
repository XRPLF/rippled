#include <xrpl/ledger/helpers/PerpHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTakesAsset.h>
#include <xrpl/protocol/STVector256.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace xrpl::perp {

namespace {

[[nodiscard]] Number
rateOf(std::int64_t tenthBips)
{
    return Number{tenthBips} / Number{kRateUnity};
}

[[nodiscard]] Number
clampNumber(Number const& value, Number const& low, Number const& high)
{
    return std::max(low, std::min(value, high));
}

/**
 * Sets a position's funding index, and while it has a size its side's loss index, to the
 * market's; returns whether either changed.
 */
bool
snapshotIndexes(STLedgerEntry& position, STLedgerEntry const& market)
{
    bool changed = false;
    Number const cumulative = market[sfCumulativeFunding];
    if (position[sfFundingIndex] != cumulative)
    {
        position[sfFundingIndex] = cumulative;
        changed = true;
    }
    if (Number const size = position[sfSize]; size != Number{})
    {
        Number const sideIndex =
            size > Number{} ? market[sfLossIndexLong] : market[sfLossIndexShort];
        if (position[sfLossIndex] != sideIndex)
        {
            position[sfLossIndex] = sideIndex;
            changed = true;
        }
    }
    return changed;
}

}  // namespace

bool
isMultiple(Number const& value, Number const& step)
{
    NumberRoundModeGuard const mg(Number::RoundingMode::ToNearest);
    Number const steps = value / step;
    return steps == steps.truncate() && steps * step == value;
}

std::uint32_t
refreshTime(ReadView const& view)
{
    return static_cast<std::uint32_t>(
        view.parentCloseTime().time_since_epoch().count() + kEpochOffset.count());
}

std::optional<Number>
freshQuote(ReadView const& view, STLedgerEntry const& market, STObject const& indexOracle)
{
    auto const oracle =
        view.read(keylet::oracle(indexOracle[sfOwner], indexOracle[sfOracleDocumentID]));
    if (!oracle)
        return std::nullopt;

    auto const base = market[sfBaseAsset];
    auto const quote = market[sfQuoteAsset];
    auto const& series = oracle->getFieldArray(sfPriceDataSeries);
    auto const pair = std::ranges::find_if(series, [&](STObject const& data) {
        return data[sfBaseAsset] == base && data[sfQuoteAsset] == quote &&
            data.isFieldPresent(sfAssetPrice);
    });
    if (pair == series.end())
        return std::nullopt;

    // The pair kept its price, so it was written in the oracle's last modifying ledger.
    std::int64_t const ledgers =
        std::int64_t{view.seq()} - std::int64_t{(*oracle)[sfPreviousTxnLgrSeq]};
    if (ledgers > std::int64_t{market[sfIndexMaxLedgers]})
        return std::nullopt;

    // A provider timestamp ahead of the close gives a negative age.
    std::int64_t const age =
        std::int64_t{refreshTime(view)} - std::int64_t{(*oracle)[sfLastUpdateTime]};
    if (age > kIndexMaxAge.count())
        return std::nullopt;

    // STAmount holds the whole uint64 price, which Number's signed mantissa cannot.
    auto const price = (*pair)[sfAssetPrice];
    if (price == 0)
        return std::nullopt;
    int const scale = (*pair)[~sfScale].value_or(0);
    return Number{STAmount{noIssue(), price, -scale}};
}

std::expected<Number, TER>
computeIndex(ReadView const& view, STLedgerEntry const& market)
{
    auto const& oracles = market.getFieldArray(sfIndexOracles);
    std::vector<Number> quotes;
    quotes.reserve(oracles.size());
    for (auto const& oracle : oracles)
    {
        if (auto const quote = freshQuote(view, market, oracle))
            quotes.push_back(*quote);
    }

    if (quotes.size() < (oracles.size() / 2) + 1)
        return std::unexpected<TER>(tecPERP_STALE_PRICE);

    std::ranges::sort(quotes);
    auto const count = quotes.size();
    if (count % 2 == 1)
        return quotes[(count - 1) / 2];

    NumberRoundModeGuard const mg(Number::RoundingMode::ToNearest);
    return (quotes[(count / 2) - 1] + quotes[count / 2]) / Number{2};
}

Number
computeBasisEMA(BasisInput const& input)
{
    NumberRoundModeGuard const mg(Number::RoundingMode::ToNearest);
    Number const& index = input.index;

    // The book mid counts only from an uncrossed pair no wider than half the order band.
    Number sample;
    if (input.bid && input.ask && index > Number{} && *input.bid < *input.ask &&
        (*input.ask - *input.bid) / index <= rateOf(input.orderPriceBand) / Number{2})
    {
        sample = ((*input.bid + *input.ask) / Number{2}) - index;
    }

    Number const window{kBasisWindow.count()};
    Number const alpha = std::min(Number{input.elapsed}, window / Number{2}) / window;
    return input.basisEMA + (alpha * (sample - input.basisEMA));
}

Number
computeFundingRate(Number const& basisEMA, Number const& index)
{
    if (index <= Number{})
        return Number{};

    NumberRoundModeGuard const mg(Number::RoundingMode::ToNearest);
    Number const premium = basisEMA / index;
    Number const interestClamp = rateOf(kFundingInterestClamp);
    Number const cap = rateOf(kFundingCap);
    Number const interest =
        clampNumber(rateOf(kFundingInterestRate) - premium, -interestClamp, interestClamp);
    return clampNumber(premium + interest, -cap, cap);
}

Number
fundingAccrual(Number const& rate, Number const& index, std::uint32_t elapsed)
{
    NumberRoundModeGuard const mg(Number::RoundingMode::ToNearest);
    return rate * index * Number{std::min(elapsed, kFundingInterval)} / Number{kFundingInterval};
}

std::optional<Number>
bestPrice(ReadView const& view, uint256 const& marketID, bool sell)
{
    auto const book = keylet::perpBook(marketID, sell);
    auto const level = view.succ(book.key, getQualityNext(book.key));
    if (!level)
        return std::nullopt;

    SLE::const_pointer page;
    unsigned int index = 0;
    uint256 entry;
    if (!cdirFirst(view, *level, page, index, entry))
        return std::nullopt;

    auto const order = view.read(keylet::perpOrder(entry));
    if (!order)
        return std::nullopt;
    return (*order)[sfLimitPrice];
}

bool
tiersTighten(STArray const& current, STArray const& proposed)
{
    if (proposed.size() != current.size())
        return false;
    for (std::size_t i = 0; i < current.size(); ++i)
    {
        auto const& before = current[i];
        auto const& after = proposed[i];
        if (after[sfInitialMarginRate] < before[sfInitialMarginRate] ||
            after[sfMaintenanceMarginRate] < before[sfMaintenanceMarginRate] ||
            Number{after[sfNotionalMax]} > Number{before[sfNotionalMax]})
            return false;
    }
    return true;
}

void
refreshMarket(ApplyView& view, SLE::Ref market, SLE::Ref exchange, beast::Journal j)
{
    XRPL_ASSERT(
        market && market->getType() == ltPERP_MARKET && exchange &&
            exchange->getType() == ltPERP_EXCHANGE,
        "xrpl::perp::refreshMarket : valid market and exchange");

    if ((*market)[sfRefreshLedger] == view.seq())
        return;

    NumberRoundModeGuard const mg(Number::RoundingMode::ToNearest);
    auto const t = refreshTime(view);
    bool exchangeChanged = false;

    std::uint32_t const lastRefresh = (*market)[sfLastRefreshTime];
    std::uint32_t const elapsed = t > lastRefresh ? t - lastRefresh : 0;
    std::uint8_t const status = (*market)[sfMarketStatus];

    // Funding accrues at the rate fixed by the previous refresh.
    if (status == kMarketLive)
    {
        (*market)[sfCumulativeFunding] = (*market)[sfCumulativeFunding] +
            fundingAccrual((*market)[sfFundingRate], (*market)[sfIndexPrice], elapsed);
    }

    // A Settled market keeps its frozen index.
    if (status != kMarketSettled)
    {
        auto const index = computeIndex(view, *market);
        if (!index)
        {
            JLOG(j.debug()) << "perp::refreshMarket: no index quorum for market " << market->key();
            if (status == kMarketLive)
            {
                (*market)[sfMarketStatus] = kMarketStale;
                (*market)[sfStatusTime] = t;
            }
            (*market)[sfFundingRate] = Number{};
        }
        else
        {
            if (status == kMarketStale)
            {
                (*market)[sfMarketStatus] = kMarketLive;
                (*market)[sfStatusTime] = t;
            }

            // The book feeds only the basis, and the basis only funding.
            Number const oldIndex = (*market)[sfIndexPrice];
            Number const basisEMA = computeBasisEMA(
                {.index = *index,
                 .basisEMA = (*market)[sfBasisEMA],
                 .elapsed = elapsed,
                 .bid = bestPrice(view, market->key(), false),
                 .ask = bestPrice(view, market->key(), true),
                 .orderPriceBand = (*market)[sfOrderPriceBand]});

            (*market)[sfIndexPrice] = *index;
            (*market)[sfBasisEMA] = basisEMA;
            (*market)[sfFundingRate] = computeFundingRate(basisEMA, *index);

            Number const notionalDelta = (*market)[sfOpenInterestLong] * (*index - oldIndex);
            if (notionalDelta != Number{})
            {
                (*exchange)[sfOpenInterestNotional] =
                    (*exchange)[sfOpenInterestNotional] + notionalDelta;
                exchangeChanged = true;
            }
        }
    }

    (*market)[sfLastRefreshTime] = t;
    (*market)[sfRefreshLedger] = view.seq();
    view.update(market);
    if (exchangeChanged)
        view.update(exchange);
}

std::optional<Number>
tierRequirement(Number const& notional, STArray const& tiers, bool initial)
{
    XRPL_ASSERT(notional >= Number{}, "xrpl::perp::tierRequirement : non-negative notional");
    auto const& rateField = initial ? sfInitialMarginRate : sfMaintenanceMarginRate;

    // The deduction lowers the requirement, so it rounds down while the requirement rounds up.
    Number deduction;
    Number previousMax;
    Number previousRate;
    for (auto const& tier : tiers)
    {
        Number const rate = rateOf(tier[rateField]);
        {
            NumberRoundModeGuard const mg(Number::RoundingMode::Downward);
            deduction = deduction + (previousMax * (rate - previousRate));
        }
        Number const notionalMax = tier[sfNotionalMax];
        if (notional <= notionalMax)
        {
            NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
            return (notional * rate) - deduction;
        }
        previousMax = notionalMax;
        previousRate = rate;
    }
    return std::nullopt;
}

Number
roundCollateral(STLedgerEntry const& exchange, Number const& value, Number::RoundingMode mode)
{
    Asset const asset = exchange[sfAsset];
    return roundToAsset(asset, value, -int{exchange[sfScale]}, mode);
}

TER
checkDomain(ReadView const& view, uint256 const& domainID, AccountID const& account)
{
    auto const domain = view.read(keylet::permissionedDomain(domainID));
    if (!domain)
        return tecNO_AUTH;
    if ((*domain)[sfOwner] == account)
        return tesSUCCESS;

    auto const ter = credentials::validDomain(view, domainID, account);
    if (isTesSuccess(ter) || ter == tecEXPIRED)
        return tesSUCCESS;
    return tecNO_AUTH;
}

TER
verifyDomain(ApplyView& view, uint256 const& domainID, AccountID const& account, beast::Journal j)
{
    auto const domain = view.read(keylet::permissionedDomain(domainID));
    if (!domain)
        return tecNO_AUTH;
    if ((*domain)[sfOwner] == account)
        return tesSUCCESS;

    auto const ter = verifyValidDomain(view, account, domainID, j);
    if (isTesSuccess(ter) || ter == tecEXPIRED)
        return ter;
    return tecNO_AUTH;
}

SettlementCharge
settlementCharge(
    STLedgerEntry const& exchange,
    STLedgerEntry const& market,
    STLedgerEntry const& position)
{
    Number const size = position[sfSize];
    if (size == Number{})
        return {.funding = Number{}, .loss = Number{}};

    Number const sideIndex = size > Number{} ? market[sfLossIndexLong] : market[sfLossIndexShort];
    NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
    Number const funding = size * (market[sfCumulativeFunding] - position[sfFundingIndex]);
    Number const loss = abs(size) * (sideIndex - position[sfLossIndex]);
    return {
        .funding = roundCollateral(exchange, funding, Number::RoundingMode::Upward),
        .loss = roundCollateral(exchange, loss, Number::RoundingMode::Upward)};
}

TER
settleAccount(ApplyView& view, SLE::Ref account, SLE::Ref exchange, beast::Journal j)
{
    XRPL_ASSERT(
        account && account->getType() == ltMARGIN_ACCOUNT && exchange &&
            exchange->getType() == ltPERP_EXCHANGE && (*account)[sfExchangeID] == exchange->key(),
        "xrpl::perp::settleAccount : account of the exchange");

    // A copy, since a settlement close removes its market from sfMarkets.
    STVector256 const marketIDs = account->getFieldV256(sfMarkets);
    for (auto const& marketID : marketIDs)
    {
        auto const market = view.peek(keylet::perpMarket(marketID));
        auto const position = view.peek(keylet::marginPosition(account->key(), marketID));
        if (!market || !position)
        {
            JLOG(j.fatal()) << "perp::settleAccount: missing market or position " << marketID;
            return tefBAD_LEDGER;
        }

        refreshMarket(view, market, exchange, j);
        settlePosition(view, account, position, *market, exchange);
        if ((*market)[sfMarketStatus] == kMarketSettled)
        {
            if (auto const ter = closeSettledPosition(view, account, position, market, exchange, j);
                !isTesSuccess(ter))
                return ter;
        }
    }

    auto const flat = isFlat(view, *account);
    if (!flat)
        return flat.error();  // LCOV_EXCL_LINE
    if (*flat)
        repayWaterfallCredit(view, account, exchange);
    return tesSUCCESS;
}

void
settleMarket(STLedgerEntry& market, std::uint32_t now)
{
    market[sfMarketStatus] = kMarketSettled;
    market[sfStatusTime] = now;
    market[sfFundingRate] = Number{};
}

std::expected<bool, TER>
isFlat(ReadView const& view, STLedgerEntry const& account)
{
    for (auto const& marketID : account.getFieldV256(sfMarkets))
    {
        auto const position = view.read(keylet::marginPosition(account.key(), marketID));
        if (!position)
            return std::unexpected<TER>(tefBAD_LEDGER);  // LCOV_EXCL_LINE
        if ((*position)[sfSize] != Number{})
            return false;
    }
    return true;
}

TER
closeSettledPosition(
    ApplyView& view,
    SLE::Ref account,
    SLE::Ref position,
    SLE::Ref market,
    SLE::Ref exchange,
    beast::Journal j)
{
    XRPL_ASSERT(
        market && (*market)[sfMarketStatus] == kMarketSettled,
        "xrpl::perp::closeSettledPosition : settled market");
    Number const size = (*position)[sfSize];
    if (size == Number{})
        return tesSUCCESS;

    // A gain waits out kSettlementGrace, so losses close first.
    Number const price = (*market)[sfIndexPrice];
    Number gain;
    {
        NumberRoundModeGuard const mg(Number::RoundingMode::Downward);
        gain = roundCollateral(
            *exchange,
            (size * price) - (*position)[sfEntryNotional],
            Number::RoundingMode::Downward);
    }
    std::uint64_t const graceEnd =
        std::uint64_t{(*market)[sfStatusTime]} + kSettlementGrace.count();
    if (gain > Number{} && refreshTime(view) < graceEnd)
        return tesSUCCESS;

    applyFill(view, account, position, market, exchange, -size, price);
    if (auto const ter = deletePositionIfEmpty(view, position, j); !isTesSuccess(ter))
        return ter;  // LCOV_EXCL_LINE

    // A flat account with negative cash has realized its shortfall in this market.
    auto const flat = isFlat(view, *account);
    if (!flat)
        return flat.error();  // LCOV_EXCL_LINE
    if (!*flat || (*account)[sfCash] >= Number{})
        return tesSUCCESS;
    runWaterfall(
        view,
        account,
        exchange,
        -Number{(*account)[sfCash]},
        {{.market = market, .weight = Number{1}, .wasLong = size > Number{}}});
    return tesSUCCESS;
}

void
repayWaterfallCredit(ApplyView& view, SLE::Ref account, SLE::Ref exchange)
{
    Number const credit = (*account)[sfWaterfallCredit];
    Number const cash = (*account)[sfCash];
    if (credit <= Number{} || cash <= Number{})
        return;

    Number const repaid = std::min(cash, credit);
    (*account)[sfCash] = cash - repaid;
    (*account)[sfWaterfallCredit] = credit - repaid;
    (*exchange)[sfCashTotal] = (*exchange)[sfCashTotal] - repaid;
    (*exchange)[sfInsuranceFund] = (*exchange)[sfInsuranceFund] + absorbBadDebt(*exchange, repaid);
    view.update(account);
    view.update(exchange);
}

Number
absorbBadDebt(STLedgerEntry& exchange, Number const& amount)
{
    Number const badDebt = exchange[sfBadDebt];
    Number const absorbed = std::min(badDebt, amount);
    if (absorbed <= Number{})
        return amount;
    exchange[sfBadDebt] = badDebt - absorbed;
    return amount - absorbed;
}

std::expected<Number, TER>
withdrawalHaircut(STLedgerEntry const& exchange, Number const& amount)
{
    Number const badDebt = exchange[sfBadDebt];
    if (badDebt <= Number{} || amount <= Number{})
        return Number{};

    Number const cashTotal = exchange[sfCashTotal];
    if (cashTotal <= Number{})
        return std::unexpected<TER>(tecPERP_INSUFFICIENT_COVER);

    NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
    Number const h = std::min(Number{1}, badDebt / cashTotal);
    Number const haircut = roundCollateral(exchange, amount * h, Number::RoundingMode::Upward);
    return std::min({haircut, badDebt, amount});
}

namespace {

/**
 * The tiered requirement of a notional, charged at the last tier's rate above the last tier.
 */
[[nodiscard]] Number
requirement(Number const& notional, STArray const& tiers, bool initial)
{
    if (auto const inTiers = tierRequirement(notional, tiers, initial))
        return *inTiers;
    if (tiers.empty())
        return Number{};  // LCOV_EXCL_LINE

    auto const& last = tiers.back();
    Number const top = last[sfNotionalMax];
    auto const atTop = tierRequirement(top, tiers, initial);
    NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
    Number const rate = rateOf(last[initial ? sfInitialMarginRate : sfMaintenanceMarginRate]);
    return atTop.value_or(Number{}) + ((notional - top) * rate);
}

/**
 * The margin figures of an account, with its unsettled charges taken from cash.
 */
struct MarginState
{
    Number cash;
    Number unrealized;
    Number losses;
    MarginRequirements requirements;
};

/**
 * Every market is valued at its sfIndexPrice, frozen in a Settled market. With liquidation set, a
 * Stale market is valued at its last quorum index; otherwise no market may be Stale.
 */
[[nodiscard]] std::expected<MarginState, TER>
marginState(
    ReadView const& view,
    STLedgerEntry const& account,
    STLedgerEntry const* override = nullptr,
    bool liquidation = false)
{
    auto const exchange = view.read(keylet::perpExchange(account[sfExchangeID]));
    if (!exchange)
        return std::unexpected<TER>(tefBAD_LEDGER);  // LCOV_EXCL_LINE

    MarginState state{
        .cash = account[sfCash],
        .unrealized = Number{},
        .losses = Number{},
        .requirements = {.initial = Number{}, .maintenance = Number{}}};
    for (auto const& marketID : account.getFieldV256(sfMarkets))
    {
        auto const market = view.read(keylet::perpMarket(marketID));
        auto const read = (override != nullptr) && (*override)[sfMarketID] == marketID
            ? nullptr
            : view.read(keylet::marginPosition(account.key(), marketID));
        STLedgerEntry const* position = read ? read.get() : override;
        if (!market || (position == nullptr) || (*position)[sfMarketID] != marketID)
            return std::unexpected<TER>(tefBAD_LEDGER);  // LCOV_EXCL_LINE
        std::uint8_t const status = (*market)[sfMarketStatus];
        if (status == kMarketStale && !liquidation)
            return std::unexpected<TER>(tecPERP_STALE_PRICE);

        auto const charge = settlementCharge(*exchange, *market, *position);
        state.cash = state.cash - charge.funding - charge.loss;

        Number const index = (*market)[sfIndexPrice];
        Number const size = (*position)[sfSize];
        {
            NumberRoundModeGuard const mg(Number::RoundingMode::Downward);
            Number const pnl = (size * index) - (*position)[sfEntryNotional];
            state.unrealized = state.unrealized + pnl;
            state.losses = state.losses + std::min(pnl, Number{});
        }

        // A Settled position has a fixed value and no orders that can fill, so no requirement.
        if (status == kMarketSettled)
            continue;
        auto const& tiers = market->getFieldArray(sfMarginTiers);
        NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
        Number const worstCase =
            std::max(abs(size + (*position)[sfOpenBuy]), abs(size - (*position)[sfOpenSell]));
        Number const initial = requirement(worstCase * index, tiers, true);
        Number const maintenance = requirement(abs(size) * index, tiers, false);
        state.requirements.initial = state.requirements.initial + initial;
        state.requirements.maintenance = state.requirements.maintenance + maintenance;
    }
    return state;
}

}  // namespace

std::expected<Number, TER>
accountEquity(ReadView const& view, STLedgerEntry const& account)
{
    auto const state = marginState(view, account);
    if (!state)
        return std::unexpected(state.error());
    NumberRoundModeGuard const mg(Number::RoundingMode::Downward);
    return state->cash + state->unrealized;
}

std::expected<MarginRequirements, TER>
accountRequirements(ReadView const& view, STLedgerEntry const& account)
{
    auto const state = marginState(view, account);
    if (!state)
        return std::unexpected(state.error());
    return state->requirements;
}

std::expected<Number, TER>
withdrawable(ReadView const& view, STLedgerEntry const& account)
{
    auto const exchange = view.read(keylet::perpExchange(account[sfExchangeID]));
    auto const state = marginState(view, account);
    if (!state)
        return std::unexpected(state.error());
    if (!exchange)
        return std::unexpected<TER>(tefBAD_LEDGER);  // LCOV_EXCL_LINE

    NumberRoundModeGuard const mg(Number::RoundingMode::Downward);
    Number const limit =
        std::min(state->cash, state->cash + state->losses - state->requirements.initial);
    return roundCollateral(*exchange, limit, Number::RoundingMode::Downward);
}

std::expected<MarginSummary, TER>
marginSummary(ReadView const& view, STLedgerEntry const& account, STLedgerEntry const* position)
{
    auto const state = marginState(view, account, position);
    if (!state)
        return std::unexpected(state.error());
    NumberRoundModeGuard const mg(Number::RoundingMode::Downward);
    return MarginSummary{
        .equity = state->cash + state->unrealized, .requirements = state->requirements};
}

std::expected<MarginSummary, TER>
liquidationSummary(ReadView const& view, STLedgerEntry const& account)
{
    auto const state = marginState(view, account, nullptr, true);
    if (!state)
        return std::unexpected(state.error());
    NumberRoundModeGuard const mg(Number::RoundingMode::Downward);
    return MarginSummary{
        .equity = state->cash + state->unrealized, .requirements = state->requirements};
}

std::uint64_t
orderQuality(Number const& price, bool sell)
{
    STAmount const one{noIssue(), 1};
    STAmount const amount{noIssue(), price};
    return sell ? getRate(one, amount) : getRate(amount, one);
}

PerpBookTip::PerpBookTip(ApplyView& view, uint256 const& marketID, bool sell)
    : view_(view), book_(keylet::perpBook(marketID, sell).key), end_(getQualityNext(book_))
{
}

bool
PerpBookTip::step()
{
    for (;;)
    {
        auto const firstPage = view_.succ(book_, end_);
        if (!firstPage)
            return false;

        SLE::pointer page;
        unsigned int index = 0;
        uint256 entry;
        if (dirFirst(view_, *firstPage, page, index, entry))
        {
            order_ = view_.peek(keylet::perpOrder(entry));
            // The next search starts just before this price level, which still holds the order.
            book_ = *firstPage;
            --book_;
            return true;
        }

        // An empty level is skipped.
        book_ = *firstPage;  // LCOV_EXCL_LINE
    }
}

TER
insertOrder(ApplyView& view, RestingOrder const& order, beast::Journal j)
{
    auto const position = view.peek(keylet::marginPosition(order.marginAccountID, order.marketID));
    auto const market = view.peek(keylet::perpMarket(order.marketID));
    auto const owner = view.peek(keylet::account(order.account));
    if (!position || !market || !owner)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    auto const orderKeylet =
        keylet::perpOrder(order.account, SeqProxy::rawSequence(order.sequence));
    auto const ownerNode = view.dirInsert(
        keylet::ownerDir(order.account), orderKeylet, describeOwnerDir(order.account));
    if (!ownerNode)
        return tecDIR_FULL;  // LCOV_EXCL_LINE
    increaseOwnerCount(view, owner, {}, 1, j);

    // Every new page of the price level records its quality.
    auto const quality = orderQuality(order.price, order.sell);
    auto const level = keylet::quality(keylet::perpBook(order.marketID, order.sell), quality);
    auto const bookNode = view.dirAppend(
        level, orderKeylet, [&](SLE::Ref page) { page->setFieldU64(sfExchangeRate, quality); });
    if (!bookNode)
        return tecDIR_FULL;  // LCOV_EXCL_LINE

    auto const sle = std::make_shared<SLE>(orderKeylet);
    (*sle)[sfAccount] = order.account;
    (*sle)[sfSequence] = order.sequence;
    (*sle)[sfMarginAccountID] = order.marginAccountID;
    (*sle)[sfMarketID] = order.marketID;
    (*sle)[sfLimitPrice] = order.price;
    (*sle)[sfSize] = order.size;
    (*sle)[~sfExpiration] = order.expiration;
    (*sle)[~sfBuilder] = order.builder;
    (*sle)[~sfBuilderFee] = order.builderFee;
    (*sle)[sfBookDirectory] = level.key;
    (*sle)[sfBookNode] = *bookNode;
    (*sle)[sfOwnerNode] = *ownerNode;
    if (order.sell)
        sle->setFlag(lsfPerpSell);
    if (order.reduceOnly)
        sle->setFlag(lsfPerpReduceOnly);
    view.insert(sle);

    // Reduce-only orders never add to the position, so they are left out of its worst case.
    if (!order.reduceOnly)
    {
        auto const& open = order.sell ? sfOpenSell : sfOpenBuy;
        (*position)[open] = (*position)[open] + order.size;
    }
    (*position)[sfOrderCount] = (*position)[sfOrderCount] + 1;
    (*market)[sfOrderCount] = (*market)[sfOrderCount] + 1;
    view.update(position);
    view.update(market);
    return tesSUCCESS;
}

TER
removeOrder(ApplyView& view, SLE::Ref order, beast::Journal j)
{
    AccountID const owner = (*order)[sfAccount];
    auto const position =
        view.peek(keylet::marginPosition((*order)[sfMarginAccountID], (*order)[sfMarketID]));
    auto const market = view.peek(keylet::perpMarket((*order)[sfMarketID]));
    if (!position || !market)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    if (!view.dirRemove(keylet::ownerDir(owner), (*order)[sfOwnerNode], order->key(), false) ||
        !view.dirRemove(
            keylet::page((*order)[sfBookDirectory]), (*order)[sfBookNode], order->key(), false))
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    if (!order->isFlag(lsfPerpReduceOnly))
    {
        auto const& open = order->isFlag(lsfPerpSell) ? sfOpenSell : sfOpenBuy;
        (*position)[open] = (*position)[open] - (*order)[sfSize];
    }
    (*position)[sfOrderCount] = (*position)[sfOrderCount] - 1;
    (*market)[sfOrderCount] = (*market)[sfOrderCount] - 1;
    decreaseOwnerCountForObject(view, owner, order, 1, j);
    view.erase(order);
    view.update(position);
    view.update(market);
    return deletePositionIfEmpty(view, position, j);
}

TER
deletePositionIfEmpty(ApplyView& view, SLE::Ref position, beast::Journal j)
{
    if ((*position)[sfSize] != Number{} || (*position)[sfOrderCount] != 0)
        return tesSUCCESS;

    auto const account = view.peek(keylet::marginAccount((*position)[sfMarginAccountID]));
    if (!account)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    AccountID const owner = (*account)[sfAccount];
    if (!view.dirRemove(keylet::ownerDir(owner), (*position)[sfOwnerNode], position->key(), false))
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    STVector256 markets = account->getFieldV256(sfMarkets);
    if (auto const it = std::ranges::find(markets, (*position)[sfMarketID]); it != markets.end())
        markets.erase(it);
    // An empty default field is stored absent.
    if (markets.empty())
    {
        account->makeFieldAbsent(sfMarkets);
    }
    else
    {
        account->setFieldV256(sfMarkets, markets);
    }
    decreaseOwnerCountForObject(view, owner, position, 1, j);
    view.erase(position);
    view.update(account);
    return tesSUCCESS;
}

std::expected<SLE::pointer, TER>
openPosition(ApplyView& view, SLE::Ref account, STLedgerEntry const& market, beast::Journal j)
{
    auto const& markets = account->getFieldV256(sfMarkets);
    if (markets.size() >= kMaxMarginPositions)
        return std::unexpected<TER>(tecPERP_POSITION_LIMIT);
    if (market.isFlag(lsfPerpIsolatedOnly) && !markets.empty())
        return std::unexpected<TER>(tecPERP_POSITION_LIMIT);
    for (auto const& marketID : markets)
    {
        auto const other = view.read(keylet::perpMarket(marketID));
        if (!other)
            return std::unexpected<TER>(tefBAD_LEDGER);  // LCOV_EXCL_LINE
        if (other->isFlag(lsfPerpIsolatedOnly))
            return std::unexpected<TER>(tecPERP_POSITION_LIMIT);
    }
    return createPosition(view, account, market, j);
}

std::expected<SLE::pointer, TER>
createPosition(ApplyView& view, SLE::Ref account, STLedgerEntry const& market, beast::Journal j)
{
    AccountID const owner = (*account)[sfAccount];
    auto const root = view.peek(keylet::account(owner));
    if (!root)
        return std::unexpected<TER>(tefBAD_LEDGER);  // LCOV_EXCL_LINE

    auto position = std::make_shared<SLE>(keylet::marginPosition(account->key(), market.key()));
    if (auto const ter = dirLink(view, owner, position))
        return std::unexpected<TER>(ter);  // LCOV_EXCL_LINE
    increaseOwnerCount(view, root, {}, 1, j);

    (*position)[sfMarginAccountID] = account->key();
    (*position)[sfMarketID] = market.key();
    (*position)[sfFundingIndex] = market[sfCumulativeFunding];
    view.insert(position);

    STVector256 markets = account->getFieldV256(sfMarkets);
    markets.pushBack(market.key());
    account->setFieldV256(sfMarkets, markets);
    view.update(account);
    return position;
}

void
settlePosition(
    ApplyView& view,
    SLE::Ref account,
    SLE::Ref position,
    STLedgerEntry const& market,
    SLE::Ref exchange)
{
    auto const charge = settlementCharge(*exchange, market, *position);
    Number const total = charge.funding + charge.loss;
    if (snapshotIndexes(*position, market))
        view.update(position);
    if (total == Number{})
        return;

    (*account)[sfCash] = (*account)[sfCash] - total;
    (*exchange)[sfCashTotal] = (*exchange)[sfCashTotal] - total;
    (*exchange)[sfSettlementBalance] = (*exchange)[sfSettlementBalance] + total;
    view.update(account);
    view.update(exchange);
}

Number
tradeIntoPosition(
    STLedgerEntry& position,
    STLedgerEntry const& market,
    STLedgerEntry const& exchange,
    Number const& quantity,
    Number const& price)
{
    NumberRoundModeGuard const mg(Number::RoundingMode::ToNearest);
    Number const size = position[sfSize];
    Number const entry = position[sfEntryNotional];
    Number realized;
    Number opening = quantity;

    // An opposite quantity first closes up to the whole position at its average entry.
    if (size != Number{} && (quantity > Number{}) != (size > Number{}))
    {
        Number const magnitude = std::min(abs(quantity), abs(size));
        Number const closed = size > Number{} ? magnitude : -magnitude;
        Number const basis = closed == size ? entry : entry * closed / size;
        realized =
            roundCollateral(exchange, (closed * price) - basis, Number::RoundingMode::Downward);
        position[sfEntryNotional] = entry - basis;
        position[sfSize] = size - closed;
        opening = quantity + closed;
    }

    if (opening != Number{})
    {
        Number const remaining = position[sfSize];
        position[sfSize] = remaining + opening;
        position[sfEntryNotional] = position[sfEntryNotional] + (opening * price);
        // A size that leaves zero or changes sign starts from the market's current indexes.
        if (remaining == Number{})
        {
            position[sfFundingIndex] = market[sfCumulativeFunding];
            position[sfLossIndex] =
                opening > Number{} ? market[sfLossIndexLong] : market[sfLossIndexShort];
        }
    }
    return realized;
}

void
applyFill(
    ApplyView& view,
    SLE::Ref account,
    SLE::Ref position,
    SLE::Ref market,
    SLE::Ref exchange,
    Number const& quantity,
    Number const& price)
{
    settlePosition(view, account, position, *market, exchange);
    Number const before = (*position)[sfSize];
    Number const realized = tradeIntoPosition(*position, *market, *exchange, quantity, price);
    Number const after = (*position)[sfSize];

    if (realized != Number{})
    {
        (*account)[sfCash] = (*account)[sfCash] + realized;
        (*exchange)[sfCashTotal] = (*exchange)[sfCashTotal] + realized;
        (*exchange)[sfSettlementBalance] = (*exchange)[sfSettlementBalance] - realized;
    }

    Number const longDelta = std::max(after, Number{}) - std::max(before, Number{});
    Number const shortDelta = std::max(-after, Number{}) - std::max(-before, Number{});
    (*market)[sfOpenInterestLong] = (*market)[sfOpenInterestLong] + longDelta;
    (*market)[sfOpenInterestShort] = (*market)[sfOpenInterestShort] + shortDelta;
    if (longDelta != Number{})
    {
        NumberRoundModeGuard const mg(Number::RoundingMode::ToNearest);
        (*exchange)[sfOpenInterestNotional] =
            (*exchange)[sfOpenInterestNotional] + (longDelta * (*market)[sfIndexPrice]);
    }

    associateAsset(*account, (*exchange)[sfAsset]);
    view.update(position);
    view.update(account);
    view.update(market);
    view.update(exchange);
}

namespace {

/**
 * A fee at a rate on a notional, paid by an account: rounded upward onto the collateral grid.
 */
[[nodiscard]] Number
feeOf(STLedgerEntry const& exchange, Number const& notional, std::uint32_t rate)
{
    if (rate == 0)
        return Number{};
    NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
    return roundCollateral(exchange, notional * rateOf(rate), Number::RoundingMode::Upward);
}

/**
 * Charges one side of a fill its trading fee, split to the owner and the insurance fund, and its
 * builder fee, paid to the builder's margin account or else the insurance fund (spec 12).
 */
void
chargeFees(
    ApplyView& view,
    SLE::Ref exchange,
    SLE::Ref account,
    Number const& notional,
    std::uint32_t rate,
    std::optional<AccountID> const& builder,
    std::uint32_t builderRate)
{
    Number const fee = feeOf(*exchange, notional, rate);
    Number const builderFee = builder ? feeOf(*exchange, notional, builderRate) : Number{};
    if (fee == Number{} && builderFee == Number{})
        return;

    (*account)[sfCash] = (*account)[sfCash] - fee - builderFee;
    view.update(account);

    Number insurance;
    if (fee != Number{})
    {
        Number ownerPart;
        {
            NumberRoundModeGuard const mg(Number::RoundingMode::Downward);
            ownerPart = roundCollateral(
                *exchange,
                fee * rateOf((*exchange)[sfOwnerFeeShare]),
                Number::RoundingMode::Downward);
        }
        (*exchange)[sfCashTotal] = (*exchange)[sfCashTotal] - fee;
        (*exchange)[sfOwnerFees] = (*exchange)[sfOwnerFees] + ownerPart;
        insurance = fee - ownerPart;
    }

    if (builder && builderFee != Number{})
    {
        auto const builderAccount = view.peek(keylet::marginAccount(*builder, exchange->key()));
        if (builderAccount)
        {
            (*builderAccount)[sfCash] = (*builderAccount)[sfCash] + builderFee;
            view.update(builderAccount);
        }
        else
        {
            (*exchange)[sfCashTotal] = (*exchange)[sfCashTotal] - builderFee;
            insurance = insurance + builderFee;
        }
    }

    if (insurance != Number{})
    {
        (*exchange)[sfInsuranceFund] =
            (*exchange)[sfInsuranceFund] + absorbBadDebt(*exchange, insurance);
    }
    view.update(exchange);
}

/**
 * The maker check of spec 8 step 3: whether a maker can take a fill of quantity at price. The
 * fill is applied to copies of the maker's account and position, the maker's other positions are
 * settled read-only after refreshing their markets, and the fill is refused when it would leave
 * equity below zero, or below the initial requirement when the position grows.
 *
 * @return Whether the fill may be made, or tefBAD_LEDGER when a listed entry is missing.
 */
[[nodiscard]] std::expected<bool, TER>
makerCanFill(
    ApplyView& view,
    SLE::Ref market,
    SLE::Ref exchange,
    STLedgerEntry const& account,
    STLedgerEntry const& position,
    STLedgerEntry const& order,
    Number const& quantity,
    Number const& price,
    beast::Journal j)
{
    for (auto const& marketID : account.getFieldV256(sfMarkets))
    {
        if (marketID == market->key())
            continue;
        auto const other = view.peek(keylet::perpMarket(marketID));
        if (!other)
            return std::unexpected<TER>(tefBAD_LEDGER);  // LCOV_EXCL_LINE
        refreshMarket(view, other, exchange, j);
    }

    STLedgerEntry accountAfter{account};
    STLedgerEntry positionAfter{position};
    auto const charge = settlementCharge(*exchange, *market, positionAfter);
    snapshotIndexes(positionAfter, *market);

    bool const sells = order.isFlag(lsfPerpSell);
    Number const sizeBefore = abs(positionAfter[sfSize]);
    Number const realized =
        tradeIntoPosition(positionAfter, *market, *exchange, sells ? -quantity : quantity, price);
    Number const notional = quantity * price;
    Number fees = feeOf(*exchange, notional, (*exchange)[sfMakerFee]);
    if (order.isFieldPresent(sfBuilder))
        fees = fees + feeOf(*exchange, notional, order[~sfBuilderFee].value_or(0));
    accountAfter[sfCash] = accountAfter[sfCash] - charge.funding - charge.loss + realized - fees;
    if (!order.isFlag(lsfPerpReduceOnly))
    {
        auto const& open = sells ? sfOpenSell : sfOpenBuy;
        positionAfter[open] = positionAfter[open] - quantity;
    }

    // A maker whose account cannot be valued, with a market not Live, is treated as unfunded.
    auto const summary = marginSummary(view, accountAfter, &positionAfter);
    if (!summary)
    {
        if (summary.error() == tefBAD_LEDGER)
            return std::unexpected(summary.error());  // LCOV_EXCL_LINE
        return false;
    }
    if (summary->equity < Number{})
        return false;
    return abs(positionAfter[sfSize]) <= sizeBefore ||
        summary->equity >= summary->requirements.initial;
}

/**
 * The rise of a market's long open interest when a position moves from before to after.
 */
[[nodiscard]] Number
longRise(Number const& before, Number const& after)
{
    return std::max(after, Number{}) - std::max(before, Number{});
}

}  // namespace

std::expected<MatchResult, TER>
matchOrder(ApplyView& view, SLE::Ref market, SLE::Ref exchange, Taker& taker, beast::Journal j)
{
    MatchResult result;
    PerpBookTip tip(view, market->key(), !taker.sell);
    std::size_t visited = 0;
    while (taker.remaining > Number{})
    {
        if (visited == kMaxPerpFills)
        {
            result.exhausted = true;
            break;
        }
        if (!tip.step())
            break;
        auto const order = tip.order();
        if (!order)
            return std::unexpected<TER>(tefBAD_LEDGER);  // LCOV_EXCL_LINE

        // Each order is checked at its own price, since a quality key may hold more than one.
        Number const price = (*order)[sfLimitPrice];
        if (taker.sell ? price < taker.limitPrice : price > taker.limitPrice)
            break;
        ++visited;

        // Expired and self-trade makers are deleted unfilled.
        if (hasExpired(view, (*order)[~sfExpiration]) || (*order)[sfAccount] == taker.owner)
        {
            if (auto const ter = removeOrder(view, order, j); !isTesSuccess(ter))
                return std::unexpected(ter);  // LCOV_EXCL_LINE
            continue;
        }

        auto const makerAccount = view.peek(keylet::marginAccount((*order)[sfMarginAccountID]));
        auto const makerPosition =
            view.peek(keylet::marginPosition((*order)[sfMarginAccountID], market->key()));
        if (!makerAccount || !makerPosition)
            return std::unexpected<TER>(tefBAD_LEDGER);  // LCOV_EXCL_LINE

        // A reduce-only maker fills at most its position, on the side that reduces it.
        bool const makerReduceOnly = order->isFlag(lsfPerpReduceOnly);
        Number quantity = std::min(taker.remaining, Number{(*order)[sfSize]});
        if (makerReduceOnly)
        {
            Number const size = (*makerPosition)[sfSize];
            Number const reducible =
                taker.sell ? std::max(-size, Number{}) : std::max(size, Number{});
            quantity = std::min(quantity, reducible);
        }

        bool funded = quantity > Number{};
        if (funded)
        {
            auto const check = makerCanFill(
                view, market, exchange, *makerAccount, *makerPosition, *order, quantity, price, j);
            if (!check)
                return std::unexpected(check.error());  // LCOV_EXCL_LINE
            funded = *check;
        }
        if (!funded)
        {
            if (auto const ter = removeOrder(view, order, j); !isTesSuccess(ter))
                return std::unexpected(ter);  // LCOV_EXCL_LINE
            continue;
        }

        Number const takerQuantity = taker.sell ? -quantity : quantity;
        Number const takerSize = (*taker.position)[sfSize];
        Number const makerSize = (*makerPosition)[sfSize];
        Number const rise = longRise(takerSize, takerSize + takerQuantity) +
            longRise(makerSize, makerSize - takerQuantity);
        if (rise > Number{} && (*market)[sfOpenInterestLong] + rise > (*market)[sfOpenInterestMax])
        {
            result.capped = true;
            break;
        }

        if (taker.postOnly)
        {
            result.crossed = true;
            break;
        }

        applyFill(view, taker.account, taker.position, market, exchange, takerQuantity, price);
        applyFill(view, makerAccount, makerPosition, market, exchange, -takerQuantity, price);
        Number const notional = quantity * price;
        chargeFees(
            view,
            exchange,
            taker.account,
            notional,
            taker.noTakerFee ? 0 : std::uint32_t{(*exchange)[sfTakerFee]},
            taker.builder,
            taker.builderFee);
        chargeFees(
            view,
            exchange,
            makerAccount,
            notional,
            (*exchange)[sfMakerFee],
            std::as_const(*order)[~sfBuilder],
            std::as_const(*order)[~sfBuilderFee].value_or(0));

        // The filled part of each order leaves its position's counted open size.
        if (!taker.reduceOnly)
        {
            auto const& open = taker.sell ? sfOpenSell : sfOpenBuy;
            (*taker.position)[open] = (*taker.position)[open] - quantity;
            view.update(taker.position);
        }
        if (!makerReduceOnly)
        {
            auto const& open = taker.sell ? sfOpenBuy : sfOpenSell;
            (*makerPosition)[open] = (*makerPosition)[open] - quantity;
            view.update(makerPosition);
        }
        (*order)[sfSize] = (*order)[sfSize] - quantity;
        view.update(order);
        taker.remaining = taker.remaining - quantity;
        result.filled = result.filled + quantity;

        if ((*order)[sfSize] == Number{} ||
            (makerReduceOnly && (*makerPosition)[sfSize] == Number{}))
        {
            if (auto const ter = removeOrder(view, order, j); !isTesSuccess(ter))
                return std::unexpected(ter);  // LCOV_EXCL_LINE
        }
    }

    associateAsset(*exchange, (*exchange)[sfAsset]);
    return result;
}

namespace {

/**
 * Raises the opposite side's loss index of each loss market by its share of remaining over that
 * side's open interest (spec 9.6 step 3). Shares follow the weights, rounded downward onto the
 * collateral grid, with the rounding remainder on the first heaviest market.
 *
 * @return The amount the loss indexes took; a market with no opposite open interest takes nothing.
 */
[[nodiscard]] Number
chargeLossIndexes(
    ApplyView& view,
    SLE::Ref exchange,
    Number const& remaining,
    std::vector<LossMarket> const& lossMarkets)
{
    Number totalWeight;
    std::size_t heaviest = lossMarkets.size();
    for (std::size_t i = 0; i < lossMarkets.size(); ++i)
    {
        Number const weight = lossMarkets[i].weight;
        if (weight <= Number{})
            continue;
        totalWeight = totalWeight + weight;
        if (heaviest == lossMarkets.size() || weight > lossMarkets[heaviest].weight)
            heaviest = i;
    }
    if (totalWeight <= Number{})
        return Number{};

    std::vector<Number> shares(lossMarkets.size());
    Number assigned;
    {
        NumberRoundModeGuard const mg(Number::RoundingMode::Downward);
        for (std::size_t i = 0; i < lossMarkets.size(); ++i)
        {
            if (lossMarkets[i].weight <= Number{})
                continue;
            shares[i] = roundCollateral(
                *exchange,
                remaining * lossMarkets[i].weight / totalWeight,
                Number::RoundingMode::Downward);
            assigned = assigned + shares[i];
        }
    }
    shares[heaviest] = shares[heaviest] + (remaining - assigned);

    Number charged;
    for (std::size_t i = 0; i < lossMarkets.size(); ++i)
    {
        auto const& lossMarket = lossMarkets[i];
        if (shares[i] <= Number{})
            continue;
        auto const& market = lossMarket.market;
        Number const opposite =
            lossMarket.wasLong ? (*market)[sfOpenInterestShort] : (*market)[sfOpenInterestLong];
        if (opposite <= Number{})
            continue;

        // The rise rounds upward, so the side's charges cover at least its share.
        auto const& index = lossMarket.wasLong ? sfLossIndexShort : sfLossIndexLong;
        NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
        (*market)[index] = (*market)[index] + (shares[i] / opposite);
        view.update(market);
        (*exchange)[sfSettlementBalance] = (*exchange)[sfSettlementBalance] - shares[i];
        charged = charged + shares[i];
    }
    return charged;
}

}  // namespace

void
runWaterfall(
    ApplyView& view,
    SLE::Ref account,
    SLE::Ref exchange,
    Number const& shortfall,
    std::vector<LossMarket> const& lossMarkets)
{
    XRPL_ASSERT(
        account && account->getType() == ltMARGIN_ACCOUNT && exchange &&
            exchange->getType() == ltPERP_EXCHANGE && (*account)[sfExchangeID] == exchange->key(),
        "xrpl::perp::runWaterfall : account of the exchange");
    if (shortfall <= Number{})
        return;

    Number remaining = shortfall;

    // 1-2. The insurance fund, then the owner's cover.
    for (auto const* field : {&sfInsuranceFund, &sfCoverAvailable})
    {
        Number const held = (*exchange)[*field];
        Number const taken = std::min(remaining, held);
        if (taken <= Number{})
            continue;
        (*exchange)[*field] = held - taken;
        remaining = remaining - taken;
    }

    // 3. The opposite sides' loss indexes.
    if (remaining > Number{})
        remaining = remaining - chargeLossIndexes(view, exchange, remaining, lossMarkets);

    // 4. Bad debt, recovered by the withdrawal haircut.
    if (remaining > Number{})
        (*exchange)[sfBadDebt] = (*exchange)[sfBadDebt] + remaining;

    (*account)[sfCash] = (*account)[sfCash] + shortfall;
    (*account)[sfWaterfallCredit] = (*account)[sfWaterfallCredit] + (shortfall - remaining);
    (*exchange)[sfCashTotal] = (*exchange)[sfCashTotal] + shortfall;
    associateAsset(*account, (*exchange)[sfAsset]);
    associateAsset(*exchange, (*exchange)[sfAsset]);
    view.update(account);
    view.update(exchange);
}

}  // namespace xrpl::perp

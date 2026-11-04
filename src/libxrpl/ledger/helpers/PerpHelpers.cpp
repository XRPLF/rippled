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
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SOTemplate.h>
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
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
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
    if (age > std::int64_t{market[sfIndexMaxAge]})
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

MarkResult
computeMark(MarkInput const& input)
{
    NumberRoundModeGuard const mg(Number::RoundingMode::ToNearest);
    Number const& index = input.index;

    // The book mid counts only from an uncrossed pair no wider than sfMidSpreadMax.
    std::optional<Number> mid;
    if (input.bid && input.ask && index > Number{} && *input.bid < *input.ask &&
        (*input.ask - *input.bid) / index <= rateOf(input.midSpreadMax))
    {
        mid = (*input.bid + *input.ask) / Number{2};
    }

    Number const sample = mid ? *mid - index : Number{};
    Number const window{input.basisWindow};
    Number const half{5, -1};
    Number const alpha =
        input.basisWindow == 0 ? half : std::min(Number{input.elapsed}, window * half) / window;
    Number const basisEMA = input.basisEMA + (alpha * (sample - input.basisEMA));

    std::array<Number, 3> candidates{index, index + basisEMA, mid.value_or(index)};
    std::ranges::sort(candidates);
    Number const raw = candidates[1];

    Number moved = raw;
    if (input.prevMark > Number{})
    {
        Number const moveMax = rateOf(input.markMoveMax);
        moved = clampNumber(
            raw, input.prevMark * (Number{1} - moveMax), input.prevMark * (Number{1} + moveMax));
    }

    Number const band = rateOf(input.markBand);
    return {
        .mark = clampNumber(moved, index * (Number{1} - band), index * (Number{1} + band)),
        .basisEMA = basisEMA};
}

Number
computeFundingRate(
    Number const& mark,
    Number const& index,
    std::int32_t interestRate,
    std::uint32_t fundingCap)
{
    if (index <= Number{})
        return Number{};

    NumberRoundModeGuard const mg(Number::RoundingMode::ToNearest);
    Number const premium = (mark - index) / index;
    Number const interestClamp = rateOf(kFundingInterestClamp);
    Number const cap = rateOf(fundingCap);
    Number const interest =
        clampNumber(rateOf(interestRate) - premium, -interestClamp, interestClamp);
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
applyPendingParameters(STLedgerEntry& entry, std::uint32_t now)
{
    auto const effective = entry[~sfParameterEffectiveTime];
    if (!entry.isFieldPresent(sfPendingParameters) || !effective || *effective > now)
        return false;

    STObject const pending = entry.getFieldObject(sfPendingParameters);
    for (auto const& field : pending)
    {
        auto const& name = field.getFName();
        if (field.getSType() == STI_NOTPRESENT)
            continue;
        if (entry.getFieldIndex(name) == -1)
        {
            UNREACHABLE("xrpl::perp::applyPendingParameters : field outside the entry");
            continue;
        }

        switch (field.getSType())
        {
            case STI_UINT32:
                entry.setFieldU32(name, pending.getFieldU32(name));
                break;
            case STI_INT32:
                entry.setFieldI32(name, pending.getFieldI32(name));
                break;
            case STI_NUMBER:
                entry.setFieldNumber(name, pending.getFieldNumber(name));
                break;
            case STI_ARRAY:
                entry.setFieldArray(name, pending.getFieldArray(name));
                break;
            default:
                UNREACHABLE("xrpl::perp::applyPendingParameters : unexpected field type");
                continue;
        }

        // A default-styled field holding its default value is stored absent.
        if (entry.getStyle(name) == SoeDefault && entry.peekAtField(name).isDefault())
            entry.makeFieldAbsent(name);
    }

    entry.makeFieldAbsent(sfPendingParameters);
    entry.makeFieldAbsent(sfParameterEffectiveTime);
    return true;
}

std::uint32_t
parameterEffectiveTime(std::uint32_t now, std::uint32_t delay)
{
    std::uint64_t const effective = std::uint64_t{now} + delay;
    return static_cast<std::uint32_t>(
        std::min<std::uint64_t>(effective, std::numeric_limits<std::uint32_t>::max()));
}

STLedgerEntry
eventualParameters(STLedgerEntry const& entry)
{
    STLedgerEntry eventual{entry};
    applyPendingParameters(eventual, std::numeric_limits<std::uint32_t>::max());
    return eventual;
}

bool
tiersLoosen(STArray const& current, STArray const& proposed)
{
    if (current.empty() || proposed.empty())
        return !proposed.empty();

    Number const proposedMax = proposed.back()[sfNotionalMax];
    if (proposedMax > current.back()[sfNotionalMax])
        return true;

    // Both requirements are linear between tier bounds, so comparing at every bound suffices.
    for (auto const* tiers : {&current, &proposed})
    {
        for (auto const& tier : *tiers)
        {
            Number const bound = tier[sfNotionalMax];
            if (bound > proposedMax)
                continue;
            for (bool const initial : {true, false})
            {
                auto const before = tierRequirement(bound, current, initial);
                auto const after = tierRequirement(bound, proposed, initial);
                if (before && after && *after < *before)
                    return true;
            }
        }
    }
    return false;
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
    bool exchangeChanged = applyPendingParameters(*exchange, t);
    applyPendingParameters(*market, t);

    std::uint32_t const lastRefresh = (*market)[sfLastRefreshTime];
    std::uint32_t const elapsed = t > lastRefresh ? t - lastRefresh : 0;
    std::uint8_t const status = (*market)[sfMarketStatus];

    // Funding accrues at the rate fixed by the previous refresh.
    if (status == kMarketLive)
    {
        (*market)[sfCumulativeFunding] = (*market)[sfCumulativeFunding] +
            fundingAccrual((*market)[sfFundingRate], (*market)[sfIndexPrice], elapsed);
    }

    auto const index = computeIndex(view, *market);
    if (!index)
    {
        JLOG(j.debug()) << "perp::refreshMarket: no index quorum for market " << market->key();
        if (status != kMarketSettled)
        {
            (*market)[sfMarketStatus] = kMarketStale;
            if (!market->isFieldPresent(sfStaleSince))
                (*market)[sfStaleSince] = t;
        }
        (*market)[sfFundingRate] = Number{};
    }
    else
    {
        if (status != kMarketSettled)
        {
            (*market)[sfMarketStatus] = kMarketLive;
            (*market)[~sfStaleSince] = std::nullopt;
        }

        Number const oldMark = (*market)[sfMarkPrice];
        auto const mark = computeMark(
            {.index = *index,
             .prevMark = oldMark,
             .basisEMA = (*market)[sfBasisEMA],
             .elapsed = elapsed,
             .bid = bestPrice(view, market->key(), false),
             .ask = bestPrice(view, market->key(), true),
             .basisWindow = (*market)[sfBasisWindow],
             .midSpreadMax = (*market)[sfMidSpreadMax],
             .markMoveMax = (*market)[sfMarkMoveMax],
             .markBand = (*market)[sfMarkBand]});

        (*market)[sfIndexPrice] = *index;
        (*market)[sfMarkPrice] = mark.mark;
        (*market)[sfBasisEMA] = mark.basisEMA;
        (*market)[sfFundingRate] = computeFundingRate(
            mark.mark, *index, (*market)[sfFundingInterestRate], (*market)[sfFundingCap]);

        Number const notionalDelta = (*market)[sfOpenInterestLong] * (mark.mark - oldMark);
        if (notionalDelta != Number{})
        {
            (*exchange)[sfOpenInterestNotional] =
                (*exchange)[sfOpenInterestNotional] + notionalDelta;
            exchangeChanged = true;
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

    // Every charge is on the collateral grid, so the sum is exact.
    Number charges;
    bool holdsPosition = false;
    for (auto const& marketID : account->getFieldV256(sfMarkets))
    {
        auto const market = view.peek(keylet::perpMarket(marketID));
        auto const position = view.peek(keylet::marginPosition(account->key(), marketID));
        if (!market || !position)
        {
            JLOG(j.fatal()) << "perp::settleAccount: missing market or position " << marketID;
            return tefBAD_LEDGER;
        }

        refreshMarket(view, market, exchange, j);
        auto const charge = settlementCharge(*exchange, *market, *position);
        charges = charges + charge.funding + charge.loss;

        if ((*position)[sfSize] != Number{})
            holdsPosition = true;
        if (snapshotIndexes(*position, *market))
            view.update(position);
    }

    if (charges != Number{})
    {
        (*account)[sfCash] = (*account)[sfCash] - charges;
        (*exchange)[sfCashTotal] = (*exchange)[sfCashTotal] - charges;
        (*exchange)[sfSettlementBalance] = (*exchange)[sfSettlementBalance] + charges;
        view.update(account);
        view.update(exchange);
    }

    if (!holdsPosition)
        repayWaterfallCredit(view, account, exchange);
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

[[nodiscard]] std::expected<MarginState, TER>
marginState(
    ReadView const& view,
    STLedgerEntry const& account,
    STLedgerEntry const* override = nullptr)
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
        if ((*market)[sfMarketStatus] != kMarketLive)
            return std::unexpected<TER>(tecPERP_STALE_PRICE);

        auto const charge = settlementCharge(*exchange, *market, *position);
        state.cash = state.cash - charge.funding - charge.loss;

        Number const mark = (*market)[sfMarkPrice];
        Number const size = (*position)[sfSize];
        {
            NumberRoundModeGuard const mg(Number::RoundingMode::Downward);
            Number const pnl = (size * mark) - (*position)[sfEntryNotional];
            state.unrealized = state.unrealized + pnl;
            state.losses = state.losses + std::min(pnl, Number{});
        }

        auto const& tiers = market->getFieldArray(sfMarginTiers);
        NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
        Number const worstCase =
            std::max(abs(size + (*position)[sfOpenBuy]), abs(size - (*position)[sfOpenSell]));
        Number const initial = requirement(worstCase * mark, tiers, true);
        Number const maintenance = requirement(abs(size) * mark, tiers, false);
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
            (*exchange)[sfOpenInterestNotional] + (longDelta * (*market)[sfMarkPrice]);
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
 * Charges one side of a fill its trading fee, split to the owner, the vault and the insurance
 * fund, and its builder fee, paid to the builder's margin account or else the insurance fund
 * (spec 12). The vault share accrues to vaultFees, or to the insurance fund when unset.
 */
void
chargeFees(
    ApplyView& view,
    SLE::Ref exchange,
    SLE::Ref account,
    Number const& notional,
    std::uint32_t rate,
    std::optional<AccountID> const& builder,
    std::uint32_t builderRate,
    std::optional<Number>& vaultFees)
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
        Number vaultPart;
        {
            NumberRoundModeGuard const mg(Number::RoundingMode::Downward);
            ownerPart = roundCollateral(
                *exchange,
                fee * rateOf((*exchange)[sfOwnerFeeShare]),
                Number::RoundingMode::Downward);
            vaultPart = roundCollateral(
                *exchange,
                fee * rateOf((*exchange)[sfVaultFeeShare]),
                Number::RoundingMode::Downward);
        }
        (*exchange)[sfCashTotal] = (*exchange)[sfCashTotal] - fee;
        (*exchange)[sfOwnerFees] = (*exchange)[sfOwnerFees] + ownerPart;
        insurance = fee - ownerPart - vaultPart;
        if (vaultFees)
        {
            *vaultFees = *vaultFees + vaultPart;
        }
        else
        {
            insurance = insurance + vaultPart;
        }
    }

    if (builder && builderFee != Number{})
    {
        auto const builderAccount = view.peek(keylet::marginAccount(*builder, exchange->key(), 0));
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
 * Whether the exchange's vault takes the vault fee share: it is bound and has shares, since a
 * vault without shares may hold no assets.
 */
[[nodiscard]] bool
vaultTakesFees(ReadView const& view, STLedgerEntry const& exchange)
{
    auto const vaultID = exchange[~sfVaultID];
    if (!vaultID)
        return false;
    auto const vault = view.read(keylet::vault(*vaultID));
    if (!vault)
        return false;  // LCOV_EXCL_LINE
    auto const shares = view.read(keylet::mptokenIssuance((*vault)[sfShareMPTID]));
    return shares && (*shares)[sfOutstandingAmount] != 0;
}

/**
 * Moves the vault fee share from the exchange's pseudo-account to the vault's, raising the
 * vault's balances so that its shares appreciate.
 */
[[nodiscard]] TER
payVaultFees(ApplyView& view, SLE::Ref exchange, Number const& amount, beast::Journal j)
{
    auto const vault = view.peek(keylet::vault((*exchange)[sfVaultID]));
    if (!vault)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    Asset const asset = (*exchange)[sfAsset];
    STAmount const paid{asset, amount};
    if (auto const ter = accountSend(
            view, (*exchange)[sfAccount], (*vault)[sfAccount], paid, j, {}, WaiveTransferFee::Yes);
        !isTesSuccess(ter))
        return ter;
    if (auto const ter = adjustVaultCash(vault, paid, j); !isTesSuccess(ter))
        return ter;  // LCOV_EXCL_LINE
    associateAsset(*vault, asset);
    view.update(vault);

    (*exchange)[sfAssetsTotal] = (*exchange)[sfAssetsTotal] - amount;
    view.update(exchange);
    return tesSUCCESS;
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
    std::optional<Number> vaultFees;
    if (vaultTakesFees(view, *exchange))
        vaultFees = Number{};

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
            (*exchange)[sfTakerFee],
            taker.builder,
            taker.builderFee,
            vaultFees);
        chargeFees(
            view,
            exchange,
            makerAccount,
            notional,
            (*exchange)[sfMakerFee],
            std::as_const(*order)[~sfBuilder],
            std::as_const(*order)[~sfBuilderFee].value_or(0),
            vaultFees);

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

    if (vaultFees && *vaultFees != Number{})
    {
        if (auto const ter = payVaultFees(view, exchange, *vaultFees, j); !isTesSuccess(ter))
            return std::unexpected(ter);
    }
    associateAsset(*exchange, (*exchange)[sfAsset]);
    return result;
}

}  // namespace xrpl::perp

#include <xrpl/ledger/helpers/PerpHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SOTemplate.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TER.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
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

        bool changed = false;
        Number const cumulative = (*market)[sfCumulativeFunding];
        if ((*position)[sfFundingIndex] != cumulative)
        {
            (*position)[sfFundingIndex] = cumulative;
            changed = true;
        }
        if (Number const size = (*position)[sfSize]; size != Number{})
        {
            holdsPosition = true;
            Number const sideIndex =
                size > Number{} ? (*market)[sfLossIndexLong] : (*market)[sfLossIndexShort];
            if ((*position)[sfLossIndex] != sideIndex)
            {
                (*position)[sfLossIndex] = sideIndex;
                changed = true;
            }
        }
        if (changed)
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
marginState(ReadView const& view, STLedgerEntry const& account)
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
        auto const position = view.read(keylet::marginPosition(account.key(), marketID));
        if (!market || !position)
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

}  // namespace xrpl::perp

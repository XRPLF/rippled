#include <xrpl/ledger/helpers/PerpHelpers.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
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

}  // namespace xrpl::perp

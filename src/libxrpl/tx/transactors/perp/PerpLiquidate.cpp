#include <xrpl/tx/transactors/perp/PerpLiquidate.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/PerpHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTakesAsset.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <algorithm>
#include <cstdint>
#include <expected>
#include <optional>
#include <vector>

namespace xrpl {

namespace {

[[nodiscard]] Number
rateOf(std::uint32_t tenthBips)
{
    return Number{tenthBips} / Number{perp::kRateUnity};
}

/**
 * The loss markets of a takeover's waterfall (spec 9.4 step 1): every market where the account
 * holds a size, weighted by that position's notional at the market's sfIndexPrice.
 */
[[nodiscard]] std::expected<std::vector<perp::LossMarket>, TER>
lossMarketsOf(ApplyView& view, STLedgerEntry const& account)
{
    std::vector<perp::LossMarket> lossMarkets;
    for (auto const& marketID : account.getFieldV256(sfMarkets))
    {
        auto const market = view.peek(keylet::perpMarket(marketID));
        auto const position = view.read(keylet::marginPosition(account.key(), marketID));
        if (!market || !position)
            return std::unexpected<TER>(tefBAD_LEDGER);  // LCOV_EXCL_LINE
        Number const size = (*position)[sfSize];
        if (size == Number{})
            continue;
        NumberRoundModeGuard const mg(Number::RoundingMode::ToNearest);
        lossMarkets.push_back(
            {.market = market,
             .weight = abs(size) * Number{(*market)[sfIndexPrice]},
             .wasLong = size > Number{}});
    }
    return lossMarkets;
}

/**
 * The size of spec 9.2: kLiquidationChunk of the position rounded up to a lot when equity is at
 * least two thirds of the maintenance requirement and the notional is above the exchange's
 * sfLiquidationChunkMin, else the whole position.
 */
[[nodiscard]] Number
liquidationSize(
    STLedgerEntry const& market,
    STLedgerEntry const& exchange,
    Number const& held,
    perp::MarginSummary const& summary)
{
    Number const whole = abs(held);
    Number notional;
    {
        NumberRoundModeGuard const mg(Number::RoundingMode::ToNearest);
        notional = whole * market[sfIndexPrice];
    }
    if (Number{3} * summary.equity < Number{2} * summary.requirements.maintenance ||
        notional <= exchange[sfLiquidationChunkMin])
        return whole;

    Number const lot = market[sfLotSize];
    NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
    Number const lots = whole * rateOf(perp::kLiquidationChunk) / lot;
    Number wholeLots = lots.truncate();
    if (wholeLots < lots)
        wholeLots = wholeLots + Number{1};
    return std::min(whole, wholeLots * lot);
}

}  // namespace

std::uint32_t
PerpLiquidate::getFlagsMask(PreflightContext const& ctx)
{
    return tfPerpLiquidateMask;
}

NotTEC
PerpLiquidate::preflight(PreflightContext const& ctx)
{
    auto const& tx = ctx.tx;
    if (tx.isFlag(tfPerpLiquidateToBook) == tx.isFlag(tfPerpLiquidateTakeover))
        return temINVALID_FLAG;

    if (tx[sfMarginAccountID] == beast::kZero || tx[sfMarketID] == beast::kZero)
        return temMALFORMED;

    if (auto const size = tx[~sfSize]; size && size->signum() <= 0)
        return temMALFORMED;

    return tesSUCCESS;
}

TER
PerpLiquidate::preclaim(PreclaimContext const& ctx)
{
    auto const& tx = ctx.tx;
    auto const market = ctx.view.read(keylet::perpMarket(tx[sfMarketID]));
    if (!market)
        return tecNO_ENTRY;
    auto const target = ctx.view.read(keylet::marginAccount(tx[sfMarginAccountID]));
    if (!target || (*target)[sfExchangeID] != (*market)[sfExchangeID])
        return tecNO_ENTRY;
    auto const exchange = ctx.view.read(keylet::perpExchange((*market)[sfExchangeID]));
    if (!exchange)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    // The liquidator acts through its own margin account on the exchange.
    auto const liquidator = keylet::marginAccount(tx[sfAccount], exchange->key());
    if (!ctx.view.exists(liquidator))
        return tecNO_ENTRY;

    auto const position = ctx.view.read(keylet::marginPosition(target->key(), market->key()));
    if (!position || (*position)[sfSize] == Number{})
        return tecNO_ENTRY;

    if (tx.isFlag(tfPerpLiquidateTakeover))
    {
        // An account cannot take over its own position.
        if (liquidator.key == target->key())
            return tecNO_PERMISSION;
        if (auto const domainID = (*exchange)[~sfDomainID])
        {
            if (auto const ter = perp::checkDomain(ctx.view, *domainID, tx[sfAccount]);
                !isTesSuccess(ter))
                return ter;
        }
    }
    return tesSUCCESS;
}

TER
PerpLiquidate::doApply()
{
    auto const& tx = ctx_.tx;
    bool const takeover = tx.isFlag(tfPerpLiquidateTakeover);
    auto const market = view().peek(keylet::perpMarket(tx[sfMarketID]));
    auto const target = view().peek(keylet::marginAccount(tx[sfMarginAccountID]));
    auto const root = view().peek(keylet::account(accountID_));
    if (!market || !target || !root)
        return tefINTERNAL;  // LCOV_EXCL_LINE
    auto const exchange = view().peek(keylet::perpExchange((*market)[sfExchangeID]));
    if (!exchange)
        return tefINTERNAL;  // LCOV_EXCL_LINE
    auto const liquidator = view().peek(keylet::marginAccount(accountID_, exchange->key()));
    auto const position = view().peek(keylet::marginPosition(target->key(), market->key()));
    if (!liquidator || !position)
        return tefINTERNAL;  // LCOV_EXCL_LINE
    std::uint32_t const ownerCount = (*root)[sfOwnerCount];

    // 1. The named market must be Live; settling the target refreshes all its markets.
    perp::refreshMarket(view(), market, exchange, j_);
    if ((*market)[sfMarketStatus] != perp::kMarketLive)
        return tecPERP_STALE_PRICE;
    if (auto const ter = perp::settleAccount(view(), target, exchange, j_); !isTesSuccess(ter))
        return ter;
    if (liquidator->key() != target->key())
    {
        if (auto const ter = perp::settleAccount(view(), liquidator, exchange, j_);
            !isTesSuccess(ter))
            return ter;
    }

    // 2. The target must be below its maintenance requirement, with Stale markets at their last
    //    quorum index.
    auto const before = perp::liquidationSummary(view(), *target);
    if (!before)
        return before.error();
    if (before->equity >= before->requirements.maintenance)
        return tecPERP_HEALTHY;

    // 3. The size rule, lowered by the transaction's sfSize.
    Number const held = (*position)[sfSize];
    Number const index = (*market)[sfIndexPrice];
    Number size = liquidationSize(*market, *exchange, held, *before);
    if (auto const requested = tx[~sfSize])
    {
        if (!perp::isMultiple(*requested, (*market)[sfLotSize]))
            return tecPRECISION_LOSS;
        size = std::min(size, *requested);
    }
    bool const wasLong = held > Number{};

    Number liquidated;
    if (!takeover)
    {
        // 4. Book mode: an immediate-or-cancel reducing order for the target, without fees.
        Number limit;
        {
            NumberRoundModeGuard const mg(Number::RoundingMode::ToNearest);
            Number const band = rateOf((*market)[sfOrderPriceBand]);
            limit = wasLong ? index * (Number{1} - band) : index * (Number{1} + band);
        }
        perp::Taker taker{
            .account = target,
            .position = position,
            .owner = (*target)[sfAccount],
            .limitPrice = limit,
            .remaining = size,
            .sell = wasLong,
            .reduceOnly = true,
            .postOnly = false,
            .builder = std::nullopt,
            .builderFee = 0,
            .noTakerFee = true};
        auto const match = perp::matchOrder(view(), market, exchange, taker, j_);
        if (!match)
            return match.error();
        if (match->filled == Number{})
            return tecPERP_NO_LIQUIDITY;
        liquidated = match->filled;
    }
    else
    {
        // 4. Takeover mode: a negative equity is paid first, exactly, since the move is at the
        //    index.
        if (auto const domainID = (*exchange)[~sfDomainID])
        {
            if (auto const ter = perp::verifyDomain(view(), *domainID, accountID_, j_);
                !isTesSuccess(ter))
                return ter;
        }
        if (before->equity < Number{})
        {
            auto const lossMarkets = lossMarketsOf(view(), *target);
            if (!lossMarkets)
                return lossMarkets.error();  // LCOV_EXCL_LINE
            Number const shortfall =
                perp::roundCollateral(*exchange, -before->equity, Number::RoundingMode::Upward);
            perp::runWaterfall(view(), target, exchange, shortfall, *lossMarkets);
        }

        // The target closes the size at the index and the liquidator opens it the same way.
        Number const closing = wasLong ? -size : size;
        perp::applyFill(view(), target, position, market, exchange, closing, index);
        auto taken = view().peek(keylet::marginPosition(liquidator->key(), market->key()));
        if (!taken)
        {
            auto const opened = perp::openPosition(view(), liquidator, *market, j_);
            if (!opened)
                return opened.error();
            taken = *opened;
        }
        perp::applyFill(view(), liquidator, taken, market, exchange, -closing, index);

        // No order may take a notional above the last tier, and neither may a takeover.
        Number const takenSize = (*taken)[sfSize];
        Number const worstCase =
            std::max(abs(takenSize + (*taken)[sfOpenBuy]), abs(takenSize - (*taken)[sfOpenSell]));
        auto const& tiers = market->getFieldArray(sfMarginTiers);
        if (tiers.empty() || worstCase * index > tiers.back()[sfNotionalMax])
            return tecPERP_POSITION_LIMIT;
        liquidated = size;
    }

    // 5. The exchange's penalty, capped by the target's equity, is shared with the liquidator, or
    //    the insurance fund pays the exchange's minimum reward when there is no penalty.
    auto const after = perp::liquidationSummary(view(), *target);
    if (!after)
        return after.error();  // LCOV_EXCL_LINE
    Number penalty;
    {
        NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
        penalty = perp::roundCollateral(
            *exchange,
            liquidated * index * rateOf((*exchange)[sfLiquidationPenalty]),
            Number::RoundingMode::Upward);
    }
    penalty = std::min(
        penalty,
        perp::roundCollateral(
            *exchange, std::max(after->equity, Number{}), Number::RoundingMode::Downward));
    bool const sameOwner = (*liquidator)[sfAccount] == (*target)[sfAccount];
    Number reward;
    if (penalty > Number{})
    {
        if (!sameOwner)
        {
            NumberRoundModeGuard const mg(Number::RoundingMode::Downward);
            reward = perp::roundCollateral(
                *exchange,
                penalty * rateOf(perp::kLiquidationRewardShare),
                Number::RoundingMode::Downward);
        }
        (*target)[sfCash] = (*target)[sfCash] - penalty;
        (*exchange)[sfCashTotal] = (*exchange)[sfCashTotal] - penalty;
        (*exchange)[sfInsuranceFund] =
            (*exchange)[sfInsuranceFund] + perp::absorbBadDebt(*exchange, penalty - reward);
    }
    else if (!sameOwner)
    {
        reward = std::min(
            Number{(*exchange)[sfLiquidationRewardMin]}, Number{(*exchange)[sfInsuranceFund]});
        (*exchange)[sfInsuranceFund] = (*exchange)[sfInsuranceFund] - reward;
    }
    if (reward > Number{})
    {
        (*liquidator)[sfCash] = (*liquidator)[sfCash] + reward;
        (*exchange)[sfCashTotal] = (*exchange)[sfCashTotal] + reward;
    }

    // 6. A flat target with negative cash has realized its shortfall, which the waterfall pays.
    if (auto const ter = perp::deletePositionIfEmpty(view(), position, j_); !isTesSuccess(ter))
        return ter;  // LCOV_EXCL_LINE
    auto const flat = perp::isFlat(view(), *target);
    if (!flat)
        return flat.error();  // LCOV_EXCL_LINE
    if (*flat && (*target)[sfCash] < Number{})
    {
        perp::runWaterfall(
            view(),
            target,
            exchange,
            -Number{(*target)[sfCash]},
            {{.market = market, .weight = Number{1}, .wasLong = wasLong}});
    }
    if (*flat)
        perp::repayWaterfallCredit(view(), target, exchange);

    // 7. A takeover leaves the liquidator at or above its initial requirement.
    if (takeover)
    {
        auto const summary = perp::marginSummary(view(), *liquidator);
        if (!summary)
            return summary.error();
        if (summary->equity < summary->requirements.initial)
            return tecPERP_INSUFFICIENT_MARGIN;
        if ((*root)[sfOwnerCount] > ownerCount && preFeeBalance_ < accountReserve(view(), root, j_))
            return tecINSUFFICIENT_RESERVE;
    }

    Asset const asset = (*exchange)[sfAsset];
    associateAsset(*target, asset);
    associateAsset(*liquidator, asset);
    associateAsset(*exchange, asset);
    view().update(target);
    view().update(liquidator);
    view().update(exchange);
    return tesSUCCESS;
}

void
PerpLiquidate::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
{
}

bool
PerpLiquidate::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl

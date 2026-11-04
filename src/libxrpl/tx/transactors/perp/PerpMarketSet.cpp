#include <xrpl/tx/transactors/perp/PerpMarketSet.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/PerpHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <utility>

namespace xrpl {

bool
PerpMarketSet::checkExtraFeatures(PreflightContext const& ctx)
{
    return ctx.rules.enabled(featureSingleAssetVault);
}

std::uint32_t
PerpMarketSet::getFlagsMask(PreflightContext const& ctx)
{
    return tfPerpMarketSetMask;
}

namespace {

/**
 * Whether the named oracles are 1 to kMaxPerpOracles distinct (owner, document) pairs.
 */
bool
validOracles(STArray const& oracles)
{
    if (oracles.empty() || oracles.size() > perp::kMaxPerpOracles)
        return false;

    std::set<std::pair<AccountID, std::uint32_t>> seen;
    for (auto const& oracle : oracles)
    {
        if (oracle[sfOwner] == beast::kZero ||
            !seen.emplace(oracle[sfOwner], oracle[sfOracleDocumentID]).second)
            return false;
    }
    return true;
}

/**
 * Whether the tiers are 1 to kMaxMarginTiers, ascending in notional, with
 * initial above maintenance and both rates rising from tier to tier.
 */
bool
validTiers(STArray const& tiers)
{
    if (tiers.empty() || tiers.size() > perp::kMaxMarginTiers)
        return false;

    std::optional<STObject> previous;
    for (auto const& tier : tiers)
    {
        Number const notionalMax = tier[sfNotionalMax];
        std::uint32_t const initial = tier[sfInitialMarginRate];
        std::uint32_t const maintenance = tier[sfMaintenanceMarginRate];
        if (notionalMax.signum() <= 0 || maintenance == 0 || initial <= maintenance ||
            initial > perp::kRateUnity)
            return false;

        if (previous &&
            (notionalMax <= (*previous)[sfNotionalMax] ||
             initial <= (*previous)[sfInitialMarginRate] ||
             maintenance <= (*previous)[sfMaintenanceMarginRate]))
            return false;
        previous = tier;
    }
    return true;
}

}  // namespace

NotTEC
PerpMarketSet::preflight(PreflightContext const& ctx)
{
    auto const& tx = ctx.tx;
    if (tx.isFlag(tfInnerBatchTxn))
        return temINVALID_INNER_BATCH;

    if (auto const marketID = tx[~sfMarketID])
    {
        // An update may not change the fixed parameters or the isolation flag.
        std::array<SField const*, 5> const fixed{
            &sfExchangeID, &sfBaseAsset, &sfQuoteAsset, &sfPriceTick, &sfLotSize};
        for (auto const* field : fixed)
        {
            if (tx.isFieldPresent(*field))
                return temMALFORMED;
        }
        if (*marketID == beast::kZero || tx.isFlag(tfPerpIsolatedOnly))
            return temMALFORMED;
    }
    else
    {
        // Creation carries every required parameter of the market.
        std::array<SField const*, 20> const required{
            &sfExchangeID,
            &sfBaseAsset,
            &sfQuoteAsset,
            &sfIndexOracles,
            &sfIndexMaxAge,
            &sfIndexMaxLedgers,
            &sfPriceTick,
            &sfLotSize,
            &sfMarginTiers,
            &sfOpenInterestMax,
            &sfOrderPriceBand,
            &sfMidSpreadMax,
            &sfMarkBand,
            &sfMarkMoveMax,
            &sfBasisWindow,
            &sfFundingCap,
            &sfLiquidationPenalty,
            &sfLiquidationRewardShare,
            &sfLiquidationChunkMin,
            &sfSettleStaleAfter};
        for (auto const* field : required)
        {
            if (!tx.isFieldPresent(*field))
                return temMALFORMED;
        }
        if (tx[sfExchangeID] == beast::kZero || tx[sfBaseAsset] == tx[sfQuoteAsset] ||
            tx.isFlag(tfPerpMarketSettle))
            return temMALFORMED;
    }

    if (tx.isFieldPresent(sfIndexOracles) && !validOracles(tx.getFieldArray(sfIndexOracles)))
        return temMALFORMED;

    if (tx.isFieldPresent(sfMarginTiers) && !validTiers(tx.getFieldArray(sfMarginTiers)))
        return temMALFORMED;

    // Increments, caps and chunk sizes are positive; the reward floor is not negative.
    for (auto const* field : {&sfPriceTick, &sfLotSize, &sfOpenInterestMax, &sfLiquidationChunkMin})
    {
        if (auto const value = tx[~*field]; value && value->signum() <= 0)
            return temMALFORMED;
    }
    if (auto const rewardMin = tx[~sfLiquidationRewardMin]; rewardMin && rewardMin->signum() < 0)
        return temMALFORMED;

    for (auto const* field :
         {&sfOrderPriceBand,
          &sfMidSpreadMax,
          &sfMarkBand,
          &sfMarkMoveMax,
          &sfLiquidationPenalty,
          &sfLiquidationRewardShare})
    {
        if (!validNumericRange(tx[~*field], perp::kRateUnity))
            return temMALFORMED;
    }

    if (!validNumericRange(tx[~sfFundingCap], perp::kMaxFundingCap))
        return temMALFORMED;

    if (tx.isFieldPresent(sfMidSpreadMax) && tx.isFieldPresent(sfOrderPriceBand) &&
        tx[sfMidSpreadMax] >= tx[sfOrderPriceBand])
        return temMALFORMED;

    if (tx[~sfIndexMaxAge] == 0u || tx[~sfIndexMaxLedgers] == 0u || tx[~sfBasisWindow] == 0u)
        return temMALFORMED;

    if (auto const staleAfter = tx[~sfSettleStaleAfter];
        staleAfter && *staleAfter < perp::kMinSettleStaleAfter.count())
        return temMALFORMED;

    return tesSUCCESS;
}

TER
PerpMarketSet::preclaim(PreclaimContext const& ctx)
{
    auto const& tx = ctx.tx;
    auto const exchangeID = [&]() -> std::optional<uint256> {
        if (auto const marketID = tx[~sfMarketID])
        {
            auto const market = ctx.view.read(keylet::perpMarket(*marketID));
            if (!market)
                return std::nullopt;
            return (*market)[sfExchangeID];
        }
        return tx[sfExchangeID];
    }();
    if (!exchangeID)
        return tecNO_ENTRY;

    auto const exchange = ctx.view.read(keylet::perpExchange(*exchangeID));
    if (!exchange)
        return tecNO_ENTRY;
    if ((*exchange)[sfOwner] != tx[sfAccount])
        return tecNO_PERMISSION;

    if (auto const rewardMin = tx[~sfLiquidationRewardMin];
        rewardMin && !isRepresentable((*exchange)[sfAsset], *rewardMin))
        return tecPRECISION_LOSS;

    if (tx.isFieldPresent(sfMarketID))
        return tesSUCCESS;

    if (ctx.view.exists(keylet::perpMarket(*exchangeID, tx[sfBaseAsset], tx[sfQuoteAsset])))
        return tecDUPLICATE;

    if ((*exchange)[sfMarketCount] >= perp::kMaxPerpMarkets)
        return tecPERP_MARKET_LIMIT;

    return tesSUCCESS;
}

namespace {

/**
 * How a change of one market parameter is applied (spec 3.2, 4.3).
 */
enum class ChangeClass { Delayed, DelayedUp, DelayedDown };

/**
 * Queues the transaction's value of field into pending, or writes it to the market, by the
 * field's change class. An unchanged value is ignored.
 */
template <class T>
void
classifyChange(
    STLedgerEntry& market,
    STObject& pending,
    bool& queued,
    TypedField<T> const& field,
    std::optional<typename T::value_type> const& proposed,
    ChangeClass changeClass)
{
    if (!proposed)
        return;
    typename T::value_type const current = market[field];
    if (*proposed == current)
        return;

    bool const queue = changeClass == ChangeClass::Delayed ||
        (changeClass == ChangeClass::DelayedUp ? *proposed > current : *proposed < current);
    if (queue)
    {
        pending[field] = *proposed;
        queued = true;
    }
    else
    {
        market[field] = *proposed;
    }
}

}  // namespace

TER
PerpMarketSet::doApply()
{
    if (ctx_.tx.isFieldPresent(sfMarketID))
        return updateMarket();
    return createMarket();
}

TER
PerpMarketSet::createMarket()
{
    auto const& tx = ctx_.tx;
    auto const exchangeID = tx[sfExchangeID];
    auto const exchange = view().peek(keylet::perpExchange(exchangeID));
    auto const owner = view().peek(keylet::account(accountID_));
    if (!exchange || !owner)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    auto const market =
        std::make_shared<SLE>(keylet::perpMarket(exchangeID, tx[sfBaseAsset], tx[sfQuoteAsset]));
    (*market)[sfExchangeID] = exchangeID;
    (*market)[sfBaseAsset] = tx[sfBaseAsset];
    (*market)[sfQuoteAsset] = tx[sfQuoteAsset];
    market->setFieldArray(sfIndexOracles, tx.getFieldArray(sfIndexOracles));
    market->setFieldArray(sfMarginTiers, tx.getFieldArray(sfMarginTiers));
    for (auto const* field :
         {&sfIndexMaxAge,
          &sfIndexMaxLedgers,
          &sfOrderPriceBand,
          &sfMidSpreadMax,
          &sfMarkBand,
          &sfMarkMoveMax,
          &sfBasisWindow,
          &sfFundingCap,
          &sfLiquidationPenalty,
          &sfLiquidationRewardShare,
          &sfSettleStaleAfter})
        (*market)[*field] = tx[*field];
    for (auto const* field : {&sfPriceTick, &sfLotSize, &sfOpenInterestMax, &sfLiquidationChunkMin})
        (*market)[*field] = tx[*field];
    if (auto const interest = tx[~sfFundingInterestRate])
        (*market)[sfFundingInterestRate] = *interest;
    if (auto const rewardMin = tx[~sfLiquidationRewardMin])
    {
        (*market)[sfLiquidationRewardMin] =
            perp::roundCollateral(*exchange, *rewardMin, Number::RoundingMode::Downward);
    }
    if (tx.isFlag(tfPerpIsolatedOnly))
        market->setFlag(lsfPerpIsolatedOnly);

    // The market starts Live, so its index must meet the quorum now.
    auto const index = perp::computeIndex(view(), *market);
    if (!index)
        return index.error();

    if (auto const ter = dirLink(view(), accountID_, market))
        return ter;
    increaseOwnerCount(view(), owner, {}, 1, j_);
    if (preFeeBalance_ < accountReserve(view(), owner, j_))
        return tecINSUFFICIENT_RESERVE;

    {
        NumberRoundModeGuard const mg(Number::RoundingMode::ToNearest);
        (*market)[sfIndexPrice] = *index;
        (*market)[sfMarkPrice] = *index;
        (*market)[sfFundingRate] = perp::computeFundingRate(
            *index, *index, (*market)[sfFundingInterestRate], (*market)[sfFundingCap]);
    }
    (*market)[sfLastRefreshTime] = perp::refreshTime(view());
    (*market)[sfRefreshLedger] = view().seq();

    (*exchange)[sfMarketCount] = (*exchange)[sfMarketCount] + 1;
    view().update(exchange);
    view().insert(market);
    return tesSUCCESS;
}

TER
PerpMarketSet::updateMarket()
{
    auto const& tx = ctx_.tx;
    auto const market = view().peek(keylet::perpMarket(tx[sfMarketID]));
    if (!market)
        return tefINTERNAL;  // LCOV_EXCL_LINE
    auto const exchange = view().peek(keylet::perpExchange((*market)[sfExchangeID]));
    if (!exchange)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    // Funding accrues and due changes apply under the old parameters before any new one.
    perp::refreshMarket(view(), market, exchange, j_);
    auto const now = perp::refreshTime(view());

    STObject pending = STObject::makeInnerObject(sfPendingParameters);
    bool queued = false;
    auto const u32 = [&](SF_UINT32 const& field, ChangeClass changeClass) {
        classifyChange(*market, pending, queued, field, tx[~field], changeClass);
    };
    auto const number = [&](SF_NUMBER const& field, ChangeClass changeClass) {
        classifyChange(*market, pending, queued, field, tx[~field], changeClass);
    };

    for (auto const* field : {&sfBasisWindow, &sfLiquidationPenalty, &sfLiquidationRewardShare})
        u32(*field, ChangeClass::Delayed);
    for (auto const* field :
         {&sfIndexMaxAge,
          &sfIndexMaxLedgers,
          &sfOrderPriceBand,
          &sfMidSpreadMax,
          &sfMarkBand,
          &sfMarkMoveMax,
          &sfFundingCap})
        u32(*field, ChangeClass::DelayedUp);
    u32(sfSettleStaleAfter, ChangeClass::DelayedDown);
    classifyChange(
        *market,
        pending,
        queued,
        sfFundingInterestRate,
        tx[~sfFundingInterestRate],
        ChangeClass::Delayed);
    number(sfLiquidationChunkMin, ChangeClass::Delayed);
    number(sfOpenInterestMax, ChangeClass::DelayedUp);
    if (auto const rewardMin = tx[~sfLiquidationRewardMin])
    {
        classifyChange(
            *market,
            pending,
            queued,
            sfLiquidationRewardMin,
            std::optional<Number>{
                perp::roundCollateral(*exchange, *rewardMin, Number::RoundingMode::Downward)},
            ChangeClass::Delayed);
    }

    if (tx.isFieldPresent(sfIndexOracles) &&
        !(tx.getFieldArray(sfIndexOracles) == market->getFieldArray(sfIndexOracles)))
    {
        pending.setFieldArray(sfIndexOracles, tx.getFieldArray(sfIndexOracles));
        queued = true;
    }

    // Tiers that allow more notional or require less margin are queued; tighter ones apply now.
    if (tx.isFieldPresent(sfMarginTiers) &&
        !(tx.getFieldArray(sfMarginTiers) == market->getFieldArray(sfMarginTiers)))
    {
        if (perp::tiersLoosen(
                market->getFieldArray(sfMarginTiers), tx.getFieldArray(sfMarginTiers)))
        {
            pending.setFieldArray(sfMarginTiers, tx.getFieldArray(sfMarginTiers));
            queued = true;
        }
        else
        {
            market->setFieldArray(sfMarginTiers, tx.getFieldArray(sfMarginTiers));
        }
    }

    // A new queued change replaces the one pending.
    if (queued)
    {
        market->setFieldObject(sfPendingParameters, pending);
        (*market)[sfParameterEffectiveTime] =
            perp::parameterEffectiveTime(now, (*exchange)[sfParameterDelay]);
    }

    // The book mid must stay inside the order band now and once the queued change applies.
    auto const spreadFits = [](STLedgerEntry const& entry) {
        return entry[sfMidSpreadMax] < entry[sfOrderPriceBand];
    };
    if (!spreadFits(*market) || !spreadFits(perp::eventualParameters(*market)))
        return tecLIMIT_EXCEEDED;

    if (tx.isFlag(tfPerpMarketSettle))
    {
        std::uint8_t const status = (*market)[sfMarketStatus];
        if (status == perp::kMarketSettled)
            return tecPERP_MARKET_SETTLED;
        if (status != perp::kMarketLive)
            return tecPERP_STALE_PRICE;

        // The owner settles a Live market at its current mark (spec 10).
        perp::settleMarket(*market, (*market)[sfMarkPrice], now);
    }

    view().update(market);
    return tesSUCCESS;
}

void
PerpMarketSet::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
{
}

bool
PerpMarketSet::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl

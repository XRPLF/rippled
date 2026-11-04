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
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
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
        // An update names no fixed field and does not set the isolation flag (spec 11).
        std::array<SField const*, 7> const fixed{
            &sfExchangeID,
            &sfBaseAsset,
            &sfQuoteAsset,
            &sfIndexOracles,
            &sfIndexMaxLedgers,
            &sfPriceTick,
            &sfLotSize};
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
        // Creation carries every parameter of the market.
        std::array<SField const*, 10> const required{
            &sfExchangeID,
            &sfBaseAsset,
            &sfQuoteAsset,
            &sfIndexOracles,
            &sfIndexMaxLedgers,
            &sfPriceTick,
            &sfLotSize,
            &sfMarginTiers,
            &sfOpenInterestMax,
            &sfOrderPriceBand};
        for (auto const* field : required)
        {
            if (!tx.isFieldPresent(*field))
                return temMALFORMED;
        }
        if (tx[sfExchangeID] == beast::kZero || tx[sfBaseAsset] == tx[sfQuoteAsset] ||
            tx.isFlag(tfPerpMarketSettle) || tx[sfIndexMaxLedgers] == 0 ||
            !validOracles(tx.getFieldArray(sfIndexOracles)))
            return temMALFORMED;
    }

    if (tx.isFieldPresent(sfMarginTiers) && !validTiers(tx.getFieldArray(sfMarginTiers)))
        return temMALFORMED;

    // Increments and caps are positive.
    for (auto const* field : {&sfPriceTick, &sfLotSize, &sfOpenInterestMax})
    {
        if (auto const value = tx[~*field]; value && value->signum() <= 0)
            return temMALFORMED;
    }

    if (!validNumericRange(tx[~sfOrderPriceBand], perp::kRateUnity))
        return temMALFORMED;

    return tesSUCCESS;
}

TER
PerpMarketSet::preclaim(PreclaimContext const& ctx)
{
    auto const& tx = ctx.tx;
    std::shared_ptr<SLE const> market;
    if (auto const marketID = tx[~sfMarketID])
    {
        market = ctx.view.read(keylet::perpMarket(*marketID));
        if (!market)
            return tecNO_ENTRY;
    }
    auto const exchangeID = market ? (*market)[sfExchangeID] : tx[sfExchangeID];

    auto const exchange = ctx.view.read(keylet::perpExchange(exchangeID));
    if (!exchange)
        return tecNO_ENTRY;
    if ((*exchange)[sfOwner] != tx[sfAccount])
        return tecNO_PERMISSION;

    if (market)
    {
        // A loosening value refuses the whole update (spec 11).
        if (auto const cap = tx[~sfOpenInterestMax]; cap && *cap > (*market)[sfOpenInterestMax])
            return tecNO_PERMISSION;
        if (auto const band = tx[~sfOrderPriceBand]; band && *band > (*market)[sfOrderPriceBand])
            return tecNO_PERMISSION;
        if (tx.isFieldPresent(sfMarginTiers) &&
            !perp::tiersTighten(
                market->getFieldArray(sfMarginTiers), tx.getFieldArray(sfMarginTiers)))
            return tecNO_PERMISSION;
        return tesSUCCESS;
    }

    if (ctx.view.exists(keylet::perpMarket(exchangeID, tx[sfBaseAsset], tx[sfQuoteAsset])))
        return tecDUPLICATE;

    if ((*exchange)[sfMarketCount] >= perp::kMaxPerpMarkets)
        return tecPERP_MARKET_LIMIT;

    return tesSUCCESS;
}

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
    (*market)[sfIndexMaxLedgers] = tx[sfIndexMaxLedgers];
    (*market)[sfOrderPriceBand] = tx[sfOrderPriceBand];
    for (auto const* field : {&sfPriceTick, &sfLotSize, &sfOpenInterestMax})
        (*market)[*field] = tx[*field];
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

    auto const t = perp::refreshTime(view());
    (*market)[sfIndexPrice] = *index;
    (*market)[sfFundingRate] = perp::computeFundingRate(Number{}, *index);
    (*market)[sfStatusTime] = t;
    (*market)[sfLastRefreshTime] = t;
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

    // Funding accrues and the premium is sampled under the old parameters (spec 5.4).
    perp::refreshMarket(view(), market, exchange, j_);

    // Preclaim refused every loosening, so each value given applies at once.
    if (auto const cap = tx[~sfOpenInterestMax])
        (*market)[sfOpenInterestMax] = *cap;
    if (auto const band = tx[~sfOrderPriceBand])
        (*market)[sfOrderPriceBand] = *band;
    if (tx.isFieldPresent(sfMarginTiers))
        market->setFieldArray(sfMarginTiers, tx.getFieldArray(sfMarginTiers));

    if (tx.isFlag(tfPerpMarketSettle))
    {
        std::uint8_t const status = (*market)[sfMarketStatus];
        if (status == perp::kMarketSettled)
            return tecPERP_MARKET_SETTLED;
        if (status != perp::kMarketLive)
            return tecPERP_STALE_PRICE;

        // The owner settles a Live market at its current index (spec 10).
        perp::settleMarket(*market, perp::refreshTime(view()));
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

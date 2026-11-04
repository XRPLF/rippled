#include <xrpl/tx/transactors/perp/PerpOrderCreate.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/PerpHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STTakesAsset.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <algorithm>
#include <cstdint>

namespace xrpl {

bool
PerpOrderCreate::checkExtraFeatures(PreflightContext const& ctx)
{
    return ctx.rules.enabled(featureSingleAssetVault);
}

std::uint32_t
PerpOrderCreate::getFlagsMask(PreflightContext const& ctx)
{
    return tfPerpOrderCreateMask;
}

NotTEC
PerpOrderCreate::preflight(PreflightContext const& ctx)
{
    auto const& tx = ctx.tx;
    if (tx[sfMarketID] == beast::kZero)
        return temMALFORMED;

    Number const price = tx[sfLimitPrice];
    Number const size = tx[sfSize];
    if (price.signum() <= 0 || size.signum() <= 0 || !perp::fitsQualityKey(price))
        return temMALFORMED;

    if (static_cast<int>(tx.isFlag(tfPerpPostOnly)) +
            static_cast<int>(tx.isFlag(tfImmediateOrCancel)) +
            static_cast<int>(tx.isFlag(tfFillOrKill)) >
        1)
        return temINVALID_FLAG;

    if (tx[~sfOfferSequence] == 0u)
        return temBAD_SEQUENCE;

    if (tx[~sfExpiration] == 0u)
        return temBAD_EXPIRATION;

    // A builder and its fee come together.
    if (tx.isFieldPresent(sfBuilder) != tx.isFieldPresent(sfBuilderFee) ||
        (tx.isFieldPresent(sfBuilder) && tx[sfBuilder] == beast::kZero) ||
        !validNumericRange(tx[~sfBuilderFee], perp::kMaxTradingFee))
        return temMALFORMED;

    return tesSUCCESS;
}

TER
PerpOrderCreate::preclaim(PreclaimContext const& ctx)
{
    auto const& tx = ctx.tx;
    auto const account = tx[sfAccount];
    auto const market = ctx.view.read(keylet::perpMarket(tx[sfMarketID]));
    if (!market)
        return tecNO_ENTRY;
    auto const exchange = ctx.view.read(keylet::perpExchange((*market)[sfExchangeID]));
    if (!exchange)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    if (!ctx.view.exists(
            keylet::marginAccount(account, exchange->key(), tx[~sfSubaccount].value_or(0))))
        return tecNO_ENTRY;

    // The builder's fee is capped by the exchange and paid to its subaccount 0.
    if (auto const builder = tx[~sfBuilder])
    {
        if (tx[sfBuilderFee] > (*exchange)[sfBuilderFeeMax])
            return tecNO_PERMISSION;
        if (!ctx.view.exists(keylet::marginAccount(*builder, exchange->key(), 0)))
            return tecNO_ENTRY;
    }

    if (auto const domainID = (*exchange)[~sfDomainID])
    {
        if (auto const ter = perp::checkDomain(ctx.view, *domainID, account); !isTesSuccess(ter))
            return ter;
    }

    if (hasExpired(ctx.view, tx[~sfExpiration]))
        return tecEXPIRED;

    return tesSUCCESS;
}

TER
PerpOrderCreate::doApply()
{
    auto const& tx = ctx_.tx;
    bool const sell = tx.isFlag(tfPerpSell);
    bool const reduceOnly = tx.isFlag(tfPerpReduceOnly);
    Number const price = tx[sfLimitPrice];
    Number const size = tx[sfSize];

    // 1. The named own order is cancelled first.
    if (auto const offerSequence = tx[~sfOfferSequence])
    {
        if (auto const old =
                view().peek(keylet::perpOrder(accountID_, SeqProxy::rawSequence(*offerSequence))))
        {
            if (auto const ter = perp::removeOrder(view(), old, j_); !isTesSuccess(ter))
                return ter;  // LCOV_EXCL_LINE
        }
    }

    auto const market = view().peek(keylet::perpMarket(tx[sfMarketID]));
    auto const root = view().peek(keylet::account(accountID_));
    if (!market || !root)
        return tefINTERNAL;  // LCOV_EXCL_LINE
    auto const exchange = view().peek(keylet::perpExchange((*market)[sfExchangeID]));
    if (!exchange)
        return tefINTERNAL;  // LCOV_EXCL_LINE
    auto const account = view().peek(
        keylet::marginAccount(accountID_, exchange->key(), tx[~sfSubaccount].value_or(0)));
    if (!account)
        return tefINTERNAL;  // LCOV_EXCL_LINE
    std::uint32_t const ownerCount = (*root)[sfOwnerCount];

    // 2. The market is refreshed and must be Live.
    perp::refreshMarket(view(), market, exchange, j_);
    std::uint8_t const status = (*market)[sfMarketStatus];
    if (status == perp::kMarketSettled)
        return tecPERP_MARKET_SETTLED;
    if (status != perp::kMarketLive)
        return tecPERP_STALE_PRICE;

    // 3. The account is settled.
    if (auto const ter = perp::settleAccount(view(), account, exchange, j_); !isTesSuccess(ter))
        return ter;

    // 4. Tick, lot, price band, domain and reduce-only checks.
    if (!perp::isMultiple(price, (*market)[sfPriceTick]) ||
        !perp::isMultiple(size, (*market)[sfLotSize]))
        return tecPRECISION_LOSS;
    {
        NumberRoundModeGuard const mg(Number::RoundingMode::ToNearest);
        Number const index = (*market)[sfIndexPrice];
        Number const band = index * Number{(*market)[sfOrderPriceBand]} / Number{perp::kRateUnity};
        if (sell ? price < index - band : price > index + band)
            return tecPERP_PRICE_BAND;
    }
    if (auto const domainID = (*exchange)[~sfDomainID])
    {
        if (auto const ter = perp::verifyDomain(view(), *domainID, accountID_, j_);
            !isTesSuccess(ter))
            return ter;
    }
    auto position = view().peek(keylet::marginPosition(account->key(), market->key()));
    if (reduceOnly)
    {
        Number const held = position ? Number{(*position)[sfSize]} : Number{};
        if (held == Number{} || (held > Number{}) != sell || size > abs(held))
            return tecNO_PERMISSION;
    }

    // 5. A new position obeys the position limits.
    if (!position)
    {
        auto const opened = perp::openPosition(view(), account, *market, j_);
        if (!opened)
            return opened.error();
        position = *opened;
    }

    // 6. The order is counted, and pending in sfOrderCount so its position outlives matching.
    auto const& open = sell ? sfOpenSell : sfOpenBuy;
    if (!reduceOnly)
        (*position)[open] = (*position)[open] + size;
    (*position)[sfOrderCount] = (*position)[sfOrderCount] + 1;
    view().update(position);
    if (!reduceOnly)
    {
        Number const held = (*position)[sfSize];
        Number const worstCase =
            std::max(abs(held + (*position)[sfOpenBuy]), abs(held - (*position)[sfOpenSell]));
        auto const& tiers = market->getFieldArray(sfMarginTiers);
        if (tiers.empty() || worstCase * (*market)[sfMarkPrice] > tiers.back()[sfNotionalMax])
            return tecPERP_POSITION_LIMIT;
    }
    {
        auto const summary = perp::marginSummary(view(), *account);
        if (!summary)
            return summary.error();
        if (summary->equity < Number{} ||
            (!reduceOnly && summary->equity < summary->requirements.initial))
            return tecPERP_INSUFFICIENT_MARGIN;
    }

    // 7. Matching.
    Number const openInterest = (*market)[sfOpenInterestLong];
    perp::Taker taker{
        .account = account,
        .position = position,
        .owner = accountID_,
        .limitPrice = price,
        .remaining = size,
        .sell = sell,
        .reduceOnly = reduceOnly,
        .postOnly = tx.isFlag(tfPerpPostOnly),
        .builder = tx[~sfBuilder],
        .builderFee = tx[~sfBuilderFee].value_or(0)};
    auto const match = perp::matchOrder(view(), market, exchange, taker, j_);
    if (!match)
        return match.error();

    // 8. The pending count is released, and the remainder rests or is dropped.
    if (match->crossed)
        return tecKILLED;
    if (tx.isFlag(tfFillOrKill) && taker.remaining > Number{})
        return tecKILLED;
    if (match->capped && match->filled == Number{})
        return tecPERP_OI_CAP;
    if (!reduceOnly)
        (*position)[open] = (*position)[open] - taker.remaining;
    (*position)[sfOrderCount] = (*position)[sfOrderCount] - 1;
    view().update(position);

    // A remainder the cap or the fill bound left unfilled would cross the book, so it is dropped.
    if (taker.remaining > Number{} && !tx.isFlag(tfImmediateOrCancel) && !match->capped &&
        !match->exhausted)
    {
        if (auto const ter = perp::insertOrder(
                view(),
                {.account = accountID_,
                 .sequence = tx.getSeqProxy().value(),
                 .marginAccountID = account->key(),
                 .marketID = market->key(),
                 .price = price,
                 .size = taker.remaining,
                 .sell = sell,
                 .reduceOnly = reduceOnly,
                 .expiration = tx[~sfExpiration],
                 .builder = tx[~sfBuilder],
                 .builderFee = tx[~sfBuilderFee]},
                j_);
            !isTesSuccess(ter))
            return ter;
    }

    // 9. The account stays solvent, and meets its initial requirement when its risk rose.
    {
        auto const summary = perp::marginSummary(view(), *account);
        if (!summary)
            return summary.error();
        if (summary->equity < Number{} ||
            (!reduceOnly && summary->equity < summary->requirements.initial))
            return tecPERP_INSUFFICIENT_MARGIN;
    }

    // New open interest needs cover of sfCoverRateMinimum of the open-interest notional.
    if ((*market)[sfOpenInterestLong] > openInterest)
    {
        NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
        Number const required = perp::roundCollateral(
            *exchange,
            Number{(*exchange)[sfCoverRateMinimum]} / Number{perp::kRateUnity} *
                (*exchange)[sfOpenInterestNotional],
            Number::RoundingMode::Upward);
        if ((*exchange)[sfCoverAvailable] < required)
            return tecPERP_INSUFFICIENT_COVER;
    }

    if (auto const ter = perp::deletePositionIfEmpty(view(), position, j_); !isTesSuccess(ter))
        return ter;  // LCOV_EXCL_LINE

    if ((*root)[sfOwnerCount] > ownerCount && preFeeBalance_ < accountReserve(view(), root, j_))
        return tecINSUFFICIENT_RESERVE;

    associateAsset(*account, (*exchange)[sfAsset]);
    associateAsset(*exchange, (*exchange)[sfAsset]);
    view().update(account);
    view().update(exchange);
    return tesSUCCESS;
}

void
PerpOrderCreate::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
{
}

bool
PerpOrderCreate::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    return true;
}

}  // namespace xrpl

#include <test/jtx/perp.h>

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/helpers/PerpHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>

namespace xrpl::test::jtx {

namespace {

json::Value
base(json::StaticString const& type, Account const& account)
{
    json::Value jv;
    jv[jss::TransactionType] = type;
    jv[jss::Account] = account.human();
    return jv;
}

}  // namespace

json::Value
Perp::exchangeSet(Account const& owner, Asset const& asset)
{
    auto jv = base(jss::PerpExchangeSet, owner);
    jv[jss::Asset] = toJson(asset);
    jv[sfParameterDelay] = static_cast<std::uint32_t>(perp::kMinParameterDelay.count());
    return jv;
}

json::Value
Perp::exchangeUpdate(Account const& owner, uint256 const& exchangeID)
{
    auto jv = base(jss::PerpExchangeSet, owner);
    jv[sfExchangeID] = to_string(exchangeID);
    return jv;
}

json::Value
Perp::exchangeDelete(Account const& owner, uint256 const& exchangeID)
{
    auto jv = base(jss::PerpExchangeDelete, owner);
    jv[sfExchangeID] = to_string(exchangeID);
    return jv;
}

json::Value
Perp::marketSet(Account const& owner, uint256 const& exchangeID, MarketArgs const& args)
{
    auto jv = base(jss::PerpMarketSet, owner);
    jv[sfExchangeID] = to_string(exchangeID);
    jv[sfBaseAsset] = args.baseAsset;
    jv[sfQuoteAsset] = args.quoteAsset;

    json::Value oracles{json::ValueType::Array};
    for (auto const& oracle : args.oracles)
    {
        json::Value entry;
        entry[sfIndexOracle][sfOwner] = toBase58(oracle.owner);
        entry[sfIndexOracle][sfOracleDocumentID] = oracle.documentID;
        oracles.append(entry);
    }
    jv[sfIndexOracles] = oracles;

    json::Value tiers{json::ValueType::Array};
    for (auto const& tier : args.tiers)
    {
        json::Value entry;
        entry[sfMarginTier][sfNotionalMax] = to_string(tier.notionalMax);
        entry[sfMarginTier][sfInitialMarginRate] = tier.initialMarginRate;
        entry[sfMarginTier][sfMaintenanceMarginRate] = tier.maintenanceMarginRate;
        tiers.append(entry);
    }
    jv[sfMarginTiers] = tiers;

    jv[sfIndexMaxAge] = args.indexMaxAge;
    jv[sfIndexMaxLedgers] = args.indexMaxLedgers;
    jv[sfPriceTick] = to_string(args.priceTick);
    jv[sfLotSize] = to_string(args.lotSize);
    jv[sfOpenInterestMax] = to_string(args.openInterestMax);
    jv[sfOrderPriceBand] = args.orderPriceBand;
    jv[sfMidSpreadMax] = args.midSpreadMax;
    jv[sfMarkBand] = args.markBand;
    jv[sfMarkMoveMax] = args.markMoveMax;
    jv[sfBasisWindow] = args.basisWindow;
    jv[sfFundingCap] = args.fundingCap;
    jv[sfLiquidationPenalty] = args.liquidationPenalty;
    jv[sfLiquidationRewardShare] = args.liquidationRewardShare;
    jv[sfLiquidationChunkMin] = to_string(args.liquidationChunkMin);
    jv[sfSettleStaleAfter] = args.settleStaleAfter;
    return jv;
}

json::Value
Perp::marketUpdate(Account const& owner, uint256 const& marketID)
{
    auto jv = base(jss::PerpMarketSet, owner);
    jv[sfMarketID] = to_string(marketID);
    return jv;
}

json::Value
Perp::marketDelete(Account const& owner, uint256 const& marketID)
{
    auto jv = base(jss::PerpMarketDelete, owner);
    jv[sfMarketID] = to_string(marketID);
    return jv;
}

json::Value
Perp::deposit(Account const& account, uint256 const& exchangeID, STAmount const& amount)
{
    auto jv = base(jss::MarginDeposit, account);
    jv[sfExchangeID] = to_string(exchangeID);
    jv[jss::Amount] = toJson(amount);
    return jv;
}

json::Value
Perp::withdraw(Account const& account, uint256 const& exchangeID, STAmount const& amount)
{
    auto jv = base(jss::MarginWithdraw, account);
    jv[sfExchangeID] = to_string(exchangeID);
    jv[jss::Amount] = toJson(amount);
    return jv;
}

json::Value
Perp::order(
    Account const& account,
    uint256 const& marketID,
    Number const& price,
    Number const& size)
{
    auto jv = base(jss::PerpOrderCreate, account);
    jv[sfMarketID] = to_string(marketID);
    jv[sfLimitPrice] = to_string(price);
    jv[sfSize] = to_string(size);
    return jv;
}

json::Value
Perp::cancel(Account const& account, std::uint32_t offerSequence)
{
    auto jv = base(jss::PerpOrderCancel, account);
    jv[sfOfferSequence] = offerSequence;
    return jv;
}

json::Value
Perp::liquidate(
    Account const& liquidator,
    uint256 const& marginAccountID,
    uint256 const& marketID,
    std::uint32_t flags)
{
    auto jv = base(jss::PerpLiquidate, liquidator);
    jv[sfMarginAccountID] = to_string(marginAccountID);
    jv[sfMarketID] = to_string(marketID);
    jv[jss::Flags] = flags;
    return jv;
}

json::Value
Perp::refresh(Account const& account, uint256 const& marketID)
{
    auto jv = base(jss::PerpRefresh, account);
    jv[sfMarketID] = to_string(marketID);
    return jv;
}

Keylet
Perp::exchangeKeylet(Env& env, Account const& owner)
{
    return keylet::perpExchange(owner.id(), SeqProxy::rawSequence(env.seq(owner)));
}

Keylet
Perp::orderKeylet(Env& env, Account const& account)
{
    return keylet::perpOrder(account.id(), SeqProxy::rawSequence(env.seq(account)));
}

}  // namespace xrpl::test::jtx

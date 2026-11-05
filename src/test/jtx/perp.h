#pragma once

#include <test/jtx/Account.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/STAmount.h>

#include <cstdint>
#include <string>
#include <vector>

namespace xrpl::test::jtx {

class Env;

/**
 * Builders for the PerpetualFutures transactions. Each returns the minimal
 * well-formed transaction; a test adds optional fields to the returned JSON.
 */
struct Perp
{
    /**
     * One index source of a market: an XLS-47 oracle.
     */
    struct OracleRef
    {
        AccountID owner;
        std::uint32_t documentID{};
    };

    /**
     * One margin tier of a market; rates are in tenth-bips.
     */
    struct Tier
    {
        Number notionalMax;
        std::uint32_t initialMarginRate;
        std::uint32_t maintenanceMarginRate;
    };

    /**
     * The parameters of a market creation. The defaults pass preflight; the
     * oracles have none and must be named.
     */
    struct MarketArgs
    {
        std::vector<OracleRef> oracles;
        std::string baseAsset{"XRP"};
        std::string quoteAsset{"USD"};
        std::uint32_t indexMaxLedgers{10};
        Number priceTick{1, -4};
        Number lotSize{1};
        std::vector<Tier> tiers{
            {.notionalMax = Number{100'000},
             .initialMarginRate = 10'000,
             .maintenanceMarginRate = 5'000},
            {.notionalMax = Number{1'000'000},
             .initialMarginRate = 20'000,
             .maintenanceMarginRate = 10'000}};
        Number openInterestMax{1'000'000};
        std::uint32_t orderPriceBand{10'000};
    };

    /**
     * PerpExchangeSet creating an exchange in asset, with a liquidation penalty of 1 % and a
     * liquidation chunk minimum of 10 000.
     */
    static json::Value
    exchangeSet(Account const& owner, Asset const& asset);

    /**
     * PerpExchangeSet updating exchangeID; the test adds the fields to change.
     */
    static json::Value
    exchangeUpdate(Account const& owner, uint256 const& exchangeID);

    static json::Value
    exchangeDelete(Account const& owner, uint256 const& exchangeID);

    /**
     * PerpMarketSet creating a market on exchangeID.
     */
    static json::Value
    marketSet(Account const& owner, uint256 const& exchangeID, MarketArgs const& args);

    /**
     * PerpMarketSet updating marketID; the test adds the fields to change.
     */
    static json::Value
    marketUpdate(Account const& owner, uint256 const& marketID);

    static json::Value
    marketDelete(Account const& owner, uint256 const& marketID);

    /**
     * MarginDeposit into account's margin account on exchangeID.
     */
    static json::Value
    deposit(Account const& account, uint256 const& exchangeID, STAmount const& amount);

    /**
     * MarginWithdraw from account's margin account on exchangeID.
     */
    static json::Value
    withdraw(Account const& account, uint256 const& exchangeID, STAmount const& amount);

    /**
     * PerpOrderCreate buying size at price; tfPerpSell makes it a sell.
     */
    static json::Value
    order(Account const& account, uint256 const& marketID, Number const& price, Number const& size);

    static json::Value
    cancel(Account const& account, std::uint32_t offerSequence);

    /**
     * PerpLiquidate of marginAccountID's position in marketID, in the mode flags selects.
     */
    static json::Value
    liquidate(
        Account const& liquidator,
        uint256 const& marginAccountID,
        uint256 const& marketID,
        std::uint32_t flags);

    static json::Value
    refresh(Account const& account, uint256 const& marketID);

    /**
     * PerpRefresh settling marginAccountID, with no market named.
     */
    static json::Value
    refreshAccount(Account const& account, uint256 const& marginAccountID);

    /**
     * The exchange owner's next PerpExchangeSet creates, at its next sequence.
     */
    static Keylet
    exchangeKeylet(Env& env, Account const& owner);

    /**
     * The order account's next PerpOrderCreate rests, at its next sequence.
     */
    static Keylet
    orderKeylet(Env& env, Account const& account);
};

}  // namespace xrpl::test::jtx

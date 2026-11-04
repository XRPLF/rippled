#include <test/app/perp/PerpTestBase.h>
#include <test/jtx/Env.h>
#include <test/jtx/Oracle.h>
#include <test/jtx/amount.h>
#include <test/jtx/perp.h>
#include <test/jtx/ter.h>
#include <test/jtx/txflags.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/helpers/PerpHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STVector256.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace xrpl {

namespace {

/**
 * How a settlement scenario's exchange and markets are created.
 */
struct SettleOptions
{
    /**
     * Adds an ETH/USD market beside XRP/USD.
     */
    bool eth{};
    /**
     * sfFundingInterestRate of every market, in tenth-bips.
     */
    std::int32_t interest{};
    /**
     * Binds the owner's vault with this sfVaultLossLimit.
     */
    std::optional<std::uint32_t> vaultLossLimit;
};

}  // namespace

class PerpSettle_test : public PerpTestBase
{
    using Perp = test::jtx::Perp;

    struct Settling
    {
        World world;
        Keylet exchange;
        Keylet xrp;
        Keylet eth;
    };

    /**
     * Oracle prices of XRP/USD and, when set, ETH/USD, in whole USD.
     */
    [[nodiscard]] static test::jtx::oracle::DataSeries
    prices(std::uint32_t xrp, std::optional<std::uint32_t> eth)
    {
        test::jtx::oracle::DataSeries series{{"XRP", "USD", xrp * 10, 1}};
        if (eth)
            series.emplace_back("ETH", "USD", *eth * 10, 1);
        return series;
    }

    /**
     * A market on wide oracle bounds, with one tier of 4 % initial and 2 % maintenance margin.
     */
    [[nodiscard]] json::Value
    marketTx(Keylet const& exchange, std::string const& base, SettleOptions const& options) const
    {
        auto args = marketArgs();
        args.baseAsset = base;
        args.indexMaxLedgers = 1'000;
        args.indexMaxAge = 100'000;
        args.tiers = {
            {.notionalMax = Number{1'000'000},
             .initialMarginRate = 4'000,
             .maintenanceMarginRate = 2'000}};
        auto jv = Perp::marketSet(owner_, exchange.key, args);
        if (options.interest != 0)
            jv[sfFundingInterestRate] = options.interest;
        return jv;
    }

    /**
     * Oracles at XRP 100 (and ETH 100), a USD exchange and its markets. Closes the ledger.
     */
    Settling
    setup(Env& env, SettleOptions const& options = {})
    {
        auto const world =
            setupMarketWorld(env, prices(100, options.eth ? std::optional{100u} : std::nullopt));

        auto const exchange = Perp::exchangeKeylet(env, owner_);
        auto create = Perp::exchangeSet(owner_, usd_);
        if (options.vaultLossLimit)
        {
            create[sfVaultID] = to_string(world.vault.key);
            create[sfVaultLossWindow] = 86'400;
            create[sfVaultLossLimit] = *options.vaultLossLimit;
        }
        env(create);
        env.close();

        env(marketTx(exchange, "XRP", options));
        if (options.eth)
            env(marketTx(exchange, "ETH", options));
        env.close();
        return {
            .world = world,
            .exchange = exchange,
            .xrp = marketKeylet(exchange.key, "XRP"),
            .eth = marketKeylet(exchange.key, "ETH")};
    }

    /**
     * Moves every oracle to the given prices; an absent ETH price leaves that pair unpriced.
     */
    static void
    movePrices(Env& env, Settling& t, std::uint32_t xrp, std::optional<std::uint32_t> eth)
    {
        env.close();
        for (auto& oracle : t.world.oracles)
            oracle.set(test::jtx::oracle::UpdateArg{.series = prices(xrp, eth)});
        env.close();
    }

    void
    deposit(Env& env, Settling const& t, Account const& account, std::uint32_t amount)
    {
        env(Perp::deposit(account, t.exchange.key, usd_(amount)));
    }

    void
    cover(Env& env, Settling const& t, std::uint32_t amount)
    {
        using namespace test::jtx;
        env(Perp::deposit(owner_, t.exchange.key, usd_(amount)), Txflags(tfPerpCover));
    }

    [[nodiscard]] static json::Value
    order(
        Account const& account,
        Keylet const& market,
        Number const& price,
        Number const& size,
        std::uint32_t flags = 0)
    {
        auto jv = Perp::order(account, market.key, price, size);
        if (flags != 0)
            jv[jss::Flags] = flags;
        return jv;
    }

    /**
     * Opens a position of size between a buyer and a seller at price: the buyer rests first and
     * the seller fills it in the next ledger.
     */
    static void
    trade(
        Env& env,
        Keylet const& market,
        Account const& buyer,
        Account const& seller,
        Number const& size,
        Number const& price)
    {
        env(order(buyer, market, price, size));
        env.close();
        env(order(seller, market, price, size, tfPerpSell));
        env.close();
    }

    /**
     * The exchange owner settles a Live market at its mark.
     */
    void
    settleByOwner(Env& env, Keylet const& market)
    {
        auto jv = Perp::marketUpdate(owner_, market.key);
        jv[jss::Flags] = tfPerpMarketSettle;
        env(jv);
        env.close();
    }

    [[nodiscard]] static json::Value
    settleStale(Account const& account, Keylet const& market)
    {
        auto jv = Perp::refresh(account, market.key);
        jv[jss::Flags] = tfPerpSettleStale;
        return jv;
    }

    [[nodiscard]] static Keylet
    marginKeylet(Account const& account, Settling const& t)
    {
        return keylet::marginAccount(account.id(), t.exchange.key, 0);
    }

    [[nodiscard]] static Keylet
    positionKeylet(Account const& account, Settling const& t, Keylet const& market)
    {
        return keylet::marginPosition(marginKeylet(account, t).key, market.key);
    }

    /**
     * PerpRefresh settling account's margin account, submitted by submitter.
     */
    [[nodiscard]] static json::Value
    settle(Account const& submitter, Account const& account, Settling const& t)
    {
        return Perp::refreshAccount(submitter, marginKeylet(account, t).key);
    }

    [[nodiscard]] static Number
    sizeOf(Env& env, Account const& account, Settling const& t, Keylet const& market)
    {
        auto const sle = env.le(positionKeylet(account, t, market));
        return sle ? Number((*sle)[sfSize]) : Number();
    }

    [[nodiscard]] static Number
    cashOf(Env& env, Account const& account, Settling const& t)
    {
        auto const sle = env.le(marginKeylet(account, t));
        return sle ? Number((*sle)[sfCash]) : Number(-1'000'000);
    }

    [[nodiscard]] static Number
    creditOf(Env& env, Account const& account, Settling const& t)
    {
        auto const sle = env.le(marginKeylet(account, t));
        return sle ? Number((*sle)[sfWaterfallCredit]) : Number(-1);
    }

    [[nodiscard]] static bool
    hasNoMarkets(Env& env, Account const& account, Settling const& t)
    {
        auto const sle = env.le(marginKeylet(account, t));
        return sle && sle->getFieldV256(sfMarkets).empty();
    }

    [[nodiscard]] static Number
    exchangeField(Env& env, Settling const& t, SF_NUMBER const& field)
    {
        auto const sle = env.le(t.exchange);
        return sle ? Number((*sle)[field]) : Number(-1'000'000);
    }

    [[nodiscard]] static Number
    marketField(Env& env, Keylet const& market, SF_NUMBER const& field)
    {
        auto const sle = env.le(market);
        return sle ? Number((*sle)[field]) : Number(-1'000'000);
    }

    [[nodiscard]] static std::uint8_t
    statusOf(Env& env, Keylet const& market)
    {
        auto const sle = env.le(market);
        return sle ? std::uint8_t{(*sle)[sfMarketStatus]} : std::uint8_t{0xFF};
    }

    /**
     * The Unix time a transaction submitted now refreshes at.
     */
    [[nodiscard]] static std::uint32_t
    now(Env& env)
    {
        return perp::refreshTime(*env.current());
    }

    /**
     * Checks invariants 1 and 2 of the spec on the exchange.
     */
    void
    expectAccounting(Env& env, Settling const& t)
    {
        auto const sle = env.le(t.exchange);
        if (!BEAST_EXPECT(sle))
            return;
        Number const assets = (*sle)[sfAssetsTotal];
        Number const held = accountHolds(
            *env.current(),
            (*sle)[sfAccount],
            Asset{(*sle)[sfAsset]},
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            env.journal);
        BEAST_EXPECT(held == assets);
        BEAST_EXPECT(
            assets ==
            (*sle)[sfCashTotal] + (*sle)[sfInsuranceFund] + (*sle)[sfCoverAvailable] +
                (*sle)[sfOwnerFees] + (*sle)[sfSettlementBalance] - (*sle)[sfBadDebt]);
    }

    void
    testRefreshFundingFlows()
    {
        testcase("refresh funding flows");
        using namespace test::jtx;
        Env env{*this, all_};
        auto t = setup(env, {.interest = 50});
        deposit(env, t, alice_, 2'000);
        deposit(env, t, bob_, 2'000);
        env.close();
        trade(env, t.xrp, bob_, alice_, Number(100), Number(100));

        // Interest 0.05 % with mark at index: F = 0 + clamp(0.0005, +-0.0005) = 0.0005 per 8 h.
        BEAST_EXPECT(marketField(env, t.xrp, sfFundingRate) == Number(5, -4));
        Number const before = marketField(env, t.xrp, sfCumulativeFunding);
        auto const bobPosition = env.le(positionKeylet(bob_, t, t.xrp));
        BEAST_EXPECT(bobPosition && (*bobPosition)[sfFundingIndex] == before);

        // More than 8 h later a keeper refreshes: 0.0005 x 100 x 28800 / 28800 = 0.05 per unit.
        // Settling each account in the same ledger moves 100 x 0.05 = 5 from the long to the
        // short.
        env.close(std::chrono::seconds{30'000});
        Number const balance = exchangeField(env, t, sfSettlementBalance);
        env(Perp::refresh(carol_, t.xrp.key));
        env(settle(carol_, bob_, t));
        env(settle(carol_, alice_, t));
        env.close();
        Number const after = marketField(env, t.xrp, sfCumulativeFunding);
        BEAST_EXPECT(after == before + Number(5, -2));
        BEAST_EXPECT(cashOf(env, bob_, t) == Number(1'995));
        BEAST_EXPECT(cashOf(env, alice_, t) == Number(2'005));
        BEAST_EXPECT(exchangeField(env, t, sfSettlementBalance) == balance);
        auto const alicePosition = env.le(positionKeylet(alice_, t, t.xrp));
        BEAST_EXPECT(alicePosition && (*alicePosition)[sfFundingIndex] == after);
        expectAccounting(env, t);

        // A refresh names an existing market or margin account.
        env(Perp::refresh(carol_, uint256{1}), Ter(tecNO_ENTRY));
        env(Perp::refreshAccount(carol_, uint256{1}), Ter(tecNO_ENTRY));
        env(Perp::refreshAccount(carol_, t.xrp.key), Ter(tecNO_ENTRY));
    }

    void
    testSnapshotsAtZeroCrossing()
    {
        testcase("snapshots at zero crossing");
        using namespace test::jtx;
        Env env{*this, all_};
        auto t = setup(env);
        deposit(env, t, alice_, 750);
        deposit(env, t, bob_, 2'000);
        deposit(env, t, carol_, 2'000);
        deposit(env, t, liq_, 1'000);
        env.close();
        trade(env, t.xrp, bob_, alice_, Number(100), Number(100));
        movePrices(env, t, 110, std::nullopt);
        env(order(carol_, t.xrp, Number(108), Number(100), tfPerpSell));
        env.close();

        // alice's book liquidation at 108 leaves cash -50, paid by the longs: 50 / 100 = 0.5.
        env(Perp::liquidate(liq_, marginKeylet(alice_, t).key, t.xrp.key, tfPerpLiquidateToBook));
        env.close();
        BEAST_EXPECT(marketField(env, t.xrp, sfLossIndexLong) == Number(5, -1));
        BEAST_EXPECT(marketField(env, t.xrp, sfLossIndexShort) == Number(0));

        // The review's finding 3: liq's position is created by a resting ask and first filled
        // short, so it takes the short index 0 and is never credited 10 x 0.5 by the long one.
        env(order(liq_, t.xrp, Number(110), Number(10), tfPerpSell));
        env.close();
        {
            auto const position = env.le(positionKeylet(liq_, t, t.xrp));
            BEAST_EXPECT(position && (*position)[sfLossIndex] == Number(0));
        }
        env(order(bob_, t.xrp, Number(110), Number(10)));
        env.close();
        BEAST_EXPECT(sizeOf(env, liq_, t, t.xrp) == Number(-10));
        env(settle(carol_, liq_, t));
        env.close();
        BEAST_EXPECT(cashOf(env, liq_, t) == Number(1'000));
        {
            auto const position = env.le(positionKeylet(liq_, t, t.xrp));
            BEAST_EXPECT(position && (*position)[sfLossIndex] == Number(0));
        }

        // bob paid 100 x 0.5 = 50 as a long. His sale of 220 flips him short 110 and carol's
        // purchase flips her long 120: each takes the new side's index and is not charged again.
        BEAST_EXPECT(cashOf(env, bob_, t) == Number(1'950));
        env(order(carol_, t.xrp, Number(110), Number(220)));
        env.close();
        env(order(bob_, t.xrp, Number(110), Number(220), tfPerpSell));
        env.close();
        BEAST_EXPECT(sizeOf(env, bob_, t, t.xrp) == Number(-110));
        BEAST_EXPECT(sizeOf(env, carol_, t, t.xrp) == Number(120));
        Number const bobCash = cashOf(env, bob_, t);
        Number const carolCash = cashOf(env, carol_, t);
        BEAST_EXPECT(bobCash == Number(2'950));
        BEAST_EXPECT(carolCash == Number(1'800));
        env(settle(liq_, bob_, t));
        env(settle(liq_, carol_, t));
        env.close();
        BEAST_EXPECT(cashOf(env, bob_, t) == bobCash);
        BEAST_EXPECT(cashOf(env, carol_, t) == carolCash);
        auto const bobPosition = env.le(positionKeylet(bob_, t, t.xrp));
        BEAST_EXPECT(bobPosition && (*bobPosition)[sfLossIndex] == Number(0));
        auto const carolPosition = env.le(positionKeylet(carol_, t, t.xrp));
        BEAST_EXPECT(carolPosition && (*carolPosition)[sfLossIndex] == Number(5, -1));
        expectAccounting(env, t);
    }

    void
    testStaleRefusesOrders()
    {
        testcase("stale refuses orders");
        using namespace test::jtx;
        Env env{*this, all_};
        auto t = setup(env, {.interest = 50});
        deposit(env, t, alice_, 2'000);
        deposit(env, t, bob_, 2'000);
        deposit(env, t, carol_, 1'000);
        env.close();
        trade(env, t.xrp, bob_, alice_, Number(10), Number(100));
        auto const carolSeq = env.seq(carol_);
        auto const carolOrder = Perp::orderKeylet(env, carol_);
        env(order(carol_, t.xrp, Number(95), Number(1)));
        env.close();

        // Every quote is older than sfIndexMaxAge, so the market has no quorum.
        env.close(std::chrono::seconds{200'000});
        env(order(alice_, t.xrp, Number(100), Number(1), tfPerpSell), Ter(tecPERP_STALE_PRICE));
        auto const staleAt = now(env);
        env(Perp::refresh(carol_, t.xrp.key));
        env.close();
        BEAST_EXPECT(statusOf(env, t.xrp) == perp::kMarketStale);
        {
            auto const market = env.le(t.xrp);
            BEAST_EXPECT(market && (*market)[~sfStaleSince] == staleAt);
            BEAST_EXPECT(market && (*market)[sfFundingRate] == Number(0));
        }
        Number const cumulative = marketField(env, t.xrp, sfCumulativeFunding);
        auto const alicePosition = env.le(positionKeylet(alice_, t, t.xrp));

        // Orders and liquidations of the market are refused; cancels and deposits are not.
        env(order(alice_, t.xrp, Number(100), Number(1), tfPerpSell), Ter(tecPERP_STALE_PRICE));
        env(Perp::liquidate(bob_, marginKeylet(alice_, t).key, t.xrp.key, tfPerpLiquidateToBook),
            Ter(tecPERP_STALE_PRICE));
        env(Perp::withdraw(alice_, t.exchange.key, usd_(1)), Ter(tecPERP_STALE_PRICE));
        env(Perp::cancel(carol_, carolSeq));
        deposit(env, t, alice_, 10);
        env.close();
        BEAST_EXPECT(!env.le(carolOrder));

        // The refresh that found the market Stale first accrued up to 8 h while it was Live, which
        // alice's short 10 receives with the deposit.
        {
            auto const exchange = env.le(t.exchange);
            if (!BEAST_EXPECT(exchange && alicePosition))
                return;
            Number const received = perp::roundCollateral(
                *exchange,
                Number(10) * (cumulative - (*alicePosition)[sfFundingIndex]),
                Number::RoundingMode::Downward);
            BEAST_EXPECT(received > Number(5, -1));
            BEAST_EXPECT(cashOf(env, alice_, t) == Number(2'010) + received);
        }

        // Funding does not accrue while Stale, and the stale time is kept.
        env.close(std::chrono::seconds{3'600});
        env(Perp::refresh(carol_, t.xrp.key));
        env.close();
        BEAST_EXPECT(marketField(env, t.xrp, sfCumulativeFunding) == cumulative);
        {
            auto const market = env.le(t.xrp);
            BEAST_EXPECT(market && (*market)[~sfStaleSince] == staleAt);
        }

        // Fresh quotes make the market Live again.
        movePrices(env, t, 100, std::nullopt);
        env(Perp::refresh(carol_, t.xrp.key));
        env.close();
        BEAST_EXPECT(statusOf(env, t.xrp) == perp::kMarketLive);
        {
            auto const market = env.le(t.xrp);
            BEAST_EXPECT(market && !market->isFieldPresent(sfStaleSince));
        }
        env(order(alice_, t.xrp, Number(100), Number(1), tfPerpSell));
        env.close();
    }

    void
    testStaleWithdrawOtherMarkets()
    {
        testcase("stale withdraw other markets");
        using namespace test::jtx;
        Env env{*this, all_};
        auto t = setup(env, {.eth = true});
        deposit(env, t, alice_, 2'000);
        deposit(env, t, bob_, 2'000);
        deposit(env, t, carol_, 2'000);
        deposit(env, t, liq_, 2'000);
        env.close();
        trade(env, t.xrp, bob_, alice_, Number(10), Number(100));
        trade(env, t.eth, carol_, liq_, Number(10), Number(100));

        // The oracles drop the ETH pair, so ETH turns Stale and XRP stays Live.
        movePrices(env, t, 100, std::nullopt);
        env(Perp::refresh(alice_, t.eth.key));
        env.close();
        BEAST_EXPECT(statusOf(env, t.eth) == perp::kMarketStale);
        BEAST_EXPECT(statusOf(env, t.xrp) == perp::kMarketLive);

        // alice holds only XRP, so her withdrawal is computed; carol's needs the ETH mark.
        env(Perp::withdraw(alice_, t.exchange.key, usd_(100)));
        env(Perp::withdraw(carol_, t.exchange.key, usd_(100)), Ter(tecPERP_STALE_PRICE));
        deposit(env, t, carol_, 100);
        env.close();
        BEAST_EXPECT(cashOf(env, alice_, t) == Number(1'900));
        BEAST_EXPECT(cashOf(env, carol_, t) == Number(2'100));
        BEAST_EXPECT(sizeOf(env, carol_, t, t.eth) == Number(10));
        expectAccounting(env, t);
    }

    void
    testSettleStaleByAnyone()
    {
        testcase("settle stale by anyone");
        using namespace test::jtx;
        Env env{*this, all_};
        auto t = setup(env, {.eth = true});
        deposit(env, t, carol_, 2'000);
        deposit(env, t, liq_, 2'000);
        env.close();
        trade(env, t.eth, carol_, liq_, Number(10), Number(100));
        movePrices(env, t, 100, std::nullopt);

        // A Live market is not settled this way; a market only now found Stale is too young.
        env(settleStale(alice_, t.xrp), Ter(tecNO_PERMISSION));
        env(settleStale(alice_, t.eth), Ter(tecTOO_SOON));
        auto noMarket = Perp::refreshAccount(alice_, marginKeylet(carol_, t).key);
        noMarket[jss::Flags] = tfPerpSettleStale;
        env(noMarket, Ter(temMALFORMED));
        env.close();
        BEAST_EXPECT(statusOf(env, t.eth) == perp::kMarketLive);

        env(Perp::refresh(alice_, t.eth.key));
        env.close();
        auto const market = env.le(t.eth);
        if (!BEAST_EXPECT(market && market->isFieldPresent(sfStaleSince)))
            return;
        std::uint32_t const staleSince = (*market)[sfStaleSince];

        env.close(std::chrono::seconds{1'800});
        BEAST_EXPECT(now(env) - staleSince < 3'600);
        env(settleStale(alice_, t.eth), Ter(tecTOO_SOON));
        env.close(std::chrono::seconds{3'600});
        BEAST_EXPECT(now(env) - staleSince >= 3'600);

        // Any account settles it at the last mark once Stale for sfSettleStaleAfter.
        auto const settledAt = now(env);
        env(settleStale(alice_, t.eth));
        env.close();
        {
            auto const settled = env.le(t.eth);
            if (!BEAST_EXPECT(settled))
                return;
            BEAST_EXPECT((*settled)[sfMarketStatus] == perp::kMarketSettled);
            BEAST_EXPECT((*settled)[~sfSettlementPrice] == Number(100));
            BEAST_EXPECT((*settled)[~sfSettledAt] == settledAt);
            BEAST_EXPECT(!settled->isFieldPresent(sfStaleSince));
            BEAST_EXPECT((*settled)[sfFundingRate] == Number(0));
        }
        env(settleStale(alice_, t.eth), Ter(tecPERP_MARKET_SETTLED));

        // At the entry price neither side gains, so both close at once.
        env(settle(alice_, carol_, t));
        env(settle(alice_, liq_, t));
        env.close();
        BEAST_EXPECT(!env.le(positionKeylet(carol_, t, t.eth)));
        BEAST_EXPECT(!env.le(positionKeylet(liq_, t, t.eth)));
        BEAST_EXPECT(cashOf(env, carol_, t) == Number(2'000));
        BEAST_EXPECT(cashOf(env, liq_, t) == Number(2'000));
        BEAST_EXPECT(marketField(env, t.eth, sfOpenInterestLong) == Number(0));
        BEAST_EXPECT(marketField(env, t.eth, sfOpenInterestShort) == Number(0));
        expectAccounting(env, t);
    }

    void
    testSettlementCloseAtPrice()
    {
        testcase("settlement close at price");
        using namespace test::jtx;
        Env env{*this, all_};
        auto t = setup(env);
        deposit(env, t, alice_, 2'000);
        deposit(env, t, bob_, 2'000);
        deposit(env, t, carol_, 1'000);
        env.close();
        trade(env, t.xrp, bob_, alice_, Number(100), Number(100));
        auto const carolSeq = env.seq(carol_);
        env(order(carol_, t.xrp, Number(95), Number(5)));
        movePrices(env, t, 90, std::nullopt);
        settleByOwner(env, t.xrp);
        BEAST_EXPECT(statusOf(env, t.xrp) == perp::kMarketSettled);
        {
            auto const market = env.le(t.xrp);
            BEAST_EXPECT(market && (*market)[~sfSettlementPrice] == Number(90));
        }

        // bob's long closes at 90 when anyone settles him: 100 x (90 - 100) = -1000.
        env(settle(carol_, bob_, t));
        env.close();
        BEAST_EXPECT(!env.le(positionKeylet(bob_, t, t.xrp)));
        BEAST_EXPECT(hasNoMarkets(env, bob_, t));
        BEAST_EXPECT(cashOf(env, bob_, t) == Number(1'000));
        BEAST_EXPECT(marketField(env, t.xrp, sfOpenInterestLong) == Number(0));
        BEAST_EXPECT(marketField(env, t.xrp, sfOpenInterestShort) == Number(100));
        BEAST_EXPECT(sizeOf(env, alice_, t, t.xrp) == Number(-100));

        // No order is placed, and a later quote does not move the settled market's mark.
        env(order(alice_, t.xrp, Number(90), Number(1)), Ter(tecPERP_MARKET_SETTLED));
        movePrices(env, t, 80, std::nullopt);
        env(Perp::refresh(bob_, t.xrp.key));
        env.close();
        BEAST_EXPECT(marketField(env, t.xrp, sfMarkPrice) == Number(90));
        BEAST_EXPECT(statusOf(env, t.xrp) == perp::kMarketSettled);

        // Any account removes an order in a Settled market, one per cancel.
        auto byBob = Perp::cancel(bob_, carolSeq);
        byBob[sfOwner] = carol_.human();
        env(byBob);
        env.close();
        BEAST_EXPECT(!env.le(positionKeylet(carol_, t, t.xrp)));
        BEAST_EXPECT(hasNoMarkets(env, carol_, t));
        expectAccounting(env, t);
    }

    void
    testLossesCloseFirstGrace()
    {
        testcase("losses close first grace");
        using namespace test::jtx;
        Env env{*this, all_};
        auto t = setup(env);
        deposit(env, t, alice_, 2'000);
        deposit(env, t, bob_, 2'000);
        env.close();
        trade(env, t.xrp, bob_, alice_, Number(100), Number(100));
        movePrices(env, t, 90, std::nullopt);
        settleByOwner(env, t.xrp);
        auto const market = env.le(t.xrp);
        if (!BEAST_EXPECT(market && market->isFieldPresent(sfSettledAt)))
            return;
        std::uint32_t const settledAt = (*market)[sfSettledAt];
        auto const grace = static_cast<std::uint32_t>(perp::kSettlementGrace.count());

        // alice's short would realize +1000, so it stays open at its settled value.
        env(settle(alice_, alice_, t));
        env.close();
        BEAST_EXPECT(sizeOf(env, alice_, t, t.xrp) == Number(-100));
        BEAST_EXPECT(cashOf(env, alice_, t) == Number(2'000));

        // The settled value is known, so alice withdraws without a Live mark.
        env(Perp::withdraw(alice_, t.exchange.key, usd_(100)));
        env.close();
        BEAST_EXPECT(cashOf(env, alice_, t) == Number(1'900));

        // bob's losing long closes at once.
        env(settle(alice_, bob_, t));
        env.close();
        BEAST_EXPECT(cashOf(env, bob_, t) == Number(1'000));

        env.close(std::chrono::seconds{grace - 600});
        BEAST_EXPECT(now(env) < settledAt + grace);
        env(settle(bob_, alice_, t));
        env.close();
        BEAST_EXPECT(sizeOf(env, alice_, t, t.xrp) == Number(-100));

        // After kSettlementGrace any transaction closes it, here a deposit.
        env.close(std::chrono::seconds{1'200});
        BEAST_EXPECT(now(env) >= settledAt + grace);
        deposit(env, t, alice_, 1);
        env.close();
        BEAST_EXPECT(!env.le(positionKeylet(alice_, t, t.xrp)));
        BEAST_EXPECT(hasNoMarkets(env, alice_, t));
        BEAST_EXPECT(cashOf(env, alice_, t) == Number(2'901));
        BEAST_EXPECT(marketField(env, t.xrp, sfOpenInterestShort) == Number(0));
        expectAccounting(env, t);
    }

    void
    testSettlementShortfallWaterfall()
    {
        testcase("settlement shortfall waterfall");
        using namespace test::jtx;
        auto const grace = std::chrono::seconds{perp::kSettlementGrace.count() + 60};

        // alice short 100 at 100 with 500 settles at 110: cash -500. Cover pays 200, and the
        // open long side's loss index takes 300 / 100 = 3, which bob pays when he closes.
        {
            Env env{*this, all_};
            auto t = setup(env);
            cover(env, t, 200);
            deposit(env, t, alice_, 500);
            deposit(env, t, bob_, 2'000);
            env.close();
            trade(env, t.xrp, bob_, alice_, Number(100), Number(100));
            movePrices(env, t, 110, std::nullopt);
            settleByOwner(env, t.xrp);

            env(settle(carol_, alice_, t));
            env.close();
            BEAST_EXPECT(!env.le(positionKeylet(alice_, t, t.xrp)));
            BEAST_EXPECT(hasNoMarkets(env, alice_, t));
            BEAST_EXPECT(cashOf(env, alice_, t) == Number(0));
            BEAST_EXPECT(creditOf(env, alice_, t) == Number(500));
            BEAST_EXPECT(exchangeField(env, t, sfCoverAvailable) == Number(0));
            BEAST_EXPECT(marketField(env, t.xrp, sfLossIndexLong) == Number(3));
            BEAST_EXPECT(exchangeField(env, t, sfBadDebt) == Number(0));
            expectAccounting(env, t);

            env.close(grace);
            env(settle(carol_, bob_, t));
            env.close();
            BEAST_EXPECT(!env.le(positionKeylet(bob_, t, t.xrp)));
            BEAST_EXPECT(cashOf(env, bob_, t) == Number(2'700));
            BEAST_EXPECT(marketField(env, t.xrp, sfOpenInterestLong) == Number(0));
            expectAccounting(env, t);
        }

        // The winner closes first after the grace, so no long is left: 300 becomes bad debt.
        {
            Env env{*this, all_};
            auto t = setup(env);
            cover(env, t, 200);
            deposit(env, t, alice_, 500);
            deposit(env, t, bob_, 2'000);
            env.close();
            trade(env, t.xrp, bob_, alice_, Number(100), Number(100));
            movePrices(env, t, 110, std::nullopt);
            settleByOwner(env, t.xrp);
            env.close(grace);

            env(settle(carol_, bob_, t));
            env.close();
            BEAST_EXPECT(cashOf(env, bob_, t) == Number(3'000));
            env(settle(carol_, alice_, t));
            env.close();
            BEAST_EXPECT(cashOf(env, alice_, t) == Number(0));
            BEAST_EXPECT(creditOf(env, alice_, t) == Number(200));
            BEAST_EXPECT(exchangeField(env, t, sfCoverAvailable) == Number(0));
            BEAST_EXPECT(exchangeField(env, t, sfBadDebt) == Number(300));
            BEAST_EXPECT(marketField(env, t.xrp, sfLossIndexLong) == Number(0));
            expectAccounting(env, t);
        }

        // A deposit settling alice draws the bound vault for the whole 500.
        {
            Env env{*this, all_};
            auto t = setup(env, {.vaultLossLimit = 100'000});
            Vault const vault{.env = env};
            env(vault.deposit(
                {.depositor = owner_, .id = t.world.vault.key, .amount = usd_(1'000)}));
            deposit(env, t, alice_, 500);
            deposit(env, t, bob_, 2'000);
            env.close();
            trade(env, t.xrp, bob_, alice_, Number(100), Number(100));
            movePrices(env, t, 110, std::nullopt);
            settleByOwner(env, t.xrp);

            deposit(env, t, alice_, 1);
            env.close();
            BEAST_EXPECT(!env.le(positionKeylet(alice_, t, t.xrp)));
            BEAST_EXPECT(cashOf(env, alice_, t) == Number(1));
            BEAST_EXPECT(creditOf(env, alice_, t) == Number(500));
            BEAST_EXPECT(exchangeField(env, t, sfVaultLossInWindow) == Number(500));
            auto const sleVault = env.le(t.world.vault);
            BEAST_EXPECT(sleVault && (*sleVault)[sfAssetsTotal] == Number(500));
            BEAST_EXPECT(marketField(env, t.xrp, sfLossIndexLong) == Number(0));
            expectAccounting(env, t);
        }
    }

public:
    void
    run() override
    {
        testRefreshFundingFlows();
        testSnapshotsAtZeroCrossing();
        testStaleRefusesOrders();
        testStaleWithdrawOtherMarkets();
        testSettleStaleByAnyone();
        testSettlementCloseAtPrice();
        testLossesCloseFirstGrace();
        testSettlementShortfallWaterfall();
    }
};

BEAST_DEFINE_TESTSUITE(PerpSettle, perp, xrpl);

}  // namespace xrpl

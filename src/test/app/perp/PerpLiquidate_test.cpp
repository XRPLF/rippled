#include <test/app/perp/PerpTestBase.h>
#include <test/jtx/Env.h>
#include <test/jtx/Oracle.h>
#include <test/jtx/amount.h>
#include <test/jtx/credentials.h>
#include <test/jtx/permissioned_dex.h>
#include <test/jtx/perp.h>
#include <test/jtx/ter.h>
#include <test/jtx/txflags.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/helpers/PerpHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STVector256.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <optional>
#include <string>

namespace xrpl {

namespace {

/**
 * How a liquidation scenario's exchange and markets are created.
 */
struct LiquidationOptions
{
    /**
     * Adds an ETH/USD market beside XRP/USD.
     */
    bool eth{};
    /**
     * PerpMarketSet flags of the XRP/USD market.
     */
    std::uint32_t xrpFlags{};
    std::optional<Number> rewardMin;
    /**
     * Binds the owner's vault with this sfVaultLossLimit.
     */
    std::optional<std::uint32_t> vaultLossLimit;
    /**
     * Puts the exchange in a permissioned domain holding every trader and the liquidator.
     */
    bool domain{};
};

}  // namespace

class PerpLiquidate_test : public PerpTestBase
{
    using Perp = test::jtx::Perp;

    struct Trading
    {
        World world;
        Keylet exchange;
        Keylet xrp;
        Keylet eth;
    };

    std::string const credType_{"Cred"};
    Account const domainOwner_{"domainOwner"};

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
    marketTx(Keylet const& exchange, std::string const& base, LiquidationOptions const& options)
        const
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
        if (options.rewardMin)
            jv[sfLiquidationRewardMin] = to_string(*options.rewardMin);
        return jv;
    }

    /**
     * Oracles at XRP 100 (and ETH 100), a USD exchange and its markets. Penalty 1 %, reward share
     * 50 %, chunk minimum 10 000. Closes the ledger.
     */
    Trading
    setup(Env& env, LiquidationOptions const& options = {})
    {
        using namespace test::jtx;
        auto const world =
            setupMarketWorld(env, prices(100, options.eth ? std::optional{100u} : std::nullopt));

        std::optional<uint256> domainID;
        if (options.domain)
            domainID = setupDomain(env, {alice_, bob_, carol_, liq_}, domainOwner_, credType_);

        auto const exchange = Perp::exchangeKeylet(env, owner_);
        auto create = Perp::exchangeSet(owner_, usd_);
        if (options.vaultLossLimit)
        {
            create[sfVaultID] = to_string(world.vault.key);
            create[sfVaultLossWindow] = 86'400;
            create[sfVaultLossLimit] = *options.vaultLossLimit;
        }
        if (domainID)
            create[sfDomainID] = to_string(*domainID);
        env(create);
        env.close();

        auto xrp = marketTx(exchange, "XRP", options);
        if (options.xrpFlags != 0)
            xrp[jss::Flags] = options.xrpFlags;
        env(xrp);
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
    movePrices(Env& env, Trading& t, std::uint32_t xrp, std::optional<std::uint32_t> eth)
    {
        env.close();
        for (auto& oracle : t.world.oracles)
            oracle.set(test::jtx::oracle::UpdateArg{.series = prices(xrp, eth)});
        env.close();
    }

    void
    deposit(
        Env& env,
        Trading const& t,
        Account const& account,
        std::uint32_t amount,
        std::uint32_t subaccount = 0)
    {
        auto jv = Perp::deposit(account, t.exchange.key, usd_(amount));
        if (subaccount != 0)
            jv[sfSubaccount] = subaccount;
        env(jv);
    }

    void
    cover(Env& env, Trading const& t, std::uint32_t amount)
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
     * Opens a position of size between a buyer and a seller at price: the maker rests first and
     * the taker fills it in the next ledger.
     */
    void
    trade(
        Env& env,
        Keylet const& market,
        Account const& buyer,
        Account const& seller,
        Number const& size,
        Number const& price,
        bool sellerTakes = true)
    {
        auto const& maker = sellerTakes ? buyer : seller;
        auto const& taker = sellerTakes ? seller : buyer;
        bool const makerSells = !sellerTakes;
        env(order(maker, market, price, size, makerSells ? tfPerpSell : 0));
        env.close();
        env(order(taker, market, price, size, makerSells ? 0 : tfPerpSell));
        env.close();
    }

    [[nodiscard]] static Keylet
    marginKeylet(Account const& account, Trading const& t, std::uint32_t subaccount = 0)
    {
        return keylet::marginAccount(account.id(), t.exchange.key, subaccount);
    }

    [[nodiscard]] static Keylet
    positionKeylet(
        Account const& account,
        Trading const& t,
        Keylet const& market,
        std::uint32_t subaccount = 0)
    {
        return keylet::marginPosition(marginKeylet(account, t, subaccount).key, market.key);
    }

    [[nodiscard]] static Number
    sizeOf(Env& env, Account const& account, Trading const& t, Keylet const& market)
    {
        auto const sle = env.le(positionKeylet(account, t, market));
        return sle ? Number((*sle)[sfSize]) : Number();
    }

    [[nodiscard]] static Number
    cashOf(Env& env, Account const& account, Trading const& t, std::uint32_t subaccount = 0)
    {
        auto const sle = env.le(marginKeylet(account, t, subaccount));
        return sle ? Number((*sle)[sfCash]) : Number(-1'000'000);
    }

    [[nodiscard]] static Number
    creditOf(Env& env, Account const& account, Trading const& t)
    {
        auto const sle = env.le(marginKeylet(account, t));
        return sle ? Number((*sle)[sfWaterfallCredit]) : Number(-1);
    }

    [[nodiscard]] static Number
    exchangeField(Env& env, Trading const& t, SF_NUMBER const& field)
    {
        auto const sle = env.le(t.exchange);
        return sle ? Number((*sle)[field]) : Number(-1'000'000);
    }

    [[nodiscard]] static json::Value
    liquidate(
        Account const& liquidator,
        Account const& target,
        Trading const& t,
        Keylet const& market,
        std::uint32_t flags)
    {
        return Perp::liquidate(liquidator, marginKeylet(target, t).key, market.key, flags);
    }

    /**
     * Checks invariants 1 and 2 of the spec on the exchange.
     */
    void
    expectAccounting(Env& env, Trading const& t)
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

    /**
     * Moves amount of the exchange's cover to its insurance fund in the open ledger only.
     */
    void
    seedInsurance(Env& env, Trading const& t, Number const& amount)
    {
        seed(env, t.exchange, [&](ApplyView&, SLE::pointer const& sle) {
            (*sle)[sfCoverAvailable] = (*sle)[sfCoverAvailable] - amount;
            (*sle)[sfInsuranceFund] = (*sle)[sfInsuranceFund] + amount;
        });
    }

    /**
     * The review's finding 1 position: alice short 100 at entry 100 with cash, bob long 100 with
     * 2000, carol and liq with 2000 and 1000 deposited, and the price moved to 110.
     */
    void
    shortAt100(Env& env, Trading& t, std::uint32_t aliceCash)
    {
        deposit(env, t, alice_, aliceCash);
        deposit(env, t, bob_, 2'000);
        deposit(env, t, carol_, 2'000);
        deposit(env, t, liq_, 1'000);
        env.close();
        trade(env, t.xrp, bob_, alice_, Number(100), Number(100));
        BEAST_EXPECT(sizeOf(env, alice_, t, t.xrp) == Number(-100));
        movePrices(env, t, 110, std::nullopt);
    }

    void
    testDisabled()
    {
        testcase("disabled");
        using namespace test::jtx;
        Env env{*this, all_ - featurePerpetualFutures};
        fundAccounts(env);
        env(Perp::liquidate(liq_, uint256{1}, uint256{2}, tfPerpLiquidateToBook), Ter(temDISABLED));
    }

    void
    testHealthy()
    {
        testcase("healthy");
        using namespace test::jtx;
        Env env{*this, all_};
        auto t = setup(env);
        deposit(env, t, alice_, 1'000);
        deposit(env, t, bob_, 2'000);
        deposit(env, t, liq_, 1'000);
        env.close();
        trade(env, t.xrp, bob_, alice_, Number(100), Number(100));

        // Equity 1000 is above MM 200 in both modes.
        env(liquidate(liq_, alice_, t, t.xrp, tfPerpLiquidateToBook), Ter(tecPERP_HEALTHY));
        env(liquidate(liq_, alice_, t, t.xrp, tfPerpLiquidateTakeover), Ter(tecPERP_HEALTHY));

        // The liquidator needs a margin account, and the target a position in the market.
        env(liquidate(carol_, alice_, t, t.xrp, tfPerpLiquidateToBook), Ter(tecNO_ENTRY));
        env(liquidate(liq_, carol_, t, t.xrp, tfPerpLiquidateToBook), Ter(tecNO_ENTRY));
        env(Perp::liquidate(liq_, marginKeylet(liq_, t).key, t.xrp.key, tfPerpLiquidateToBook),
            Ter(tecNO_ENTRY));
        env.close();
        BEAST_EXPECT(sizeOf(env, alice_, t, t.xrp) == Number(-100));
        BEAST_EXPECT(cashOf(env, alice_, t) == Number(1'000));
    }

    /**
     * One liquidation of alice short 100 at 100, marked at 110, with cash: in book mode against
     * carol's ask of 100 at 108, else by liq's takeover at the mark.
     */
    void
    chunkCase(
        bool takeover,
        std::uint32_t aliceCash,
        Number const& expectedSize,
        Number const& expectedCash,
        Number const& expectedReward)
    {
        using namespace test::jtx;
        Env env{*this, all_};
        auto t = setup(env);
        shortAt100(env, t, aliceCash);
        if (!takeover)
        {
            env(order(carol_, t.xrp, Number(108), Number(100), tfPerpSell));
            env.close();
        }

        env(liquidate(
            liq_, alice_, t, t.xrp, takeover ? tfPerpLiquidateTakeover : tfPerpLiquidateToBook));
        env.close();
        BEAST_EXPECT(sizeOf(env, alice_, t, t.xrp) == expectedSize);
        BEAST_EXPECT(cashOf(env, alice_, t) == expectedCash);
        BEAST_EXPECT(cashOf(env, liq_, t) == Number(1'000) + expectedReward);
        if (takeover)
        {
            BEAST_EXPECT(sizeOf(env, liq_, t, t.xrp) == Number(-100) - expectedSize);
        }
        else
        {
            BEAST_EXPECT(sizeOf(env, carol_, t, t.xrp) == Number(-100) - expectedSize);
        }
        expectAccounting(env, t);
    }

    void
    testChunkRule()
    {
        testcase("chunk rule");

        // Cash 1200: equity 200 sits in [2/3 MM, MM) = [146.67, 220) at notional 11000 above
        // the chunk minimum, so 20 % of the size is taken. Book: realized 20 x (100 - 108) = -160,
        // equity after 240, penalty 20 x 110 x 1 % = 22, half to liq. Takeover: realized
        // 20 x (100 - 110) = -200, equity after 200, penalty 22.
        chunkCase(false, 1'200, Number(-80), Number(1'018), Number(11));
        chunkCase(true, 1'200, Number(-80), Number(978), Number(11));

        // Cash 1100: equity 100 is below 2/3 MM, so the whole position goes. Book: realized -800,
        // cash 300, penalty min(110, 300) = 110. Takeover: realized -1000, cash 100, penalty
        // min(110, 100) = 100.
        chunkCase(false, 1'100, Number(0), Number(190), Number(55));
        chunkCase(true, 1'100, Number(0), Number(0), Number(50));
    }

    void
    testBookModeFillsFirst()
    {
        testcase("book mode fills first");
        using namespace test::jtx;
        Env env{*this, all_};
        auto t = setup(env);
        cover(env, t, 1'000);
        shortAt100(env, t, 500);
        env(order(carol_, t.xrp, Number(108), Number(100), tfPerpSell));
        env.close();

        // Before: cash 500, uPnL -100 x 110 + 10000 = -1000, equity -500, MM 220.
        // The waterfall-first design would credit 500 (equity at mark), then the fill at 108
        // would realise -800 and leave cash 200 to the target. Filling first: realized
        // -100 x 108 + 10000 = -800, cash -300 on a flat account, penalty
        // min(110, max(0, -300)) = 0, and the waterfall pays D = 300 from the cover.
        env(liquidate(liq_, alice_, t, t.xrp, tfPerpLiquidateToBook));
        env.close();

        BEAST_EXPECT(!env.le(positionKeylet(alice_, t, t.xrp)));
        auto const margin = env.le(marginKeylet(alice_, t));
        if (!BEAST_EXPECT(margin))
            return;
        BEAST_EXPECT(margin->getFieldV256(sfMarkets).empty());
        BEAST_EXPECT((*margin)[sfCash] == Number(0));
        BEAST_EXPECT((*margin)[sfWaterfallCredit] == Number(300));
        BEAST_EXPECT(exchangeField(env, t, sfCoverAvailable) == Number(700));
        BEAST_EXPECT(exchangeField(env, t, sfInsuranceFund) == Number(0));
        BEAST_EXPECT(exchangeField(env, t, sfBadDebt) == Number(0));
        BEAST_EXPECT(sizeOf(env, carol_, t, t.xrp) == Number(-100));
        BEAST_EXPECT(cashOf(env, liq_, t) == Number(1'000));
        auto const market = env.le(t.xrp);
        BEAST_EXPECT(market && (*market)[sfOpenInterestLong] == Number(100));
        BEAST_EXPECT(market && (*market)[sfOpenInterestShort] == Number(100));
        BEAST_EXPECT(market && (*market)[sfLossIndexLong] == Number(0));
        expectAccounting(env, t);
    }

    /**
     * alice short 100 XRP at 100 and long 10 ETH at 100 with cash 600, XRP moved to 110, and
     * carol's ask of 100 XRP at 108. eth sets the ETH price after the move; unset leaves ETH
     * unpriced, so its market turns Stale.
     */
    Trading
    twoMarkets(Env& env, std::optional<std::uint32_t> eth)
    {
        auto t = setup(env, {.eth = true});
        cover(env, t, 1'000);
        deposit(env, t, alice_, 600);
        deposit(env, t, bob_, 2'000);
        deposit(env, t, carol_, 2'000);
        deposit(env, t, liq_, 1'000);
        env.close();
        trade(env, t.xrp, bob_, alice_, Number(100), Number(100));
        trade(env, t.eth, alice_, bob_, Number(10), Number(100), false);
        BEAST_EXPECT(sizeOf(env, alice_, t, t.eth) == Number(10));
        movePrices(env, t, 110, eth);
        env(order(carol_, t.xrp, Number(108), Number(100), tfPerpSell));
        env.close();
        return t;
    }

    void
    testBookModeNotFlatNoWaterfall()
    {
        testcase("book mode not flat no waterfall");
        using namespace test::jtx;
        Env env{*this, all_};
        auto t = twoMarkets(env, 100);

        // Equity 600 - 1000 + 0 = -400 is below 2/3 MM, so all 100 fill at 108: realized -800,
        // cash -200. The ETH position stays, so nothing is paid yet.
        env(liquidate(liq_, alice_, t, t.xrp, tfPerpLiquidateToBook));
        env.close();
        BEAST_EXPECT(!env.le(positionKeylet(alice_, t, t.xrp)));
        BEAST_EXPECT(sizeOf(env, alice_, t, t.eth) == Number(10));
        BEAST_EXPECT(cashOf(env, alice_, t) == Number(-200));
        BEAST_EXPECT(creditOf(env, alice_, t) == Number(0));
        BEAST_EXPECT(exchangeField(env, t, sfCoverAvailable) == Number(1'000));
        BEAST_EXPECT(exchangeField(env, t, sfBadDebt) == Number(0));
        expectAccounting(env, t);
    }

    void
    testStaleOtherMarketValuedAtLastMark()
    {
        testcase("stale other market valued at last mark");
        using namespace test::jtx;
        Env env{*this, all_};
        auto t = twoMarkets(env, std::nullopt);

        // The Stale market itself cannot be liquidated.
        env(liquidate(liq_, alice_, t, t.eth, tfPerpLiquidateToBook), Ter(tecPERP_STALE_PRICE));
        env.close();
        auto const eth = env.le(t.eth);
        BEAST_EXPECT(eth && (*eth)[sfMarketStatus] == perp::kMarketStale);

        // ETH counts at its last mark 100, so equity is -400 as with a Live ETH market.
        env(liquidate(liq_, alice_, t, t.xrp, tfPerpLiquidateToBook));
        env.close();
        BEAST_EXPECT(!env.le(positionKeylet(alice_, t, t.xrp)));
        BEAST_EXPECT(sizeOf(env, alice_, t, t.eth) == Number(10));
        BEAST_EXPECT(cashOf(env, alice_, t) == Number(-200));
        expectAccounting(env, t);
    }

    void
    testTakeoverBankruptExact()
    {
        testcase("takeover bankrupt exact");
        using namespace test::jtx;
        Env env{*this, all_};
        auto t = setup(env);
        cover(env, t, 1'000);
        shortAt100(env, t, 500);

        // The waterfall pays D = -equity = 500 first; the close at mark 110 then realizes
        // exactly -1000, so the target ends with cash 0 and no penalty is due.
        env(liquidate(liq_, alice_, t, t.xrp, tfPerpLiquidateTakeover));
        env.close();
        BEAST_EXPECT(!env.le(positionKeylet(alice_, t, t.xrp)));
        BEAST_EXPECT(cashOf(env, alice_, t) == Number(0));
        BEAST_EXPECT(creditOf(env, alice_, t) == Number(500));
        BEAST_EXPECT(exchangeField(env, t, sfCoverAvailable) == Number(500));
        BEAST_EXPECT(cashOf(env, liq_, t) == Number(1'000));
        BEAST_EXPECT(sizeOf(env, liq_, t, t.xrp) == Number(-100));
        auto const position = env.le(positionKeylet(liq_, t, t.xrp));
        BEAST_EXPECT(position && (*position)[sfEntryNotional] == Number(-11'000));
        auto const market = env.le(t.xrp);
        BEAST_EXPECT(market && (*market)[sfOpenInterestShort] == Number(100));
        expectAccounting(env, t);
    }

    void
    testTakeoverPositionLimits()
    {
        testcase("takeover position limits");
        using namespace test::jtx;
        {
            // The liquidator holds ETH, so it cannot take a position in isolated-only XRP.
            Env env{*this, all_};
            auto t = setup(env, {.eth = true, .xrpFlags = tfPerpIsolatedOnly});
            shortAt100(env, t, 500);
            movePrices(env, t, 110, 100);
            trade(env, t.eth, liq_, carol_, Number(1), Number(100));
            env(liquidate(liq_, alice_, t, t.xrp, tfPerpLiquidateTakeover),
                Ter(tecPERP_POSITION_LIMIT));
            env.close();
            BEAST_EXPECT(sizeOf(env, alice_, t, t.xrp) == Number(-100));
        }
        {
            // With 100 of cash the liquidator cannot meet IM 440 on short 100 at 110.
            Env env{*this, all_};
            auto t = setup(env);
            deposit(env, t, alice_, 500);
            deposit(env, t, bob_, 2'000);
            deposit(env, t, liq_, 100);
            env.close();
            trade(env, t.xrp, bob_, alice_, Number(100), Number(100));
            movePrices(env, t, 110, std::nullopt);
            env(liquidate(liq_, alice_, t, t.xrp, tfPerpLiquidateTakeover),
                Ter(tecPERP_INSUFFICIENT_MARGIN));
            env.close();
            BEAST_EXPECT(sizeOf(env, alice_, t, t.xrp) == Number(-100));
            BEAST_EXPECT(!env.le(positionKeylet(liq_, t, t.xrp)));
        }
    }

    void
    testTakeoverDomain()
    {
        testcase("takeover domain");
        using namespace test::jtx;
        Env env{*this, all_};
        auto t = setup(env, {.domain = true});
        cover(env, t, 1'000);
        shortAt100(env, t, 500);
        env(order(carol_, t.xrp, Number(108), Number(100), tfPerpSell));
        env.close();

        // A lapsed liquidator may not take a position over, but book mode is never gated.
        env(credentials::deleteCred(liq_, liq_, domainOwner_, credType_));
        env.close();
        env(liquidate(liq_, alice_, t, t.xrp, tfPerpLiquidateTakeover), Ter(tecNO_AUTH));
        env(liquidate(liq_, alice_, t, t.xrp, tfPerpLiquidateToBook));
        env.close();
        BEAST_EXPECT(!env.le(positionKeylet(alice_, t, t.xrp)));
        BEAST_EXPECT(cashOf(env, alice_, t) == Number(0));
    }

    void
    testPenaltyAndReward()
    {
        testcase("penalty and reward");
        using namespace test::jtx;
        Env env{*this, all_};
        auto t = setup(env);
        shortAt100(env, t, 1'100);

        // Full takeover at 110 realizes -1000, leaving equity 100. The penalty
        // min(100 x 110 x 1 %, 100) = 100 is split 50 to liq and 50 to the insurance fund.
        env(liquidate(liq_, alice_, t, t.xrp, tfPerpLiquidateTakeover));
        env.close();
        BEAST_EXPECT(cashOf(env, alice_, t) == Number(0));
        BEAST_EXPECT(cashOf(env, liq_, t) == Number(1'050));
        BEAST_EXPECT(exchangeField(env, t, sfInsuranceFund) == Number(50));
        BEAST_EXPECT(!env.le(positionKeylet(alice_, t, t.xrp)));
        expectAccounting(env, t);
    }

    void
    testNoRewardSameAccount()
    {
        testcase("no reward same account");
        using namespace test::jtx;
        Env env{*this, all_};
        auto t = setup(env);
        deposit(env, t, alice_, 1'000, 1);
        shortAt100(env, t, 1'200);
        env(order(carol_, t.xrp, Number(108), Number(100), tfPerpSell));
        env.close();

        // alice's subaccount 1 liquidates subaccount 0: the penalty of 22 goes wholly to the
        // insurance fund.
        auto jv = liquidate(alice_, alice_, t, t.xrp, tfPerpLiquidateToBook);
        jv[sfSubaccount] = 1;
        env(jv);
        env.close();
        BEAST_EXPECT(sizeOf(env, alice_, t, t.xrp) == Number(-80));
        BEAST_EXPECT(cashOf(env, alice_, t) == Number(1'018));
        BEAST_EXPECT(cashOf(env, alice_, t, 1) == Number(1'000));
        BEAST_EXPECT(exchangeField(env, t, sfInsuranceFund) == Number(22));
        expectAccounting(env, t);
    }

    void
    testRewardMinFromInsurance()
    {
        testcase("reward minimum from insurance");
        using namespace test::jtx;

        // The bankrupt book case has no penalty, so liq receives min(5, insurance fund) before
        // the waterfall draws the rest of the fund and then the cover for D = 300.
        auto const run =
            [&](Number const& insurance, Number const& reward, Number const& coverAfter) {
                Env env{*this, all_};
                auto t = setup(env, {.rewardMin = Number(5)});
                cover(env, t, 1'000);
                shortAt100(env, t, 500);
                env(order(carol_, t.xrp, Number(108), Number(100), tfPerpSell));
                env.close();
                seedInsurance(env, t, insurance);
                env(liquidate(liq_, alice_, t, t.xrp, tfPerpLiquidateToBook));
                BEAST_EXPECT(cashOf(env, liq_, t) == Number(1'000) + reward);
                BEAST_EXPECT(exchangeField(env, t, sfInsuranceFund) == Number(0));
                BEAST_EXPECT(exchangeField(env, t, sfCoverAvailable) == coverAfter);
                BEAST_EXPECT(cashOf(env, alice_, t) == Number(0));
                BEAST_EXPECT(creditOf(env, alice_, t) == Number(300));
                expectAccounting(env, t);
            };
        // Fund 3: liq gets 3, the cover pays 300 of 997.
        run(Number(3), Number(3), Number(697));
        // Fund 10: liq gets 5, the fund pays 5 and the cover 295 of 990.
        run(Number(10), Number(5), Number(695));
    }

    void
    testWaterfallOrder()
    {
        testcase("waterfall order");
        using namespace test::jtx;
        {
            // D = 300: insurance 50, cover 60, the vault 10 % of 1000 = 100 per window, and the
            // remaining 90 on the long side's loss index, 90 / 100 = 0.9 per unit.
            Env env{*this, all_};
            auto t = setup(env, {.vaultLossLimit = 10'000});
            Vault const vault{.env = env};
            env(vault.deposit(
                {.depositor = owner_, .id = t.world.vault.key, .amount = usd_(1'000)}));
            cover(env, t, 110);
            shortAt100(env, t, 500);
            env(order(carol_, t.xrp, Number(108), Number(100), tfPerpSell));
            env.close();
            seedInsurance(env, t, Number(50));
            env(liquidate(liq_, alice_, t, t.xrp, tfPerpLiquidateToBook));

            BEAST_EXPECT(exchangeField(env, t, sfInsuranceFund) == Number(0));
            BEAST_EXPECT(exchangeField(env, t, sfCoverAvailable) == Number(0));
            BEAST_EXPECT(exchangeField(env, t, sfVaultLossInWindow) == Number(100));
            auto const exchange = env.le(t.exchange);
            BEAST_EXPECT(
                exchange &&
                (*exchange)[sfVaultLossWindowStart] == perp::refreshTime(*env.current()));
            auto const sleVault = env.le(t.world.vault);
            BEAST_EXPECT(sleVault && (*sleVault)[sfAssetsTotal] == Number(900));
            BEAST_EXPECT(sleVault && (*sleVault)[sfAssetsAvailable] == Number(900));
            auto const market = env.le(t.xrp);
            BEAST_EXPECT(market && (*market)[sfLossIndexLong] == Number(9, -1));
            BEAST_EXPECT(market && (*market)[sfLossIndexShort] == Number(0));
            BEAST_EXPECT(exchangeField(env, t, sfBadDebt) == Number(0));
            BEAST_EXPECT(cashOf(env, alice_, t) == Number(0));
            BEAST_EXPECT(creditOf(env, alice_, t) == Number(300));
            expectAccounting(env, t);
        }
        {
            // A vault holding 40 gives at most 40 even at a 100 % limit; 260 reaches the index.
            Env env{*this, all_};
            auto t = setup(env, {.vaultLossLimit = 100'000});
            Vault const vault{.env = env};
            env(vault.deposit({.depositor = owner_, .id = t.world.vault.key, .amount = usd_(40)}));
            shortAt100(env, t, 500);
            env(order(carol_, t.xrp, Number(108), Number(100), tfPerpSell));
            env.close();
            env(liquidate(liq_, alice_, t, t.xrp, tfPerpLiquidateToBook));
            env.close();
            auto const sleVault = env.le(t.world.vault);
            BEAST_EXPECT(sleVault && (*sleVault)[sfAssetsTotal] == Number(0));
            BEAST_EXPECT(sleVault && (*sleVault)[sfAssetsAvailable] == Number(0));
            auto const market = env.le(t.xrp);
            BEAST_EXPECT(market && (*market)[sfLossIndexLong] == Number(26, -1));
            BEAST_EXPECT(creditOf(env, alice_, t) == Number(300));
            expectAccounting(env, t);
        }
    }

    void
    testLossIndexCharged()
    {
        testcase("loss index charged");
        using namespace test::jtx;
        Env env{*this, all_};
        auto t = setup(env);
        shortAt100(env, t, 500);
        env(order(carol_, t.xrp, Number(108), Number(100), tfPerpSell));
        env.close();

        // With no other source, D = 300 lands on bob's long side: 300 / 100 = 3 per unit.
        env(liquidate(liq_, alice_, t, t.xrp, tfPerpLiquidateToBook));
        env.close();
        auto const market = env.le(t.xrp);
        BEAST_EXPECT(market && (*market)[sfLossIndexLong] == Number(3));
        BEAST_EXPECT(creditOf(env, alice_, t) == Number(300));
        expectAccounting(env, t);

        // bob pays 100 x 3 = 300 on his next settlement; carol's short is not charged.
        deposit(env, t, bob_, 1);
        deposit(env, t, carol_, 1);
        env.close();
        BEAST_EXPECT(cashOf(env, bob_, t) == Number(1'701));
        BEAST_EXPECT(cashOf(env, carol_, t) == Number(2'001));
        auto const position = env.le(positionKeylet(bob_, t, t.xrp));
        BEAST_EXPECT(position && (*position)[sfLossIndex] == Number(3));
        expectAccounting(env, t);
    }

    void
    testLossIndexZeroDivisorToBadDebt()
    {
        testcase("loss index zero divisor to bad debt");
        using namespace test::jtx;
        Env env{*this, all_};
        auto t = setup(env);
        shortAt100(env, t, 500);

        // bob's ask closes the only long, so no long is left to charge: D = 300 becomes bad debt.
        env(order(bob_, t.xrp, Number(108), Number(100), tfPerpSell));
        env.close();
        env(liquidate(liq_, alice_, t, t.xrp, tfPerpLiquidateToBook));
        env.close();
        auto const market = env.le(t.xrp);
        BEAST_EXPECT(market && (*market)[sfOpenInterestLong] == Number(0));
        BEAST_EXPECT(market && (*market)[sfLossIndexLong] == Number(0));
        BEAST_EXPECT(exchangeField(env, t, sfBadDebt) == Number(300));
        BEAST_EXPECT(cashOf(env, alice_, t) == Number(0));
        BEAST_EXPECT(creditOf(env, alice_, t) == Number(0));
        BEAST_EXPECT(cashOf(env, bob_, t) == Number(2'800));
        expectAccounting(env, t);
    }

    void
    testWaterfallCreditRepaid()
    {
        testcase("waterfall credit repaid");
        using namespace test::jtx;
        Env env{*this, all_};
        auto t = setup(env);
        cover(env, t, 1'000);
        shortAt100(env, t, 500);
        env(liquidate(liq_, alice_, t, t.xrp, tfPerpLiquidateTakeover));
        env.close();
        BEAST_EXPECT(creditOf(env, alice_, t) == Number(500));

        // Credit blocks withdrawal; the next settlement with cash repays it to the fund.
        env(Perp::withdraw(alice_, t.exchange.key, usd_(1)), Ter(tecPERP_WATERFALL_CREDIT));
        deposit(env, t, alice_, 200);
        env.close();
        BEAST_EXPECT(cashOf(env, alice_, t) == Number(200));
        deposit(env, t, alice_, 1);
        env.close();
        BEAST_EXPECT(cashOf(env, alice_, t) == Number(1));
        BEAST_EXPECT(creditOf(env, alice_, t) == Number(300));
        BEAST_EXPECT(exchangeField(env, t, sfInsuranceFund) == Number(200));
        expectAccounting(env, t);
    }

public:
    void
    run() override
    {
        testDisabled();
        testHealthy();
        testChunkRule();
        testBookModeFillsFirst();
        testBookModeNotFlatNoWaterfall();
        testStaleOtherMarketValuedAtLastMark();
        testTakeoverBankruptExact();
        testTakeoverPositionLimits();
        testTakeoverDomain();
        testPenaltyAndReward();
        testNoRewardSameAccount();
        testRewardMinFromInsurance();
        testWaterfallOrder();
        testLossIndexCharged();
        testLossIndexZeroDivisorToBadDebt();
        testWaterfallCreditRepaid();
    }
};

BEAST_DEFINE_TESTSUITE(PerpLiquidate, perp, xrpl);

}  // namespace xrpl

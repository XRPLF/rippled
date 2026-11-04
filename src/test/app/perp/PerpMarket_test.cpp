#include <test/app/perp/PerpTestBase.h>
#include <test/jtx/Env.h>
#include <test/jtx/Oracle.h>
#include <test/jtx/perp.h>
#include <test/jtx/ter.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/helpers/PerpHelpers.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace xrpl {

class PerpMarket_test : public PerpTestBase
{
    using Perp = test::jtx::Perp;

    /**
     * The JSON form of margin tiers, as PerpMarketSet carries them.
     */
    static json::Value
    tiersJson(std::vector<Perp::Tier> const& tiers)
    {
        json::Value array{json::ValueType::Array};
        for (auto const& tier : tiers)
        {
            json::Value entry;
            entry[sfMarginTier][sfNotionalMax] = to_string(tier.notionalMax);
            entry[sfMarginTier][sfInitialMarginRate] = tier.initialMarginRate;
            entry[sfMarginTier][sfMaintenanceMarginRate] = tier.maintenanceMarginRate;
            array.append(entry);
        }
        return array;
    }

    /**
     * The initial margin rate of tier index of an sfMarginTiers array.
     */
    static std::uint32_t
    initialRate(STArray const& tiers, std::size_t index)
    {
        return tiers[index][sfInitialMarginRate];
    }

    void
    testCreateNeedsQuorum()
    {
        testcase("create needs quorum");
        using namespace test::jtx;
        Env env{*this, all_};
        auto world = setupMarketWorld(env);
        auto const exchange = createExchange(env, owner_, usd_);
        auto args = marketArgs();
        args.indexMaxLedgers = 3;

        // Every quote ages out, then one of three is rewritten: no quorum.
        for (int i = 0; i < 4; ++i)
            env.close();
        world.oracles[0].set(oracle::UpdateArg{.series = {{"XRP", "USD", 21, 1}}});
        env(Perp::marketSet(owner_, exchange.key, args), Ter(tecPERP_STALE_PRICE));
        env.close();
        BEAST_EXPECT(!env.le(marketKeylet(exchange.key)));

        // Two of three fresh quotes, 2.1 and 2.3, meet the quorum with index 2.2.
        world.oracles[1].set(oracle::UpdateArg{.series = {{"XRP", "USD", 23, 1}}});
        auto const ownerCount = env.ownerCount(owner_);
        auto const t = perp::refreshTime(*env.current());
        auto const seq = env.current()->seq();
        env(Perp::marketSet(owner_, exchange.key, args));
        env.close();

        auto const market = env.le(marketKeylet(exchange.key));
        if (!BEAST_EXPECT(market))
            return;
        BEAST_EXPECT(env.ownerCount(owner_) == ownerCount + 1);
        BEAST_EXPECT(env.le(exchange) && (*env.le(exchange))[sfMarketCount] == 1);
        BEAST_EXPECT((*market)[sfExchangeID] == exchange.key);
        BEAST_EXPECT((*market)[sfIndexPrice] == Number(22, -1));
        BEAST_EXPECT((*market)[sfMarkPrice] == Number(22, -1));
        BEAST_EXPECT((*market)[sfMarketStatus] == perp::kMarketLive);
        BEAST_EXPECT((*market)[sfLastRefreshTime] == t);
        BEAST_EXPECT((*market)[sfRefreshLedger] == seq);
        BEAST_EXPECT((*market)[sfOpenInterestLong] == Number(0));
        BEAST_EXPECT(!market->isFlag(lsfPerpIsolatedOnly));
        BEAST_EXPECT(market->getFieldArray(sfIndexOracles).size() == 3);
        BEAST_EXPECT(market->getFieldArray(sfMarginTiers).size() == 2);

        // One market per pair per exchange.
        env(Perp::marketSet(owner_, exchange.key, args), Ter(tecDUPLICATE));

        // The owner creates markets; the exchange must exist.
        env(Perp::marketSet(alice_, exchange.key, args), Ter(tecNO_PERMISSION));
        env(Perp::marketSet(owner_, uint256{3}, args), Ter(tecNO_ENTRY));

        // tfPerpIsolatedOnly sets the market flag on creation.
        auto const isolated = createExchange(env, owner_, usd_);
        world.oracles[2].set(oracle::UpdateArg{.series = {{"XRP", "USD", 22, 1}}});
        auto jv = Perp::marketSet(owner_, isolated.key, args);
        jv[jss::Flags] = tfPerpIsolatedOnly;
        env(jv);
        env.close();
        auto const isolatedMarket = env.le(marketKeylet(isolated.key));
        BEAST_EXPECT(isolatedMarket && isolatedMarket->isFlag(lsfPerpIsolatedOnly));
    }

    void
    testTierValidation()
    {
        testcase("tier validation");
        using namespace test::jtx;
        Env env{*this, all_};
        setupMarketWorld(env);
        auto const exchange = createExchange(env, owner_, usd_);

        auto tiered = [&](std::vector<Perp::Tier> const& tiers) {
            auto args = marketArgs();
            args.tiers = tiers;
            return Perp::marketSet(owner_, exchange.key, args);
        };

        // Notional bounds must ascend.
        env(tiered(
                {{.notionalMax = Number{200'000},
                  .initialMarginRate = 10'000,
                  .maintenanceMarginRate = 5'000},
                 {.notionalMax = Number{100'000},
                  .initialMarginRate = 20'000,
                  .maintenanceMarginRate = 10'000}}),
            Ter(temMALFORMED));

        // The initial rate must exceed the maintenance rate.
        env(tiered(
                {{.notionalMax = Number{100'000},
                  .initialMarginRate = 5'000,
                  .maintenanceMarginRate = 5'000}}),
            Ter(temMALFORMED));

        // Rates must rise from tier to tier.
        env(tiered(
                {{.notionalMax = Number{100'000},
                  .initialMarginRate = 10'000,
                  .maintenanceMarginRate = 5'000},
                 {.notionalMax = Number{1'000'000},
                  .initialMarginRate = 10'000,
                  .maintenanceMarginRate = 8'000}}),
            Ter(temMALFORMED));

        // At least one tier and at most kMaxMarginTiers.
        env(tiered({}), Ter(temMALFORMED));
        env(tiered(
                {{.notionalMax = Number{1}, .initialMarginRate = 2, .maintenanceMarginRate = 1},
                 {.notionalMax = Number{2}, .initialMarginRate = 4, .maintenanceMarginRate = 3},
                 {.notionalMax = Number{3}, .initialMarginRate = 6, .maintenanceMarginRate = 5},
                 {.notionalMax = Number{4}, .initialMarginRate = 8, .maintenanceMarginRate = 7}}),
            Ter(temMALFORMED));
        env.close();
        BEAST_EXPECT(!env.le(marketKeylet(exchange.key)));

        // Three valid tiers are stored as given.
        std::vector<Perp::Tier> const three{
            {.notionalMax = Number{100'000},
             .initialMarginRate = 10'000,
             .maintenanceMarginRate = 5'000},
            {.notionalMax = Number{500'000},
             .initialMarginRate = 20'000,
             .maintenanceMarginRate = 10'000},
            {.notionalMax = Number{1'000'000},
             .initialMarginRate = 50'000,
             .maintenanceMarginRate = 25'000}};
        env(tiered(three));
        env.close();
        auto const market = env.le(marketKeylet(exchange.key));
        if (!BEAST_EXPECT(market))
            return;
        auto const& stored = market->getFieldArray(sfMarginTiers);
        BEAST_EXPECT(stored.size() == 3);
        BEAST_EXPECT(initialRate(stored, 2) == 50'000);

        // An update is validated the same way.
        auto update = Perp::marketUpdate(owner_, market->key());
        update[sfMarginTiers] = tiersJson(
            {{.notionalMax = Number{100'000},
              .initialMarginRate = 5'000,
              .maintenanceMarginRate = 5'000}});
        env(update, Ter(temMALFORMED));
    }

    void
    testMidSpreadBelowBand()
    {
        testcase("mid spread below band");
        using namespace test::jtx;
        Env env{*this, all_};
        setupMarketWorld(env);
        auto const exchange = createExchange(env, owner_, usd_);

        auto args = marketArgs();
        args.midSpreadMax = args.orderPriceBand;
        env(Perp::marketSet(owner_, exchange.key, args), Ter(temMALFORMED));
        env(Perp::marketSet(owner_, exchange.key, marketArgs()));
        env.close();
        auto const market = marketKeylet(exchange.key);

        auto update = [&](SF_UINT32 const& field, std::uint32_t value) {
            auto jv = Perp::marketUpdate(owner_, market.key);
            jv[field] = value;
            return jv;
        };

        // A queued raise of the spread is checked against the band it will meet.
        env(update(sfMidSpreadMax, 10'000), Ter(tecLIMIT_EXCEEDED));

        // An immediate narrowing of the band is checked against the current spread.
        env(update(sfOrderPriceBand, 1'000), Ter(tecLIMIT_EXCEEDED));

        // ...and against the queued spread.
        env(update(sfMidSpreadMax, 5'000));
        env.close();
        env(update(sfOrderPriceBand, 4'000), Ter(tecLIMIT_EXCEEDED));
        env(update(sfOrderPriceBand, 6'000));
        env.close();
        auto const sle = env.le(market);
        if (!BEAST_EXPECT(sle))
            return;
        BEAST_EXPECT((*sle)[sfOrderPriceBand] == 6'000);
        BEAST_EXPECT((*sle)[sfMidSpreadMax] == 1'000);
        BEAST_EXPECT(sle->getFieldObject(sfPendingParameters)[~sfMidSpreadMax] == 5'000u);
    }

    void
    testChangeClasses()
    {
        testcase("change classes");
        using namespace test::jtx;
        Env env{*this, all_};
        setupMarketWorld(env);
        auto const exchange = createExchange(env, owner_, usd_);
        env(Perp::marketSet(owner_, exchange.key, marketArgs()));
        env.close();
        auto const market = marketKeylet(exchange.key);

        // Fixed: the tick, the lot and the isolation flag cannot change.
        auto fixed = Perp::marketUpdate(owner_, market.key);
        fixed[sfPriceTick] = "0.01";
        env(fixed, Ter(temMALFORMED));
        auto isolate = Perp::marketUpdate(owner_, market.key);
        isolate[jss::Flags] = tfPerpIsolatedOnly;
        env(isolate, Ter(temMALFORMED));

        // Tightening changes apply at once: a lower OI cap (delayed up), a longer settle wait
        // (delayed down) and higher margin rates (delayed loosen).
        std::vector<Perp::Tier> const tighter{
            {.notionalMax = Number{100'000},
             .initialMarginRate = 12'000,
             .maintenanceMarginRate = 6'000},
            {.notionalMax = Number{1'000'000},
             .initialMarginRate = 22'000,
             .maintenanceMarginRate = 11'000}};
        auto tighten = Perp::marketUpdate(owner_, market.key);
        tighten[sfOpenInterestMax] = "500000";
        tighten[sfSettleStaleAfter] = 7'200;
        tighten[sfMarginTiers] = tiersJson(tighter);
        env(tighten);
        env.close();
        {
            auto const sle = env.le(market);
            if (!BEAST_EXPECT(sle))
                return;
            BEAST_EXPECT((*sle)[sfOpenInterestMax] == Number(500'000));
            BEAST_EXPECT((*sle)[sfSettleStaleAfter] == 7'200);
            BEAST_EXPECT(initialRate(sle->getFieldArray(sfMarginTiers), 0) == 12'000);
            BEAST_EXPECT(!sle->isFieldPresent(sfPendingParameters));
        }

        // Loosening changes are queued: any change of a delayed field, a higher OI cap, a
        // shorter settle wait and lower margin rates.
        auto const t = perp::refreshTime(*env.current());
        auto loosen = Perp::marketUpdate(owner_, market.key);
        loosen[sfBasisWindow] = 1'800;
        loosen[sfOpenInterestMax] = "2000000";
        loosen[sfSettleStaleAfter] = 3'600;
        loosen[sfMarginTiers] = tiersJson(marketArgs().tiers);
        env(loosen);
        env.close();
        {
            auto const sle = env.le(market);
            if (!BEAST_EXPECT(sle && sle->isFieldPresent(sfPendingParameters)))
                return;
            BEAST_EXPECT((*sle)[sfBasisWindow] == 3'600);
            BEAST_EXPECT((*sle)[sfOpenInterestMax] == Number(500'000));
            BEAST_EXPECT((*sle)[sfSettleStaleAfter] == 7'200);
            BEAST_EXPECT(initialRate(sle->getFieldArray(sfMarginTiers), 0) == 12'000);
            BEAST_EXPECT((*sle)[~sfParameterEffectiveTime] == t + perp::kMinParameterDelay.count());
            auto const& pending = sle->getFieldObject(sfPendingParameters);
            BEAST_EXPECT(pending[~sfBasisWindow] == 1'800u);
            BEAST_EXPECT(pending[~sfOpenInterestMax] == Number(2'000'000));
            BEAST_EXPECT(pending[~sfSettleStaleAfter] == 3'600u);
            BEAST_EXPECT(pending.isFieldPresent(sfMarginTiers));
        }

        // A raised notional bound alone loosens the tiers too.
        auto wider = Perp::marketUpdate(owner_, market.key);
        auto widerTiers = tighter;
        widerTiers[1].notionalMax = Number{2'000'000};
        wider[sfMarginTiers] = tiersJson(widerTiers);
        wider[sfBasisWindow] = 1'800;
        wider[sfOpenInterestMax] = "2000000";
        wider[sfSettleStaleAfter] = 3'600;
        env(wider);
        env.close();
        {
            auto const sle = env.le(market);
            if (!BEAST_EXPECT(sle && sle->isFieldPresent(sfPendingParameters)))
                return;
            BEAST_EXPECT(sle->getFieldArray(sfMarginTiers)[1][sfNotionalMax] == Number(1'000'000));
        }

        // The first refresh after the delay applies the queued change.
        env.close(env.now() + perp::kMinParameterDelay);
        env(Perp::marketUpdate(owner_, market.key));
        env.close();
        {
            auto const sle = env.le(market);
            if (!BEAST_EXPECT(sle))
                return;
            BEAST_EXPECT((*sle)[sfBasisWindow] == 1'800);
            BEAST_EXPECT((*sle)[sfOpenInterestMax] == Number(2'000'000));
            BEAST_EXPECT((*sle)[sfSettleStaleAfter] == 3'600);
            BEAST_EXPECT(sle->getFieldArray(sfMarginTiers)[1][sfNotionalMax] == Number(2'000'000));
            BEAST_EXPECT(!sle->isFieldPresent(sfPendingParameters));
            BEAST_EXPECT(!sle->isFieldPresent(sfParameterEffectiveTime));
        }

        // Only the owner updates a market, and the market must exist.
        auto other = Perp::marketUpdate(alice_, market.key);
        other[sfOpenInterestMax] = "1";
        env(other, Ter(tecNO_PERMISSION));
        auto missing = Perp::marketUpdate(owner_, uint256{5});
        missing[sfOpenInterestMax] = "1";
        env(missing, Ter(tecNO_ENTRY));
    }

    void
    testSettleLive()
    {
        testcase("settle live");
        using namespace test::jtx;
        Env env{*this, all_};
        setupMarketWorld(env);
        auto const live = createExchange(env, owner_, usd_);
        auto const stale = createExchange(env, owner_, usd_);
        env(Perp::marketSet(owner_, live.key, marketArgs()));
        env(Perp::marketSet(owner_, stale.key, marketArgs()));
        env.close();

        auto settle = [&](Account const& account, Keylet const& market) {
            auto jv = Perp::marketUpdate(account, market.key);
            jv[jss::Flags] = tfPerpMarketSettle;
            return jv;
        };
        auto const liveMarket = marketKeylet(live.key);
        auto const staleMarket = marketKeylet(stale.key);

        // Only the owner settles, and only with an update.
        env(settle(alice_, liveMarket), Ter(tecNO_PERMISSION));
        auto onCreate = Perp::marketSet(owner_, live.key, marketArgs());
        onCreate[jss::Flags] = tfPerpMarketSettle;
        env(onCreate, Ter(temMALFORMED));

        // A Live market settles at its current mark.
        auto const t = perp::refreshTime(*env.current());
        env(settle(owner_, liveMarket));
        env.close();
        {
            auto const sle = env.le(liveMarket);
            if (!BEAST_EXPECT(sle))
                return;
            BEAST_EXPECT((*sle)[sfMarketStatus] == perp::kMarketSettled);
            BEAST_EXPECT((*sle)[~sfSettlementPrice] == Number(2));
            BEAST_EXPECT((*sle)[~sfSettlementPrice] == (*sle)[sfMarkPrice]);
            BEAST_EXPECT((*sle)[~sfSettledAt] == t);
        }
        env(settle(owner_, liveMarket), Ter(tecPERP_MARKET_SETTLED));

        // A market without index quorum is not Live and cannot be settled by its owner.
        for (int i = 0; i < 11; ++i)
            env.close();
        env(settle(owner_, staleMarket), Ter(tecPERP_STALE_PRICE));
        env.close();
        auto const sle = env.le(staleMarket);
        BEAST_EXPECT(sle && (*sle)[sfMarketStatus] == perp::kMarketLive);
        BEAST_EXPECT(sle && !sle->isFieldPresent(sfSettlementPrice));
    }

    void
    testDeleteRequirements()
    {
        testcase("delete requirements");
        using namespace test::jtx;
        Env env{*this, all_};
        setupMarketWorld(env);
        auto const first = createExchange(env, owner_, usd_);
        auto const second = createExchange(env, owner_, usd_);
        auto const third = createExchange(env, owner_, usd_);
        for (auto const& exchange : {first, second, third})
            env(Perp::marketSet(owner_, exchange.key, marketArgs()));
        env.close();

        // Only the owner deletes a market, and the market must exist.
        auto const firstMarket = marketKeylet(first.key);
        env(Perp::marketDelete(alice_, firstMarket.key), Ter(tecNO_PERMISSION));
        env(Perp::marketDelete(owner_, uint256{11}), Ter(tecNO_ENTRY));

        // A Live market that never held a position is deleted.
        auto const ownerCount = env.ownerCount(owner_);
        env(Perp::marketDelete(owner_, firstMarket.key));
        env.close();
        BEAST_EXPECT(!env.le(firstMarket));
        BEAST_EXPECT(env.ownerCount(owner_) == ownerCount - 1);
        BEAST_EXPECT(env.le(first) && (*env.le(first))[sfMarketCount] == 0);

        // A Settled market with no open interest and no orders is deleted.
        auto const secondMarket = marketKeylet(second.key);
        auto settle = Perp::marketUpdate(owner_, secondMarket.key);
        settle[jss::Flags] = tfPerpMarketSettle;
        env(settle);
        env.close();
        env(Perp::marketDelete(owner_, secondMarket.key));
        env.close();
        BEAST_EXPECT(!env.le(secondMarket));
        BEAST_EXPECT(env.ownerCount(owner_) == ownerCount - 2);

        // Resting orders or open interest on either side block deletion.
        auto const thirdMarket = marketKeylet(third.key);
        seed(env, thirdMarket, [](ApplyView&, SLE::pointer const& market) {
            (*market)[sfOrderCount] = 1;
        });
        env(Perp::marketDelete(owner_, thirdMarket.key), Ter(tecHAS_OBLIGATIONS));
        seed(env, thirdMarket, [](ApplyView&, SLE::pointer const& market) {
            (*market)[sfOrderCount] = 0;
            (*market)[sfOpenInterestLong] = Number{1};
        });
        env(Perp::marketDelete(owner_, thirdMarket.key), Ter(tecHAS_OBLIGATIONS));
        seed(env, thirdMarket, [](ApplyView&, SLE::pointer const& market) {
            (*market)[sfOpenInterestLong] = Number{};
            (*market)[sfOpenInterestShort] = Number{1};
        });
        env(Perp::marketDelete(owner_, thirdMarket.key), Ter(tecHAS_OBLIGATIONS));
        BEAST_EXPECT(env.le(thirdMarket));
    }

    void
    testMarketLimit()
    {
        testcase("market limit");
        using namespace test::jtx;
        Env env{*this, all_};
        fundAccounts(env);
        auto const exchange = createExchange(env, owner_, usd_);

        // Oracle o1 quotes kMaxPerpMarkets pairs XRP/Cnn, ten to a document from document 2.
        auto quote = [](std::size_t i) {
            std::string name = "C00";
            name[1] = static_cast<char>('0' + (i / 10));
            name[2] = static_cast<char>('0' + (i % 10));
            return name;
        };
        auto document = [](std::size_t i) { return static_cast<std::uint32_t>((i / 10) + 2); };
        std::vector<Oracle> oracles;
        for (std::size_t first = 0; first < perp::kMaxPerpMarkets; first += 10)
        {
            oracle::DataSeries series;
            for (std::size_t i = first; i < first + 10 && i < perp::kMaxPerpMarkets; ++i)
                series.emplace_back("XRP", quote(i), 20, 1);
            oracles.emplace_back(
                env,
                oracle::CreateArg{
                    .owner = o1_.id(),
                    .documentID = json::UInt{document(first)},
                    .series = series});
        }

        auto argsFor = [&](std::size_t i) {
            Perp::MarketArgs args;
            args.oracles = {{.owner = o1_.id(), .documentID = document(i)}};
            args.quoteAsset = quote(i);
            args.indexMaxLedgers = 100;
            args.indexMaxAge = 3'600;
            return args;
        };
        for (std::size_t i = 0; i < perp::kMaxPerpMarkets; ++i)
            env(Perp::marketSet(owner_, exchange.key, argsFor(i)));
        env.close();
        BEAST_EXPECT(
            env.le(exchange) && (*env.le(exchange))[sfMarketCount] == perp::kMaxPerpMarkets);

        // The next market is refused until one is deleted.
        auto extra = argsFor(0);
        extra.quoteAsset = "USD";
        env(Perp::marketSet(owner_, exchange.key, extra), Ter(tecPERP_MARKET_LIMIT));
        env.close();
        env(Perp::marketDelete(owner_, marketKeylet(exchange.key, "XRP", quote(0)).key));
        env.close();
        env(Perp::marketSet(owner_, exchange.key, argsFor(0)));
        env.close();
        BEAST_EXPECT(
            env.le(exchange) && (*env.le(exchange))[sfMarketCount] == perp::kMaxPerpMarkets);
    }

public:
    void
    run() override
    {
        testCreateNeedsQuorum();
        testTierValidation();
        testMidSpreadBelowBand();
        testChangeClasses();
        testSettleLive();
        testDeleteRequirements();
        testMarketLimit();
    }
};

BEAST_DEFINE_TESTSUITE(PerpMarket, perp, xrpl);

}  // namespace xrpl

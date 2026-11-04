#include <test/app/perp/PerpTestBase.h>
#include <test/jtx/Env.h>
#include <test/jtx/Oracle.h>
#include <test/jtx/amount.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/PerpHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace xrpl {

class PerpPrice_test : public PerpTestBase
{
    /**
     * The index parameters of a market built directly as a ledger entry.
     */
    struct MarketParams
    {
        std::vector<AccountID> oracles;
        std::string quoteAsset{"USD"};
        std::uint32_t indexMaxLedgers{10};
    };

    [[nodiscard]] Keylet
    exchangeKeylet() const
    {
        return keylet::perpExchange(owner_.id(), SeqProxy::rawSequence(1));
    }

    /**
     * An ltPERP_EXCHANGE entry that is not yet in any view.
     */
    [[nodiscard]] std::shared_ptr<SLE>
    makeExchange() const
    {
        auto sle = std::make_shared<SLE>(exchangeKeylet());
        (*sle)[sfOwner] = owner_.id();
        (*sle)[sfLiquidationPenalty] = 1'000;
        (*sle)[sfLiquidationChunkMin] = Number{10'000};
        return sle;
    }

    /**
     * An ltPERP_MARKET entry on XRP/<quote> reading document 1 of each oracle owner, with the
     * tick, lot, cap and band of the default jtx market. It is not yet in any view.
     */
    [[nodiscard]] std::shared_ptr<SLE>
    makeMarket(MarketParams const& params) const
    {
        auto const quote = toCurrency(params.quoteAsset);
        auto sle =
            std::make_shared<SLE>(keylet::perpMarket(exchangeKeylet().key, xrpCurrency(), quote));
        (*sle)[sfExchangeID] = exchangeKeylet().key;
        (*sle)[sfBaseAsset] = xrpCurrency();
        (*sle)[sfQuoteAsset] = quote;

        STArray oracles{sfIndexOracles};
        for (auto const& owner : params.oracles)
        {
            STObject entry = STObject::makeInnerObject(sfIndexOracle);
            entry[sfOwner] = owner;
            entry[sfOracleDocumentID] = 1;
            oracles.push_back(std::move(entry));
        }
        sle->setFieldArray(sfIndexOracles, oracles);

        STArray tiers{sfMarginTiers};
        STObject tier = STObject::makeInnerObject(sfMarginTier);
        tier[sfNotionalMax] = Number{1'000'000};
        tier[sfInitialMarginRate] = 10'000;
        tier[sfMaintenanceMarginRate] = 5'000;
        tiers.push_back(std::move(tier));
        sle->setFieldArray(sfMarginTiers, tiers);

        (*sle)[sfIndexMaxLedgers] = params.indexMaxLedgers;
        (*sle)[sfPriceTick] = Number{1, -4};
        (*sle)[sfLotSize] = Number{1};
        (*sle)[sfOpenInterestMax] = Number{1'000'000};
        (*sle)[sfOrderPriceBand] = 10'000;
        return sle;
    }

    /**
     * Unix time of the view's parent close.
     */
    [[nodiscard]] static std::int64_t
    unixParentClose(ReadView const& view)
    {
        return view.parentCloseTime().time_since_epoch().count() + kEpochOffset.count();
    }

    /**
     * Creates document 1 of owner quoting the given XRP/USD price in tenths; jtx closes the ledger.
     */
    static Oracle
    makeOracle(test::jtx::Env& env, AccountID const& owner, std::uint32_t tenths)
    {
        return Oracle{
            env,
            test::jtx::oracle::CreateArg{
                .owner = owner, .documentID = 1, .series = {{"XRP", "USD", tenths, 1}}}};
    }

    [[nodiscard]] static std::optional<Number>
    firstQuote(ReadView const& view, SLE const& market)
    {
        return perp::freshQuote(view, market, market.getFieldArray(sfIndexOracles)[0]);
    }

    void
    testFreshByLedger()
    {
        testcase("fresh by ledger");
        using namespace test::jtx;
        Env env{*this, all_};
        fundAccounts(env);
        auto const oracle = makeOracle(env, o1_.id(), 20);
        env.close();
        env.close();

        auto const oracleSle = env.le(keylet::oracle(o1_.id(), oracle.documentID()));
        if (!BEAST_EXPECT(oracleSle))
            return;
        BEAST_EXPECT(env.current()->seq() - (*oracleSle)[sfPreviousTxnLgrSeq] == 3);

        auto const tooTight = makeMarket({.oracles = {o1_.id()}, .indexMaxLedgers = 2});
        BEAST_EXPECT(!firstQuote(*env.current(), *tooTight));

        auto const exact = makeMarket({.oracles = {o1_.id()}, .indexMaxLedgers = 3});
        BEAST_EXPECT(firstQuote(*env.current(), *exact) == Number(2));
    }

    void
    testFreshByTime()
    {
        testcase("fresh by time");
        using namespace test::jtx;
        Env env{*this, all_};
        fundAccounts(env);
        auto const oracle = makeOracle(env, o1_.id(), 20);
        env.close();

        auto const oracleKeylet = keylet::oracle(o1_.id(), oracle.documentID());
        auto const market = makeMarket({.oracles = {o1_.id()}, .indexMaxLedgers = 100});
        auto const quoteAt = [&](std::int64_t offset) {
            Sandbox sb{env.current().get(), TapNone};
            auto const sle = sb.peek(oracleKeylet);
            if (!BEAST_EXPECT(sle))
                return std::optional<Number>{};
            (*sle)[sfLastUpdateTime] = static_cast<std::uint32_t>(unixParentClose(sb) + offset);
            sb.update(sle);
            return firstQuote(sb, *market);
        };

        // A quote is fresh up to kIndexMaxAge seconds old, measured from the parent close.
        auto const maxAge = static_cast<std::int64_t>(perp::kIndexMaxAge.count());
        BEAST_EXPECT(maxAge == static_cast<std::int64_t>(kMaxLastUpdateTimeDelta));
        BEAST_EXPECT(quoteAt(-maxAge) == Number(2));
        BEAST_EXPECT(!quoteAt(-maxAge - 1));

        // A provider timestamp ahead of the close gives a negative age, which is fresh.
        BEAST_EXPECT(quoteAt(200) == Number(2));
    }

    void
    testMissingPair()
    {
        testcase("missing pair");
        using namespace test::jtx;
        Env env{*this, all_};
        fundAccounts(env);
        Oracle oracle{
            env,
            oracle::CreateArg{
                .owner = o1_.id(),
                .documentID = 1,
                .series = {{"XRP", "USD", 20, 1}, {"XRP", "EUR", 30, 1}}}};
        env.close();

        auto const usd = makeMarket({.oracles = {o1_.id()}});
        auto const eur = makeMarket({.oracles = {o1_.id()}, .quoteAsset = "EUR"});
        BEAST_EXPECT(firstQuote(*env.current(), *usd) == Number(2));
        BEAST_EXPECT(firstQuote(*env.current(), *eur) == Number(3));

        oracle.set(oracle::UpdateArg{.series = {{"XRP", "EUR", 31, 1}}});
        BEAST_EXPECT(!firstQuote(*env.current(), *usd));
        BEAST_EXPECT(firstQuote(*env.current(), *eur) == Number(31, -1));
    }

    void
    testIndexMedian()
    {
        testcase("index median");
        using namespace test::jtx;
        Env env{*this, all_};
        fundAccounts(env);
        Account const o4{"o4"};
        env.fund(XRP(1'000), o4);
        env.close();
        makeOracle(env, o1_.id(), 20);
        makeOracle(env, o2_.id(), 22);
        makeOracle(env, o3_.id(), 25);
        makeOracle(env, o4.id(), 24);
        env.close();

        auto const odd = makeMarket({.oracles = {o1_.id(), o2_.id(), o3_.id()}});
        auto const oddIndex = perp::computeIndex(*env.current(), *odd);
        BEAST_EXPECT(oddIndex && *oddIndex == Number(22, -1));

        auto const even = makeMarket({.oracles = {o1_.id(), o2_.id(), o3_.id(), o4.id()}});
        auto const evenIndex = perp::computeIndex(*env.current(), *even);
        BEAST_EXPECT(evenIndex && *evenIndex == Number(23, -1));
    }

    void
    testQuorum()
    {
        testcase("quorum");
        using namespace test::jtx;
        Env env{*this, all_};
        fundAccounts(env);
        auto q1 = makeOracle(env, o1_.id(), 20);
        auto q2 = makeOracle(env, o2_.id(), 20);
        makeOracle(env, o3_.id(), 20);
        for (int i = 0; i < 4; ++i)
            env.close();

        MarketParams const params{.oracles = {o1_.id(), o2_.id(), o3_.id()}, .indexMaxLedgers = 2};

        q1.set(oracle::UpdateArg{.series = {{"XRP", "USD", 21, 1}}});
        {
            auto const market = makeMarket(params);
            auto const index = perp::computeIndex(*env.current(), *market);
            BEAST_EXPECT(!index && index.error() == tecPERP_STALE_PRICE);

            Sandbox sb{env.current().get(), TapNone};
            auto const exchange = makeExchange();
            sb.insert(exchange);
            (*market)[sfFundingRate] = Number{8, -4};
            sb.insert(market);
            perp::refreshMarket(sb, market, exchange, env.journal);
            auto const t = static_cast<std::uint32_t>(unixParentClose(sb));
            BEAST_EXPECT((*market)[sfMarketStatus] == perp::kMarketStale);
            BEAST_EXPECT((*market)[sfStatusTime] == t);
            BEAST_EXPECT((*market)[sfFundingRate] == Number(0));
            BEAST_EXPECT((*market)[sfRefreshLedger] == sb.seq());

            // A market already Stale keeps the time it turned Stale.
            (*market)[sfStatusTime] = 1;
            (*market)[sfRefreshLedger] = 0;
            sb.update(market);
            perp::refreshMarket(sb, market, exchange, env.journal);
            BEAST_EXPECT((*market)[sfStatusTime] == 1);
        }

        q2.set(oracle::UpdateArg{.series = {{"XRP", "USD", 23, 1}}});
        {
            auto const market = makeMarket(params);
            auto const index = perp::computeIndex(*env.current(), *market);
            BEAST_EXPECT(index && *index == Number(22, -1));

            Sandbox sb{env.current().get(), TapNone};
            auto const exchange = makeExchange();
            sb.insert(exchange);
            (*market)[sfMarketStatus] = perp::kMarketStale;
            (*market)[sfStatusTime] = 1;
            sb.insert(market);
            perp::refreshMarket(sb, market, exchange, env.journal);
            BEAST_EXPECT((*market)[sfMarketStatus] == perp::kMarketLive);
            BEAST_EXPECT(
                (*market)[sfStatusTime] == static_cast<std::uint32_t>(unixParentClose(sb)));
            BEAST_EXPECT((*market)[sfIndexPrice] == Number(22, -1));
        }
    }

    void
    testBasisEma()
    {
        testcase("basis EMA");
        // Window 150 s: alpha = min(elapsed, 75) / 150, at most one half.
        BEAST_EXPECT(perp::kBasisWindow.count() == 150);
        perp::BasisInput const base{
            .index = Number{2},
            .basisEMA = Number{0},
            .elapsed = 75,
            .bid = Number{2'095, -3},
            .ask = Number{2'105, -3},
            .orderPriceBand = 10'000};

        // Mid 2.1 against index 2.0 samples a basis of 0.1, taken half at most.
        BEAST_EXPECT(perp::computeBasisEMA(base) == Number(5, -2));
        auto idle = base;
        idle.elapsed = 36'000;
        BEAST_EXPECT(perp::computeBasisEMA(idle) == Number(5, -2));
        auto partial = base;
        partial.elapsed = 30;
        BEAST_EXPECT(perp::computeBasisEMA(partial) == Number(2, -2));
        auto none = base;
        none.elapsed = 0;
        BEAST_EXPECT(perp::computeBasisEMA(none) == Number(0));

        // The EMA moves from its previous value toward the sample.
        auto previous = base;
        previous.basisEMA = Number{3, -1};
        BEAST_EXPECT(perp::computeBasisEMA(previous) == Number(2, -1));
    }

    void
    testMidSpreadFilter()
    {
        testcase("mid spread filter");
        // A mid counts only when the spread is within half the 10 % order band.
        perp::BasisInput const wide{
            .index = Number{2},
            .basisEMA = Number{2, -1},
            .elapsed = 75,
            .bid = Number{2},
            .ask = Number{23, -1},
            .orderPriceBand = 10'000};

        // Without a valid mid the sample is zero, so the EMA decays toward it.
        BEAST_EXPECT(perp::computeBasisEMA(wide) == Number(1, -1));

        // A spread of exactly half the band counts: mid 2.0 samples zero basis too.
        auto edge = wide;
        edge.bid = Number{195, -2};
        edge.ask = Number{205, -2};
        BEAST_EXPECT(perp::computeBasisEMA(edge) == Number(1, -1));
        auto inside = wide;
        inside.bid = Number{205, -2};
        inside.ask = Number{215, -2};
        BEAST_EXPECT(perp::computeBasisEMA(inside) == Number(15, -2));

        auto crossed = wide;
        crossed.bid = Number{2'105, -3};
        crossed.ask = Number{2'095, -3};
        BEAST_EXPECT(perp::computeBasisEMA(crossed) == Number(1, -1));

        auto oneSided = wide;
        oneSided.ask = std::nullopt;
        BEAST_EXPECT(perp::computeBasisEMA(oneSided) == Number(1, -1));
    }

    void
    testFundingRate()
    {
        testcase("funding rate");
        BEAST_EXPECT(perp::kFundingInterestRate == 10);
        BEAST_EXPECT(perp::kFundingCap == 4'000);

        // P 0.001 and interest 0.0001: 0.001 + clamp(-0.0009, +-0.0005) = 0.0005.
        BEAST_EXPECT(perp::computeFundingRate(Number{2, -3}, Number{2}) == Number(5, -4));

        // P 0.0001 equal to the interest rate adds nothing.
        BEAST_EXPECT(perp::computeFundingRate(Number{2, -4}, Number{2}) == Number(1, -4));

        // A flat book pays the interest rate.
        BEAST_EXPECT(perp::computeFundingRate(Number{0}, Number{2}) == Number(1, -4));

        // P -0.001: -0.001 + clamp(0.0011, +-0.0005) = -0.0005.
        BEAST_EXPECT(perp::computeFundingRate(Number{-2, -3}, Number{2}) == Number(-5, -4));

        // P +-0.1 is capped at +-4 %.
        BEAST_EXPECT(perp::computeFundingRate(Number{2, -1}, Number{2}) == Number(4, -2));
        BEAST_EXPECT(perp::computeFundingRate(Number{-2, -1}, Number{2}) == Number(-4, -2));

        // Without an index there is no rate.
        BEAST_EXPECT(perp::computeFundingRate(Number{2, -1}, Number{0}) == Number(0));
    }

    void
    testFundingAccrual()
    {
        testcase("funding accrual");
        using namespace test::jtx;
        BEAST_EXPECT(perp::fundingAccrual(Number{8, -4}, Number{2}, 3'600) == Number(2, -4));
        BEAST_EXPECT(perp::fundingAccrual(Number{8, -4}, Number{2}, 40'000) == Number(16, -4));
        BEAST_EXPECT(
            perp::fundingAccrual(Number{8, -4}, Number{2}, 40'000) ==
            perp::fundingAccrual(Number{8, -4}, Number{2}, 28'800));

        Env env{*this, all_};
        auto const world = setupMarketWorld(env);
        BEAST_EXPECT(world.oracles.size() == 3);
        MarketParams const params{.oracles = {o1_.id(), o2_.id(), o3_.id()}};
        auto const t = static_cast<std::uint32_t>(unixParentClose(*env.current()));

        for (auto const status : {perp::kMarketLive, perp::kMarketStale})
        {
            Sandbox sb{env.current().get(), TapNone};
            auto const exchange = makeExchange();
            sb.insert(exchange);
            auto const market = makeMarket(params);
            (*market)[sfMarketStatus] = status;
            (*market)[sfFundingRate] = Number{8, -4};
            (*market)[sfIndexPrice] = Number{2};
            (*market)[sfCumulativeFunding] = Number{1, -4};
            (*market)[sfLastRefreshTime] = t - 3'600;
            sb.insert(market);
            perp::refreshMarket(sb, market, exchange, env.journal);
            auto const expected = status == perp::kMarketLive ? Number{3, -4} : Number{1, -4};
            BEAST_EXPECT((*market)[sfCumulativeFunding] == expected);
            BEAST_EXPECT((*market)[sfLastRefreshTime] == t);
            BEAST_EXPECT((*market)[sfMarketStatus] == perp::kMarketLive);
        }
    }

    void
    testRefreshOncePerLedger()
    {
        testcase("refresh once per ledger");
        using namespace test::jtx;
        Env env{*this, all_};
        setupMarketWorld(env);
        auto const t = static_cast<std::uint32_t>(unixParentClose(*env.current()));

        Sandbox sb{env.current().get(), TapNone};
        auto const exchange = makeExchange();
        sb.insert(exchange);
        auto const market = makeMarket({.oracles = {o1_.id(), o2_.id(), o3_.id()}});
        sb.insert(market);

        perp::refreshMarket(sb, market, exchange, env.journal);
        BEAST_EXPECT((*market)[sfRefreshLedger] == sb.seq());
        BEAST_EXPECT((*market)[sfLastRefreshTime] == t);
        BEAST_EXPECT((*market)[sfIndexPrice] == Number(2));
        BEAST_EXPECT((*market)[sfBasisEMA] == Number(0));
        BEAST_EXPECT((*market)[sfFundingRate] == Number(1, -4));
        BEAST_EXPECT((*market)[sfMarketStatus] == perp::kMarketLive);

        // A second refresh in the same ledger changes nothing, even with funding due.
        (*market)[sfFundingRate] = Number{8, -4};
        (*market)[sfLastRefreshTime] = t - 3'600;
        sb.update(market);
        perp::refreshMarket(sb, market, exchange, env.journal);
        BEAST_EXPECT((*market)[sfCumulativeFunding] == Number(0));
        BEAST_EXPECT((*market)[sfLastRefreshTime] == t - 3'600);
        BEAST_EXPECT((*market)[sfFundingRate] == Number(8, -4));
    }

    void
    testRefreshOpenInterest()
    {
        testcase("refresh open interest");
        using namespace test::jtx;
        Env env{*this, all_};
        setupMarketWorld(env);

        Sandbox sb{env.current().get(), TapNone};
        auto const exchange = makeExchange();
        (*exchange)[sfOpenInterestNotional] = Number{19};
        sb.insert(exchange);
        auto const market = makeMarket({.oracles = {o1_.id(), o2_.id(), o3_.id()}});
        (*market)[sfIndexPrice] = Number{19, -1};
        (*market)[sfOpenInterestLong] = Number{10};
        sb.insert(market);

        // The index moves from 1.9 to 2.0, and the notional with it: 10 x 0.1 = 1.
        perp::refreshMarket(sb, market, exchange, env.journal);
        BEAST_EXPECT((*market)[sfIndexPrice] == Number(2));
        BEAST_EXPECT((*exchange)[sfOpenInterestNotional] == Number(20));
    }

    /**
     * An sfMarginTiers array of the given (notional bound, initial rate, maintenance rate) tiers.
     */
    [[nodiscard]] static STArray
    makeTiers(std::vector<std::tuple<Number, std::uint32_t, std::uint32_t>> const& tiers)
    {
        STArray array{sfMarginTiers};
        for (auto const& [notionalMax, initial, maintenance] : tiers)
        {
            STObject tier = STObject::makeInnerObject(sfMarginTier);
            tier[sfNotionalMax] = notionalMax;
            tier[sfInitialMarginRate] = initial;
            tier[sfMaintenanceMarginRate] = maintenance;
            array.push_back(std::move(tier));
        }
        return array;
    }

    void
    testTierTightenRule()
    {
        testcase("tier tighten rule");
        auto const current =
            makeTiers({{Number{100'000}, 10'000u, 5'000u}, {Number{1'000'000}, 20'000u, 10'000u}});

        // Unchanged, higher rates and lower bounds tighten.
        BEAST_EXPECT(perp::tiersTighten(current, current));
        BEAST_EXPECT(
            perp::tiersTighten(
                current,
                makeTiers(
                    {{Number{50'000}, 11'000u, 5'000u}, {Number{900'000}, 20'000u, 12'000u}})));

        // Any lower rate, any higher bound, or another tier count loosens.
        BEAST_EXPECT(!perp::tiersTighten(
            current,
            makeTiers({{Number{100'000}, 9'999u, 5'000u}, {Number{1'000'000}, 20'000u, 10'000u}})));
        BEAST_EXPECT(!perp::tiersTighten(
            current,
            makeTiers({{Number{100'000}, 10'000u, 5'000u}, {Number{1'000'000}, 20'000u, 9'999u}})));
        BEAST_EXPECT(!perp::tiersTighten(
            current,
            makeTiers(
                {{Number{100'001}, 10'000u, 5'000u}, {Number{1'000'000}, 20'000u, 10'000u}})));
        BEAST_EXPECT(!perp::tiersTighten(current, makeTiers({{Number{100'000}, 10'000u, 5'000u}})));
        BEAST_EXPECT(!perp::tiersTighten(
            current,
            makeTiers(
                {{Number{100'000}, 10'000u, 5'000u},
                 {Number{1'000'000}, 20'000u, 10'000u},
                 {Number{2'000'000}, 30'000u, 15'000u}})));

        // A tightened requirement is never lower at any notional both allow.
        auto const tighter =
            makeTiers({{Number{50'000}, 11'000u, 5'000u}, {Number{900'000}, 20'000u, 12'000u}});
        for (auto const& notional :
             {Number{10'000}, Number{50'000}, Number{75'000}, Number{100'000}, Number{900'000}})
        {
            for (bool const initial : {true, false})
            {
                auto const before = perp::tierRequirement(notional, current, initial);
                auto const after = perp::tierRequirement(notional, tighter, initial);
                BEAST_EXPECT(before && after && *after >= *before);
            }
        }
    }

    void
    testTierRequirement()
    {
        testcase("tier requirement");
        STArray tiers{sfMarginTiers};
        for (auto const& [notionalMax, initial, maintenance] :
             {std::tuple{Number{100'000}, 10'000u, 5'000u},
              std::tuple{Number{1'000'000}, 20'000u, 10'000u}})
        {
            STObject tier = STObject::makeInnerObject(sfMarginTier);
            tier[sfNotionalMax] = notionalMax;
            tier[sfInitialMarginRate] = initial;
            tier[sfMaintenanceMarginRate] = maintenance;
            tiers.push_back(std::move(tier));
        }

        BEAST_EXPECT(perp::tierRequirement(Number{50'000}, tiers, true) == Number(5'000));
        BEAST_EXPECT(perp::tierRequirement(Number{100'000}, tiers, true) == Number(10'000));
        // D2 = 100 000 x (0.2 - 0.1) = 10 000, so 200 000 x 0.2 - 10 000.
        BEAST_EXPECT(perp::tierRequirement(Number{200'000}, tiers, true) == Number(30'000));
        BEAST_EXPECT(perp::tierRequirement(Number{200'000}, tiers, false) == Number(15'000));
        BEAST_EXPECT(perp::tierRequirement(Number{0}, tiers, true) == Number(0));
        BEAST_EXPECT(!perp::tierRequirement(Number{1'000'001}, tiers, true));
        // Requirements round up: x times 0.2 needs one digit more than the mantissa holds.
        Number const inexact{5'000'006'666'666'666'667, -13};
        auto const rounded = perp::tierRequirement(inexact, tiers, true);
        BEAST_EXPECT(rounded && *rounded > (inexact * Number{2, -1}) - Number{10'000});
    }

public:
    void
    run() override
    {
        testFreshByLedger();
        testFreshByTime();
        testMissingPair();
        testIndexMedian();
        testQuorum();
        testBasisEma();
        testMidSpreadFilter();
        testFundingRate();
        testFundingAccrual();
        testRefreshOncePerLedger();
        testRefreshOpenInterest();
        testTierRequirement();
        testTierTightenRule();
    }
};

BEAST_DEFINE_TESTSUITE(PerpPrice, perp, xrpl);

}  // namespace xrpl

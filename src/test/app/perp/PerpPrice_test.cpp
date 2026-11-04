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
        std::uint32_t indexMaxAge{300};
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
        (*sle)[sfParameterDelay] = 86'400;
        return sle;
    }

    /**
     * An ltPERP_MARKET entry on XRP/<quote> reading document 1 of each oracle owner, with the
     * band, EMA and funding parameters of the default jtx market. It is not yet in any view.
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

        (*sle)[sfIndexMaxAge] = params.indexMaxAge;
        (*sle)[sfIndexMaxLedgers] = params.indexMaxLedgers;
        (*sle)[sfPriceTick] = Number{1, -4};
        (*sle)[sfLotSize] = Number{1};
        (*sle)[sfOpenInterestMax] = Number{1'000'000};
        (*sle)[sfOrderPriceBand] = 10'000;
        (*sle)[sfMidSpreadMax] = 1'000;
        (*sle)[sfMarkBand] = 5'000;
        (*sle)[sfMarkMoveMax] = 10'000;
        (*sle)[sfBasisWindow] = 3'600;
        (*sle)[sfFundingCap] = 4'000;
        (*sle)[sfLiquidationPenalty] = 1'000;
        (*sle)[sfLiquidationRewardShare] = 50'000;
        (*sle)[sfLiquidationChunkMin] = Number{10'000};
        (*sle)[sfSettleStaleAfter] = 3'600;
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
        env.close();

        auto const oracleKeylet = keylet::oracle(o1_.id(), oracle.documentID());
        auto const oracleSle = env.le(oracleKeylet);
        if (!BEAST_EXPECT(oracleSle))
            return;
        auto const age = unixParentClose(*env.current()) - (*oracleSle)[sfLastUpdateTime];
        BEAST_EXPECT(age > 0);

        auto const stale = makeMarket(
            {.oracles = {o1_.id()},
             .indexMaxAge = static_cast<std::uint32_t>(age - 1),
             .indexMaxLedgers = 100});
        BEAST_EXPECT(!firstQuote(*env.current(), *stale));

        auto const fresh = makeMarket(
            {.oracles = {o1_.id()},
             .indexMaxAge = static_cast<std::uint32_t>(age),
             .indexMaxLedgers = 100});
        BEAST_EXPECT(firstQuote(*env.current(), *fresh) == Number(2));

        // A provider timestamp ahead of the close gives a negative age, which is fresh.
        Sandbox sb{env.current().get(), TapNone};
        auto const future = sb.peek(oracleKeylet);
        if (!BEAST_EXPECT(future))
            return;
        (*future)[sfLastUpdateTime] = static_cast<std::uint32_t>(unixParentClose(sb) + 200);
        sb.update(future);
        auto const tight =
            makeMarket({.oracles = {o1_.id()}, .indexMaxAge = 1, .indexMaxLedgers = 100});
        BEAST_EXPECT(firstQuote(sb, *tight) == Number(2));
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
            BEAST_EXPECT((*market)[~sfStaleSince] == t);
            BEAST_EXPECT((*market)[sfFundingRate] == Number(0));
            BEAST_EXPECT((*market)[sfRefreshLedger] == sb.seq());
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
            (*market)[sfStaleSince] = 1;
            sb.insert(market);
            perp::refreshMarket(sb, market, exchange, env.journal);
            BEAST_EXPECT((*market)[sfMarketStatus] == perp::kMarketLive);
            BEAST_EXPECT(!market->isFieldPresent(sfStaleSince));
            BEAST_EXPECT((*market)[sfIndexPrice] == Number(22, -1));
            BEAST_EXPECT((*market)[sfMarkPrice] == Number(22, -1));
        }
    }

    void
    testMarkClamps()
    {
        testcase("mark clamps");
        perp::MarkInput const base{
            .index = Number{2},
            .prevMark = Number{2},
            .basisEMA = Number{0},
            .elapsed = 1'800,
            .bid = Number{2'595, -3},
            .ask = Number{2'605, -3},
            .basisWindow = 3'600,
            .midSpreadMax = 1'000,
            .markMoveMax = 10'000,
            .markBand = 5'000};

        // EMA 0.3, raw median(2.0, 2.3, 2.6) = 2.3, moved 2.2, banded 2.1.
        auto const banded = perp::computeMark(base);
        BEAST_EXPECT(banded.basisEMA == Number(3, -1));
        BEAST_EXPECT(banded.mark == Number(21, -1));

        // Raw median(2.0, 2.2, 2.2) = 2.2 moves at most 1 % from 2.0.
        auto moveCapped = base;
        moveCapped.basisEMA = Number{2, -1};
        moveCapped.bid = Number{2'199, -3};
        moveCapped.ask = Number{2'201, -3};
        moveCapped.markMoveMax = 1'000;
        auto const moved = perp::computeMark(moveCapped);
        BEAST_EXPECT(moved.basisEMA == Number(2, -1));
        BEAST_EXPECT(moved.mark == Number(202, -2));

        // An index crash from 2.0 to 1.0 pulls the mark into the 5 % band at once.
        auto crash = base;
        crash.index = Number{1};
        crash.bid = std::nullopt;
        crash.ask = std::nullopt;
        auto const crashed = perp::computeMark(crash);
        BEAST_EXPECT(crashed.mark <= Number(105, -2));
        BEAST_EXPECT(crashed.mark == Number(105, -2));

        // The first refresh of a market has no previous mark to cap the move from.
        auto first = base;
        first.prevMark = Number{0};
        first.bid = std::nullopt;
        first.ask = std::nullopt;
        BEAST_EXPECT(perp::computeMark(first).mark == Number(2));
    }

    void
    testEmaHalfCap()
    {
        testcase("EMA half cap");
        perp::MarkInput const base{
            .index = Number{2},
            .prevMark = Number{2},
            .basisEMA = Number{0},
            .elapsed = 36'000,
            .bid = Number{2'595, -3},
            .ask = Number{2'605, -3},
            .basisWindow = 3'600,
            .midSpreadMax = 1'000,
            .markMoveMax = 10'000,
            .markBand = 5'000};
        BEAST_EXPECT(perp::computeMark(base).basisEMA == Number(3, -1));

        auto quarter = base;
        quarter.elapsed = 900;
        BEAST_EXPECT(perp::computeMark(quarter).basisEMA == Number(15, -2));

        auto none = base;
        none.elapsed = 0;
        BEAST_EXPECT(perp::computeMark(none).basisEMA == Number(0));
    }

    void
    testMidSpreadFilter()
    {
        testcase("mid spread filter");
        perp::MarkInput const wide{
            .index = Number{2},
            .prevMark = Number{2},
            .basisEMA = Number{0},
            .elapsed = 1'800,
            .bid = Number{25, -1},
            .ask = Number{27, -1},
            .basisWindow = 3'600,
            .midSpreadMax = 1'000,
            .markMoveMax = 10'000,
            .markBand = 5'000};
        auto const result = perp::computeMark(wide);
        BEAST_EXPECT(result.basisEMA == Number(0));
        BEAST_EXPECT(result.mark == Number(2));

        auto crossed = wide;
        crossed.bid = Number{2'605, -3};
        crossed.ask = Number{2'595, -3};
        BEAST_EXPECT(perp::computeMark(crossed).basisEMA == Number(0));

        auto oneSided = wide;
        oneSided.ask = std::nullopt;
        BEAST_EXPECT(perp::computeMark(oneSided).basisEMA == Number(0));
    }

    void
    testFundingRate()
    {
        testcase("funding rate");
        // P 0.001 and interest 0.0001: 0.001 + clamp(-0.0009, +-0.0005) = 0.0005.
        BEAST_EXPECT(
            perp::computeFundingRate(Number{2'002, -3}, Number{2}, 10, 4'000) == Number(5, -4));

        // P 0.0001 equal to the interest rate adds nothing.
        BEAST_EXPECT(
            perp::computeFundingRate(Number{20'002, -4}, Number{2}, 10, 4'000) == Number(1, -4));

        // P +-0.1 is capped at +-4 %.
        BEAST_EXPECT(
            perp::computeFundingRate(Number{22, -1}, Number{2}, 0, 4'000) == Number(4, -2));
        BEAST_EXPECT(
            perp::computeFundingRate(Number{18, -1}, Number{2}, 0, 4'000) == Number(-4, -2));

        // A negative interest rate pulls a flat market's rate down by at most the clamp.
        BEAST_EXPECT(perp::computeFundingRate(Number{2}, Number{2}, -100, 4'000) == Number(-5, -4));
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
            (*market)[sfMarkPrice] = Number{2};
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
        BEAST_EXPECT((*market)[sfMarkPrice] == Number(2));
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
        (*market)[sfMarkPrice] = Number{19, -1};
        (*market)[sfOpenInterestLong] = Number{10};
        sb.insert(market);

        // Mark 1.9 moves to the index 2.0 within the 10 % move cap and the 5 % band.
        perp::refreshMarket(sb, market, exchange, env.journal);
        BEAST_EXPECT((*market)[sfMarkPrice] == Number(2));
        BEAST_EXPECT((*exchange)[sfOpenInterestNotional] == Number(20));
    }

    void
    testPendingParameters()
    {
        testcase("pending parameters");
        using namespace test::jtx;
        Env env{*this, all_};
        setupMarketWorld(env);

        auto const market = makeMarket({.oracles = {o1_.id(), o2_.id(), o3_.id()}});
        (*market)[sfFundingInterestRate] = 10;
        STObject pending = STObject::makeInnerObject(sfPendingParameters);
        pending[sfMarkBand] = 6'000;
        pending[sfFundingInterestRate] = 0;
        market->setFieldObject(sfPendingParameters, pending);
        (*market)[sfParameterEffectiveTime] = 1'000;

        BEAST_EXPECT(!perp::applyPendingParameters(*market, 999));
        BEAST_EXPECT((*market)[sfMarkBand] == 5'000);
        BEAST_EXPECT(market->isFieldPresent(sfPendingParameters));

        BEAST_EXPECT(perp::applyPendingParameters(*market, 1'000));
        BEAST_EXPECT((*market)[sfMarkBand] == 6'000);
        BEAST_EXPECT((*market)[sfFundingInterestRate] == 0);
        BEAST_EXPECT(!market->isFieldPresent(sfFundingInterestRate));
        BEAST_EXPECT(!market->isFieldPresent(sfPendingParameters));
        BEAST_EXPECT(!market->isFieldPresent(sfParameterEffectiveTime));
        BEAST_EXPECT(!perp::applyPendingParameters(*market, 2'000));

        // A refresh at or after the effective time applies the market's and the exchange's.
        auto const t = static_cast<std::uint32_t>(unixParentClose(*env.current()));
        Sandbox sb{env.current().get(), TapNone};
        auto const exchange = makeExchange();
        STObject exchangePending = STObject::makeInnerObject(sfPendingParameters);
        exchangePending[sfTakerFee] = 100;
        exchange->setFieldObject(sfPendingParameters, exchangePending);
        (*exchange)[sfParameterEffectiveTime] = t;
        sb.insert(exchange);
        auto const refreshed = makeMarket({.oracles = {o1_.id(), o2_.id(), o3_.id()}});
        STObject marketPending = STObject::makeInnerObject(sfPendingParameters);
        marketPending[sfMarkBand] = 7'000;
        refreshed->setFieldObject(sfPendingParameters, marketPending);
        (*refreshed)[sfParameterEffectiveTime] = t;
        sb.insert(refreshed);
        perp::refreshMarket(sb, refreshed, exchange, env.journal);
        BEAST_EXPECT((*refreshed)[sfMarkBand] == 7'000);
        BEAST_EXPECT((*exchange)[sfTakerFee] == 100);
        BEAST_EXPECT(!exchange->isFieldPresent(sfPendingParameters));
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
        testMarkClamps();
        testEmaHalfCap();
        testMidSpreadFilter();
        testFundingRate();
        testFundingAccrual();
        testRefreshOncePerLedger();
        testRefreshOpenInterest();
        testPendingParameters();
        testTierRequirement();
    }
};

BEAST_DEFINE_TESTSUITE(PerpPrice, perp, xrpl);

}  // namespace xrpl

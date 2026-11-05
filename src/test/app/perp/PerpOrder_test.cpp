#include <test/app/perp/PerpTestBase.h>
#include <test/jtx/Env.h>
#include <test/jtx/Oracle.h>
#include <test/jtx/amount.h>
#include <test/jtx/pay.h>
#include <test/jtx/perp.h>
#include <test/jtx/ter.h>
#include <test/jtx/txflags.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STVector256.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

namespace xrpl {

class PerpOrder_test : public PerpTestBase
{
    using Perp = test::jtx::Perp;

    /**
     * A USD exchange with one Live market.
     */
    struct Trading
    {
        World world;
        Keylet exchange;
        Keylet market;
    };

    /**
     * Sets up the oracles quoting series, a USD exchange with the extra creation fields that
     * exchangeFields adds, and the XRP/USD market that marketFields adjusts; alice, bob and carol
     * each deposit 1000 USD. Closes the ledger.
     */
    Trading
    setupTrading(
        Env& env,
        std::function<void(json::Value&)> const& exchangeFields = {},
        std::function<void(Perp::MarketArgs&)> const& marketFields = {},
        test::jtx::oracle::DataSeries const& series = {{"XRP", "USD", 20, 1}})
    {
        using namespace test::jtx;
        auto world = setupMarketWorld(env, series);
        auto const exchange = Perp::exchangeKeylet(env, owner_);
        auto create = Perp::exchangeSet(owner_, usd_);
        if (exchangeFields)
            exchangeFields(create);
        env(create);
        env.close();

        auto args = liveArgs();
        if (marketFields)
            marketFields(args);
        env(Perp::marketSet(owner_, exchange.key, args));
        env.close();

        for (auto const* trader : {&alice_, &bob_, &carol_})
            env(Perp::deposit(*trader, exchange.key, usd_(1'000)));
        env.close();
        return {
            .world = std::move(world),
            .exchange = exchange,
            .market = marketKeylet(exchange.key, args.baseAsset)};
    }

    /**
     * The fixture's market arguments with a ledger bound wide enough that no test goes stale by
     * ledgers; quotes still age out after perp::kIndexMaxAge.
     */
    [[nodiscard]] Perp::MarketArgs
    liveArgs() const
    {
        auto args = marketArgs();
        args.indexMaxLedgers = 1'000;
        return args;
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

    [[nodiscard]] static Keylet
    marginKeylet(Account const& account, Keylet const& exchange)
    {
        return keylet::marginAccount(account.id(), exchange.key);
    }

    [[nodiscard]] static Keylet
    positionKeylet(Account const& account, Trading const& t)
    {
        return keylet::marginPosition(marginKeylet(account, t.exchange).key, t.market.key);
    }

    /**
     * The size of an account's position, zero when it has none.
     */
    [[nodiscard]] static Number
    sizeOf(Env& env, Account const& account, Trading const& t)
    {
        auto const sle = env.le(positionKeylet(account, t));
        return sle ? Number((*sle)[sfSize]) : Number();
    }

    [[nodiscard]] static Number
    cashOf(Env& env, Account const& account, Keylet const& exchange)
    {
        auto const sle = env.le(marginKeylet(account, exchange));
        return sle ? Number((*sle)[sfCash]) : Number(-1);
    }

    /**
     * The exchange pseudo-account's balance of the collateral asset.
     */
    static Number
    pseudoBalance(Env& env, SLE const& exchange)
    {
        return accountHolds(
            *env.current(),
            exchange[sfAccount],
            Asset{exchange[sfAsset]},
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            env.journal);
    }

    /**
     * Checks invariants 1 and 2 of the spec, and that the exchange's open-interest notional is
     * its single market's long open interest at the index.
     */
    void
    expectAccounting(Env& env, Trading const& t)
    {
        auto const sle = env.le(t.exchange);
        auto const market = env.le(t.market);
        if (!BEAST_EXPECT(sle && market))
            return;
        Number const assets = (*sle)[sfAssetsTotal];
        BEAST_EXPECT(pseudoBalance(env, *sle) == assets);
        BEAST_EXPECT(
            assets ==
            (*sle)[sfCashTotal] + (*sle)[sfInsuranceFund] + (*sle)[sfCoverAvailable] +
                (*sle)[sfOwnerFees] + (*sle)[sfSettlementBalance] - (*sle)[sfBadDebt]);
        BEAST_EXPECT(
            (*sle)[sfOpenInterestNotional] ==
            (*market)[sfOpenInterestLong] * (*market)[sfIndexPrice]);
    }

    /**
     * Moves amount of an account's cash to the insurance fund in the open ledger, so that the
     * account's equity falls while the exchange's accounting identity still holds.
     */
    void
    seedCashToInsurance(
        Env& env,
        Account const& account,
        Keylet const& exchange,
        Number const& amount)
    {
        seed(env, marginKeylet(account, exchange), [&](ApplyView& view, SLE::pointer const& sle) {
            (*sle)[sfCash] = (*sle)[sfCash] - amount;
            auto const ex = view.peek(exchange);
            if (!BEAST_EXPECT(ex))
                return;
            (*ex)[sfCashTotal] = (*ex)[sfCashTotal] - amount;
            (*ex)[sfInsuranceFund] = (*ex)[sfInsuranceFund] + amount;
            view.update(ex);
        });
    }

    void
    testRestAndCancel()
    {
        testcase("rest and cancel");
        using namespace test::jtx;
        Env env{*this, all_};
        auto const t = setupTrading(env);
        Number const price{19, -1};

        auto const ownerCount = env.ownerCount(alice_);
        auto const first = Perp::orderKeylet(env, alice_);
        auto const firstSeq = env.seq(alice_);
        env(order(alice_, t.market, price, Number(10)));
        env.close();
        {
            auto const sle = env.le(first);
            if (!BEAST_EXPECT(sle))
                return;
            BEAST_EXPECT((*sle)[sfAccount] == alice_.id());
            BEAST_EXPECT((*sle)[sfSequence] == firstSeq);
            BEAST_EXPECT((*sle)[sfMarginAccountID] == marginKeylet(alice_, t.exchange).key);
            BEAST_EXPECT((*sle)[sfMarketID] == t.market.key);
            BEAST_EXPECT((*sle)[sfLimitPrice] == price);
            BEAST_EXPECT((*sle)[sfSize] == Number(10));
            BEAST_EXPECT(!sle->isFlag(lsfPerpSell));

            // A bid sits at the quality of 1/price, and its first page records that quality.
            auto const quality = getRate(STAmount{noIssue(), price}, STAmount{noIssue(), 1});
            auto const dirKey = keylet::quality(keylet::perpBook(t.market.key, false), quality);
            BEAST_EXPECT((*sle)[sfBookDirectory] == dirKey.key);
            auto const page = env.le(dirKey);
            if (BEAST_EXPECT(page))
            {
                BEAST_EXPECT((*page)[~sfExchangeRate] == quality);
                BEAST_EXPECT(getQuality(page->key()) == quality);
            }

            BEAST_EXPECT(env.ownerCount(alice_) == ownerCount + 2);
            auto const position = env.le(positionKeylet(alice_, t));
            if (BEAST_EXPECT(position))
            {
                BEAST_EXPECT((*position)[sfSize] == Number());
                BEAST_EXPECT((*position)[sfOpenBuy] == Number(10));
                BEAST_EXPECT((*position)[sfOpenSell] == Number());
                BEAST_EXPECT((*position)[sfOrderCount] == 1);
            }
            auto const market = env.le(t.market);
            BEAST_EXPECT(market && (*market)[sfOrderCount] == 1);
            auto const margin = env.le(marginKeylet(alice_, t.exchange));
            BEAST_EXPECT(
                margin && margin->getFieldV256(sfMarkets).size() == 1 &&
                margin->getFieldV256(sfMarkets)[0] == t.market.key);
        }

        // A second bid at the same price shares the price level; a sell is counted apart.
        auto const second = Perp::orderKeylet(env, alice_);
        auto const secondSeq = env.seq(alice_);
        env(order(alice_, t.market, price, Number(5)));
        auto const sell = Perp::orderKeylet(env, alice_);
        auto const sellSeq = env.seq(alice_);
        env(order(alice_, t.market, Number(21, -1), Number(3), tfPerpSell));
        env.close();
        {
            auto const a = env.le(first);
            auto const b = env.le(second);
            BEAST_EXPECT(a && b && (*a)[sfBookDirectory] == (*b)[sfBookDirectory]);
            auto const s = env.le(sell);
            BEAST_EXPECT(s && s->isFlag(lsfPerpSell));
            auto const position = env.le(positionKeylet(alice_, t));
            BEAST_EXPECT(position && (*position)[sfOpenBuy] == Number(15));
            BEAST_EXPECT(position && (*position)[sfOpenSell] == Number(3));
            BEAST_EXPECT(position && (*position)[sfOrderCount] == 3);
            BEAST_EXPECT(env.ownerCount(alice_) == ownerCount + 4);
            auto const market = env.le(t.market);
            BEAST_EXPECT(market && (*market)[sfOrderCount] == 3);
        }

        // Only the owner may cancel a resting order in a Live market before it expires.
        auto byBob = Perp::cancel(bob_, firstSeq);
        byBob[sfOwner] = alice_.human();
        env(byBob, Ter(tecNO_PERMISSION));
        env(Perp::cancel(alice_, env.seq(alice_) + 10), Ter(tecNO_ENTRY));
        env.close();

        env(Perp::cancel(alice_, firstSeq));
        env.close();
        {
            BEAST_EXPECT(!env.le(first));
            auto const position = env.le(positionKeylet(alice_, t));
            BEAST_EXPECT(position && (*position)[sfOpenBuy] == Number(5));
            BEAST_EXPECT(position && (*position)[sfOrderCount] == 2);
            BEAST_EXPECT(env.ownerCount(alice_) == ownerCount + 3);
            auto const market = env.le(t.market);
            BEAST_EXPECT(market && (*market)[sfOrderCount] == 2);
        }

        // The last cancel deletes the empty position and takes it off sfMarkets.
        env(Perp::cancel(alice_, secondSeq));
        env(Perp::cancel(alice_, sellSeq));
        env.close();
        {
            BEAST_EXPECT(!env.le(second));
            BEAST_EXPECT(!env.le(sell));
            BEAST_EXPECT(!env.le(positionKeylet(alice_, t)));
            BEAST_EXPECT(env.ownerCount(alice_) == ownerCount);
            auto const market = env.le(t.market);
            BEAST_EXPECT(market && (*market)[sfOrderCount] == 0);
            auto const margin = env.le(marginKeylet(alice_, t.exchange));
            BEAST_EXPECT(margin && margin->getFieldV256(sfMarkets).empty());
        }

        // An order whose worst case needs more initial margin than the account's equity.
        env(order(alice_, t.market, Number(2), Number(6'000)), Ter(tecPERP_INSUFFICIENT_MARGIN));
        env(order(alice_, t.market, Number(2), Number(4'000)));
        env.close();

        // An account without a margin account on the exchange.
        env(order(liq_, t.market, Number(2), Number(1)), Ter(tecNO_ENTRY));
        expectAccounting(env, t);
    }

    void
    testPriceTimePriority()
    {
        testcase("price time priority");
        using namespace test::jtx;
        Env env{*this, all_};
        auto const t = setupTrading(env);

        auto const bobFirst = Perp::orderKeylet(env, bob_);
        env(order(bob_, t.market, Number(201, -2), Number(5), tfPerpSell));
        env.close();
        auto const carolAsk = Perp::orderKeylet(env, carol_);
        env(order(carol_, t.market, Number(201, -2), Number(5), tfPerpSell));
        env.close();
        auto const bobBetter = Perp::orderKeylet(env, bob_);
        env(order(bob_, t.market, Number(2), Number(5), tfPerpSell));
        env.close();

        // The better price fills first, at the maker's price.
        env(order(alice_, t.market, Number(202, -2), Number(5)));
        env.close();
        BEAST_EXPECT(!env.le(bobBetter));
        BEAST_EXPECT(env.le(bobFirst) && env.le(carolAsk));
        BEAST_EXPECT(sizeOf(env, alice_, t) == Number(5));
        {
            auto const position = env.le(positionKeylet(alice_, t));
            BEAST_EXPECT(position && (*position)[sfEntryNotional] == Number(10));
        }

        // At one price, the earlier order fills first.
        env(order(alice_, t.market, Number(202, -2), Number(5)));
        env.close();
        BEAST_EXPECT(!env.le(bobFirst));
        BEAST_EXPECT(env.le(carolAsk));
        BEAST_EXPECT(sizeOf(env, alice_, t) == Number(10));
        BEAST_EXPECT(sizeOf(env, bob_, t) == Number(-10));
        BEAST_EXPECT(sizeOf(env, carol_, t) == Number());
        {
            auto const position = env.le(positionKeylet(alice_, t));
            BEAST_EXPECT(position && (*position)[sfEntryNotional] == Number(2005, -2));
        }

        // A buy below the best ask rests without crossing.
        env(order(alice_, t.market, Number(2), Number(5)));
        env.close();
        BEAST_EXPECT(sizeOf(env, alice_, t) == Number(10));
        auto const carolOrder = env.le(carolAsk);
        BEAST_EXPECT(carolOrder && (*carolOrder)[sfSize] == Number(5));
        expectAccounting(env, t);
    }

    void
    testLimitPriceDigits()
    {
        testcase("limit price digits");
        using namespace test::jtx;
        Env env{*this, all_};
        auto const t =
            setupTrading(env, {}, [](Perp::MarketArgs& args) { args.priceTick = Number(1, -15); });

        // 17 significant digits do not fit a book quality key; 16 do.
        env(order(alice_, t.market, Number(20'000'000'000'000'001, -16), Number(1)),
            Ter(temMALFORMED));
        env(order(alice_, t.market, Number(2'000'000'000'000'001, -15), Number(1)));
        env.close();
        auto const position = env.le(positionKeylet(alice_, t));
        BEAST_EXPECT(position && (*position)[sfOpenBuy] == Number(1));

        // A zero price or size is malformed.
        env(order(alice_, t.market, Number(), Number(1)), Ter(temMALFORMED));
        env(order(alice_, t.market, Number(2), Number()), Ter(temMALFORMED));
    }

    void
    testPriceBand()
    {
        testcase("price band");
        using namespace test::jtx;
        Env env{*this, all_};
        auto const t =
            setupTrading(env, {}, [](Perp::MarketArgs& args) { args.indexMaxLedgers = 20; });

        // Index 2.0 with a 10 % band: buys up to 2.2, sells down to 1.8.
        env(order(alice_, t.market, Number(22001, -4), Number(1)), Ter(tecPERP_PRICE_BAND));
        env(order(alice_, t.market, Number(22, -1), Number(1)));
        env(order(bob_, t.market, Number(17999, -4), Number(1), tfPerpSell),
            Ter(tecPERP_PRICE_BAND));
        env(order(bob_, t.market, Number(30, -1), Number(1), tfPerpSell));
        env.close();

        // The price is a multiple of sfPriceTick and the size of sfLotSize.
        env(order(alice_, t.market, Number(200005, -5), Number(1)), Ter(tecPRECISION_LOSS));
        env(order(alice_, t.market, Number(2), Number(15, -1)), Ter(tecPRECISION_LOSS));
        env.close();

        // Without an index quorum, no order is placed.
        for (int i = 0; i < 21; ++i)
            env.close();
        env(order(alice_, t.market, Number(2), Number(1)), Ter(tecPERP_STALE_PRICE));
    }

    void
    testFillZeroSum()
    {
        testcase("fill zero sum");
        using namespace test::jtx;
        Env env{*this, all_};
        auto const t = setupTrading(env);

        env(order(bob_, t.market, Number(2), Number(10), tfPerpSell));
        env(order(alice_, t.market, Number(2), Number(10)));
        {
            BEAST_EXPECT(sizeOf(env, alice_, t) == Number(10));
            BEAST_EXPECT(sizeOf(env, bob_, t) == Number(-10));
            auto const a = env.le(positionKeylet(alice_, t));
            auto const b = env.le(positionKeylet(bob_, t));
            BEAST_EXPECT(a && (*a)[sfEntryNotional] == Number(20));
            BEAST_EXPECT(b && (*b)[sfEntryNotional] == Number(-20));
            BEAST_EXPECT(a && (*a)[sfOpenBuy] == Number() && (*a)[sfOrderCount] == 0);
            BEAST_EXPECT(b && (*b)[sfOpenSell] == Number() && (*b)[sfOrderCount] == 0);
            auto const market = env.le(t.market);
            BEAST_EXPECT(market && (*market)[sfOpenInterestLong] == Number(10));
            BEAST_EXPECT(market && (*market)[sfOpenInterestShort] == Number(10));
            BEAST_EXPECT(market && (*market)[sfOrderCount] == 0);
            expectAccounting(env, t);
        }

        // A partial close realizes PnL against the settlement balance.
        env(order(carol_, t.market, Number(21, -1), Number(4)));
        env(order(alice_, t.market, Number(21, -1), Number(4), tfPerpSell));
        {
            BEAST_EXPECT(sizeOf(env, alice_, t) == Number(6));
            BEAST_EXPECT(sizeOf(env, carol_, t) == Number(4));
            BEAST_EXPECT(
                sizeOf(env, alice_, t) + sizeOf(env, bob_, t) + sizeOf(env, carol_, t) == Number());
            auto const a = env.le(positionKeylet(alice_, t));
            BEAST_EXPECT(a && (*a)[sfEntryNotional] == Number(12));
            BEAST_EXPECT(cashOf(env, alice_, t.exchange) == Number(10004, -1));
            auto const sle = env.le(t.exchange);
            BEAST_EXPECT(sle && (*sle)[sfSettlementBalance] == Number(-4, -1));
            BEAST_EXPECT(sle && (*sle)[sfCashTotal] == Number(30004, -1));
            auto const market = env.le(t.market);
            BEAST_EXPECT(market && (*market)[sfOpenInterestLong] == Number(10));
            BEAST_EXPECT(market && (*market)[sfOpenInterestShort] == Number(10));
            expectAccounting(env, t);
        }
        env.close();
        expectAccounting(env, t);
    }

    void
    testRealizedAndFlip()
    {
        testcase("realized and flip");
        using namespace test::jtx;
        Env env{*this, all_};
        auto const t = setupTrading(env, {}, {}, {{"XRP", "USD", 1'050, 1}});

        // Long 10 at 100, then sell 15 at 110, all in one ledger so no funding accrues.
        env(order(bob_, t.market, Number(100), Number(10), tfPerpSell));
        env(order(alice_, t.market, Number(100), Number(10)));
        {
            auto const a = env.le(positionKeylet(alice_, t));
            BEAST_EXPECT(a && (*a)[sfSize] == Number(10) && (*a)[sfEntryNotional] == Number(1'000));
        }

        // The short side's loss index moved before the flip.
        seed(env, t.market, [](ApplyView&, SLE::pointer const& sle) {
            (*sle)[sfLossIndexShort] = Number(5, -1);
        });
        env(order(carol_, t.market, Number(110), Number(15)));
        env(order(alice_, t.market, Number(110), Number(15), tfPerpSell));
        {
            auto const a = env.le(positionKeylet(alice_, t));
            auto const market = env.le(t.market);
            if (!BEAST_EXPECT(a && market))
                return;
            BEAST_EXPECT((*a)[sfSize] == Number(-5));
            BEAST_EXPECT((*a)[sfEntryNotional] == Number(-550));
            BEAST_EXPECT((*a)[sfLossIndex] == Number(5, -1));
            BEAST_EXPECT((*a)[sfFundingIndex] == (*market)[sfCumulativeFunding]);
            BEAST_EXPECT(cashOf(env, alice_, t.exchange) == Number(1'100));
            auto const sle = env.le(t.exchange);
            BEAST_EXPECT(sle && (*sle)[sfSettlementBalance] == Number(-100));
            BEAST_EXPECT((*market)[sfOpenInterestLong] == Number(15));
            BEAST_EXPECT((*market)[sfOpenInterestShort] == Number(15));
            BEAST_EXPECT(sizeOf(env, carol_, t) == Number(15));
            BEAST_EXPECT(sizeOf(env, bob_, t) == Number(-10));
        }
    }

    void
    testFees()
    {
        testcase("fees");
        using namespace test::jtx;

        // Taker 1 %, maker 0.5 %; owner 20 % of each fee, the rest to the insurance fund.
        Env env{*this, all_};
        auto const t = setupTrading(env, [](json::Value& jv) {
            jv[sfTakerFee] = 1'000;
            jv[sfMakerFee] = 500;
            jv[sfOwnerFeeShare] = 20'000;
        });
        env(order(bob_, t.market, Number(2), Number(10), tfPerpSell));
        env.close();
        env(order(alice_, t.market, Number(2), Number(10)));
        env.close();

        BEAST_EXPECT(cashOf(env, alice_, t.exchange) == Number(9998, -1));
        BEAST_EXPECT(cashOf(env, bob_, t.exchange) == Number(9999, -1));
        auto const sle = env.le(t.exchange);
        BEAST_EXPECT(sle && (*sle)[sfOwnerFees] == Number(6, -2));
        BEAST_EXPECT(sle && (*sle)[sfInsuranceFund] == Number(24, -2));
        BEAST_EXPECT(sle && (*sle)[sfAssetsTotal] == Number(3'000));
        expectAccounting(env, t);
    }

    void
    testBuilderFee()
    {
        testcase("builder fee");
        using namespace test::jtx;
        Env env{*this, all_};
        auto const t = setupTrading(env, [](json::Value& jv) { jv[sfBuilderFeeMax] = 1'000; });

        auto const withBuilder = [&](json::Value jv, Account const& builder, std::uint32_t fee) {
            jv[sfBuilder] = builder.human();
            jv[sfBuilderFee] = fee;
            return jv;
        };

        // A fee above sfBuilderFeeMax, and a builder without a margin account on the exchange.
        env(withBuilder(order(alice_, t.market, Number(2), Number(10)), carol_, 1'001),
            Ter(tecNO_PERMISSION));
        env(withBuilder(order(alice_, t.market, Number(2), Number(10)), liq_, 500),
            Ter(tecNO_ENTRY));
        env.close();

        // The maker's builder is paid from the maker, the taker's from the taker: 0.5 % of 20.
        env(withBuilder(order(bob_, t.market, Number(2), Number(10), tfPerpSell), carol_, 250));
        env.close();
        env(withBuilder(order(alice_, t.market, Number(2), Number(10)), carol_, 500));
        env.close();
        BEAST_EXPECT(cashOf(env, alice_, t.exchange) == Number(9999, -1));
        BEAST_EXPECT(cashOf(env, bob_, t.exchange) == Number(99995, -2));
        BEAST_EXPECT(cashOf(env, carol_, t.exchange) == Number(100015, -2));
        expectAccounting(env, t);

        // A builder whose margin account is gone: the fee goes to the insurance fund.
        env(withBuilder(order(bob_, t.market, Number(2), Number(10), tfPerpSell), carol_, 500));
        env.close();
        env(Perp::withdraw(carol_, t.exchange.key, STAmount{usd_.issue(), Number(100015, -2)}),
            Txflags(tfPerpDeleteAccount));
        env.close();
        BEAST_EXPECT(!env.le(marginKeylet(carol_, t.exchange)));
        env(order(alice_, t.market, Number(2), Number(10)));
        env.close();
        BEAST_EXPECT(near(cashOf(env, bob_, t.exchange), Number(99985, -2)));
        auto const sle = env.le(t.exchange);
        BEAST_EXPECT(sle && (*sle)[sfInsuranceFund] == Number(1, -1));
        expectAccounting(env, t);
    }

    void
    testSelfTrade()
    {
        testcase("self trade");
        using namespace test::jtx;
        Env env{*this, all_};
        auto const t = setupTrading(env);

        // Alice's own asks are cancelled; bob's ask behind them fills.
        auto const own = Perp::orderKeylet(env, alice_);
        env(order(alice_, t.market, Number(2), Number(5), tfPerpSell));
        auto const second = Perp::orderKeylet(env, alice_);
        env(order(alice_, t.market, Number(2), Number(5), tfPerpSell));
        env.close();
        auto const bobAsk = Perp::orderKeylet(env, bob_);
        env(order(bob_, t.market, Number(2), Number(5), tfPerpSell));
        env.close();

        auto const ownerCount = env.ownerCount(alice_);
        env(order(alice_, t.market, Number(2), Number(5)));
        env.close();
        BEAST_EXPECT(!env.le(own));
        BEAST_EXPECT(!env.le(second));
        BEAST_EXPECT(!env.le(bobAsk));
        BEAST_EXPECT(sizeOf(env, alice_, t) == Number(5));
        BEAST_EXPECT(sizeOf(env, bob_, t) == Number(-5));
        {
            auto const position = env.le(positionKeylet(alice_, t));
            BEAST_EXPECT(position && (*position)[sfOpenSell] == Number());
            BEAST_EXPECT(position && (*position)[sfOrderCount] == 0);
        }
        // Two orders fewer; the position stays, now holding a size.
        BEAST_EXPECT(env.ownerCount(alice_) == ownerCount - 2);

        // With only its own ask on the book, the taker's buy rests.
        env(order(alice_, t.market, Number(2), Number(5), tfPerpSell));
        env(order(alice_, t.market, Number(2), Number(5)));
        env.close();
        {
            auto const position = env.le(positionKeylet(alice_, t));
            BEAST_EXPECT(position && (*position)[sfSize] == Number(5));
            BEAST_EXPECT(position && (*position)[sfOpenBuy] == Number(5));
            BEAST_EXPECT(position && (*position)[sfOpenSell] == Number());
            BEAST_EXPECT(position && (*position)[sfOrderCount] == 1);
        }
        expectAccounting(env, t);
    }

    void
    testMakerUnfundedDeleted()
    {
        testcase("maker unfunded deleted");
        using namespace test::jtx;
        Env env{*this, all_};
        auto const t = setupTrading(env);

        // Bob's ask needs IM 20 when it fills; his cash falls to 1.
        auto const bobAsk = Perp::orderKeylet(env, bob_);
        auto const ownerCount = env.ownerCount(bob_);
        env(order(bob_, t.market, Number(2), Number(100), tfPerpSell));
        auto const carolAsk = Perp::orderKeylet(env, carol_);
        env(order(carol_, t.market, Number(201, -2), Number(100), tfPerpSell));
        env.close();
        BEAST_EXPECT(env.ownerCount(bob_) == ownerCount + 2);
        seedCashToInsurance(env, bob_, t.exchange, Number(999));

        env(order(alice_, t.market, Number(201, -2), Number(50)));
        BEAST_EXPECT(!env.le(bobAsk));
        BEAST_EXPECT(!env.le(positionKeylet(bob_, t)));
        BEAST_EXPECT(env.ownerCount(bob_) == ownerCount);
        BEAST_EXPECT(sizeOf(env, alice_, t) == Number(50));
        BEAST_EXPECT(sizeOf(env, carol_, t) == Number(-50));
        auto const carol = env.le(carolAsk);
        BEAST_EXPECT(carol && (*carol)[sfSize] == Number(50));
        auto const market = env.le(t.market);
        BEAST_EXPECT(market && (*market)[sfOrderCount] == 1);
        expectAccounting(env, t);
    }

    void
    testMakerWouldGoNegative()
    {
        testcase("maker would go negative");
        using namespace test::jtx;
        Env env{*this, all_};
        auto const t = setupTrading(env);

        // Bob is long 100 at 2.0 and offers to close at 1.8, a loss of 20 against the index.
        env(order(carol_, t.market, Number(2), Number(100), tfPerpSell));
        env(order(bob_, t.market, Number(2), Number(100)));
        auto const bobAsk = Perp::orderKeylet(env, bob_);
        env(order(bob_, t.market, Number(18, -1), Number(100), tfPerpSell));
        BEAST_EXPECT(sizeOf(env, bob_, t) == Number(100));

        // With cash 10, the closing fill would leave equity -10: the ask is deleted unfilled.
        seedCashToInsurance(env, bob_, t.exchange, Number(990));
        env(order(alice_, t.market, Number(18, -1), Number(100)));
        BEAST_EXPECT(!env.le(bobAsk));
        BEAST_EXPECT(sizeOf(env, bob_, t) == Number(100));
        BEAST_EXPECT(sizeOf(env, alice_, t) == Number());
        auto const position = env.le(positionKeylet(alice_, t));
        BEAST_EXPECT(position && (*position)[sfOpenBuy] == Number(100));
        expectAccounting(env, t);
    }

    void
    testReduceOnly()
    {
        testcase("reduce only");
        using namespace test::jtx;
        Env env{*this, all_};
        auto const t = setupTrading(env);

        // Without a position there is nothing to reduce.
        env(order(alice_, t.market, Number(2), Number(1), tfPerpSell | tfPerpReduceOnly),
            Ter(tecNO_PERMISSION));

        env(order(carol_, t.market, Number(2), Number(10), tfPerpSell));
        env(order(alice_, t.market, Number(2), Number(10)));
        env.close();

        // The order must oppose the position and be no larger than it.
        env(order(alice_, t.market, Number(2), Number(1), tfPerpReduceOnly), Ter(tecNO_PERMISSION));
        env(order(alice_, t.market, Number(2), Number(11), tfPerpSell | tfPerpReduceOnly),
            Ter(tecNO_PERMISSION));

        // A resting reduce-only order is counted in sfOrderCount but not in sfOpenSell.
        auto const reduce = Perp::orderKeylet(env, alice_);
        env(order(alice_, t.market, Number(21, -1), Number(10), tfPerpSell | tfPerpReduceOnly));
        env.close();
        {
            auto const sle = env.le(reduce);
            BEAST_EXPECT(sle && sle->isFlag(lsfPerpReduceOnly) && sle->isFlag(lsfPerpSell));
            auto const position = env.le(positionKeylet(alice_, t));
            BEAST_EXPECT(position && (*position)[sfOpenSell] == Number());
            BEAST_EXPECT(position && (*position)[sfOrderCount] == 1);
        }

        // Alice closes 7 elsewhere; bob's buy of 10 at her reduce-only ask fills only 3.
        env(order(bob_, t.market, Number(2), Number(7)));
        env.close();
        env(order(alice_, t.market, Number(2), Number(7), tfPerpSell | tfImmediateOrCancel));
        env.close();
        BEAST_EXPECT(sizeOf(env, alice_, t) == Number(3));
        env(order(bob_, t.market, Number(21, -1), Number(10)));
        env.close();
        BEAST_EXPECT(!env.le(reduce));
        BEAST_EXPECT(!env.le(positionKeylet(alice_, t)));
        BEAST_EXPECT(sizeOf(env, bob_, t) == Number(10));
        {
            auto const position = env.le(positionKeylet(bob_, t));
            BEAST_EXPECT(position && (*position)[sfOpenBuy] == Number(7));
            BEAST_EXPECT(position && (*position)[sfOrderCount] == 1);
        }
        expectAccounting(env, t);
    }

    void
    testOpenInterestCap()
    {
        testcase("open interest cap");
        using namespace test::jtx;
        Env env{*this, all_};
        auto const t = setupTrading(
            env, {}, [](Perp::MarketArgs& args) { args.openInterestMax = Number(10); });

        auto const bobAsk = Perp::orderKeylet(env, bob_);
        env(order(bob_, t.market, Number(2), Number(15), tfPerpSell));
        env(order(alice_, t.market, Number(2), Number(10)));
        env.close();
        BEAST_EXPECT(sizeOf(env, alice_, t) == Number(10));

        // A fill above the cap is not made; with no fill at all the taker gets tecPERP_OI_CAP.
        env(order(carol_, t.market, Number(2), Number(5)), Ter(tecPERP_OI_CAP));
        env.close();
        auto const ask = env.le(bobAsk);
        BEAST_EXPECT(ask && (*ask)[sfSize] == Number(5));
        BEAST_EXPECT(sizeOf(env, carol_, t) == Number());

        // Closing trades do not raise open interest.
        env(order(carol_, t.market, Number(199, -2), Number(5)));
        env.close();
        env(order(alice_, t.market, Number(199, -2), Number(5), tfPerpSell));
        env.close();
        BEAST_EXPECT(sizeOf(env, alice_, t) == Number(5));
        BEAST_EXPECT(sizeOf(env, carol_, t) == Number(5));
        auto const market = env.le(t.market);
        BEAST_EXPECT(market && (*market)[sfOpenInterestLong] == Number(10));
        expectAccounting(env, t);
    }

    void
    testCoverGate()
    {
        testcase("cover gate");
        using namespace test::jtx;
        Env env{*this, all_};
        auto const t = setupTrading(env, [](json::Value& jv) { jv[sfCoverRateMinimum] = 10'000; });

        // Open interest 10 at index 2.0 needs cover of 10 % of notional 20.
        env(order(bob_, t.market, Number(2), Number(20), tfPerpSell));
        env.close();
        env(order(alice_, t.market, Number(2), Number(10)), Ter(tecPERP_INSUFFICIENT_COVER));
        env.close();
        env(Perp::deposit(owner_, t.exchange.key, usd_(2)), Txflags(tfPerpCover));
        env.close();
        env(order(alice_, t.market, Number(2), Number(10)));
        env.close();
        BEAST_EXPECT(sizeOf(env, alice_, t) == Number(10));
        env(order(alice_, t.market, Number(2), Number(1)), Ter(tecPERP_INSUFFICIENT_COVER));
        env.close();

        // A trade that lowers open interest needs no cover.
        env(order(carol_, t.market, Number(19, -1), Number(4)));
        env.close();
        env(order(alice_, t.market, Number(19, -1), Number(4), tfPerpSell));
        env.close();
        BEAST_EXPECT(sizeOf(env, alice_, t) == Number(6));
        expectAccounting(env, t);
    }

    void
    testPostOnly()
    {
        testcase("post only");
        using namespace test::jtx;
        Env env{*this, all_};
        auto const t = setupTrading(env);

        auto const bobAsk = Perp::orderKeylet(env, bob_);
        env(order(bob_, t.market, Number(2), Number(5), tfPerpSell));
        env.close();

        auto const ownerCount = env.ownerCount(alice_);
        env(order(alice_, t.market, Number(2), Number(5), tfPerpPostOnly), Ter(tecKILLED));
        env.close();
        auto const ask = env.le(bobAsk);
        BEAST_EXPECT(ask && (*ask)[sfSize] == Number(5));
        BEAST_EXPECT(!env.le(positionKeylet(alice_, t)));
        BEAST_EXPECT(env.ownerCount(alice_) == ownerCount);

        auto const bid = Perp::orderKeylet(env, alice_);
        env(order(alice_, t.market, Number(199, -2), Number(5), tfPerpPostOnly));
        env.close();
        BEAST_EXPECT(env.le(bid));
        BEAST_EXPECT(env.ownerCount(alice_) == ownerCount + 2);

        // Immediate-or-cancel drops the remainder.
        env(order(carol_, t.market, Number(2), Number(8), tfImmediateOrCancel));
        env.close();
        BEAST_EXPECT(sizeOf(env, carol_, t) == Number(5));
        auto const position = env.le(positionKeylet(carol_, t));
        BEAST_EXPECT(position && (*position)[sfOpenBuy] == Number());
        BEAST_EXPECT(position && (*position)[sfOrderCount] == 0);
        expectAccounting(env, t);
    }

    void
    testPostOnlyAtOpenInterestCap()
    {
        testcase("post only at open interest cap");
        using namespace test::jtx;
        Env env{*this, all_};
        auto const t = setupTrading(
            env, {}, [](Perp::MarketArgs& args) { args.openInterestMax = Number(10); });

        auto const bobAsk = Perp::orderKeylet(env, bob_);
        env(order(bob_, t.market, Number(2), Number(15), tfPerpSell));
        env(order(alice_, t.market, Number(2), Number(10)));
        env.close();

        // A post-only order that would cross is killed even when the cap also stops the fill.
        env(order(carol_, t.market, Number(2), Number(5), tfPerpPostOnly), Ter(tecKILLED));
        env.close();
        auto const ask = env.le(bobAsk);
        BEAST_EXPECT(ask && (*ask)[sfSize] == Number(5));
        BEAST_EXPECT(!env.le(positionKeylet(carol_, t)));
    }

    void
    testPriceBelowQualityFloor()
    {
        testcase("price below quality floor");
        using namespace test::jtx;
        Env env{*this, all_};
        auto const t = setupTrading(env);

        // A price below the smallest STAmount has no book quality, so the order is malformed.
        env(order(alice_, t.market, Number(1, -98), Number(1)), Ter(temMALFORMED));
        env(order(alice_, t.market, Number(1, -98), Number(1), tfPerpSell), Ter(temMALFORMED));
        env.close();
        BEAST_EXPECT(!env.le(positionKeylet(alice_, t)));
    }

    void
    testPostMatchInsufficientMargin()
    {
        testcase("post-match insufficient margin");
        using namespace test::jtx;
        Env env{*this, all_};
        auto const t = setupTrading(env, [](json::Value& jv) { jv[sfTakerFee] = 10'000; });

        // Alice's equity of 1000 equals the initial requirement of the notional 10 000 she
        // buys, so the order passes the pre-match check and the taker fee then breaks it.
        env(order(bob_, t.market, Number(2), Number(5'000), tfPerpSell));
        env.close();
        auto const ownerCount = env.ownerCount(alice_);
        env(order(alice_, t.market, Number(2), Number(5'000)), Ter(tecPERP_INSUFFICIENT_MARGIN));
        env.close();
        BEAST_EXPECT(sizeOf(env, alice_, t) == Number());
        BEAST_EXPECT(env.ownerCount(alice_) == ownerCount);
        BEAST_EXPECT(cashOf(env, alice_, t.exchange) == Number(1'000));
        expectAccounting(env, t);
    }

    void
    testFillOrKillKeepsCleanup()
    {
        testcase("fill or kill keeps cleanup");
        using namespace test::jtx;
        Env env{*this, all_};
        auto const t = setupTrading(env);

        auto const bobAsk = Perp::orderKeylet(env, bob_);
        auto const ownerCount = env.ownerCount(bob_);
        env(order(bob_, t.market, Number(2), Number(100), tfPerpSell));
        auto const carolAsk = Perp::orderKeylet(env, carol_);
        env(order(carol_, t.market, Number(201, -2), Number(5), tfPerpSell));
        env.close();
        seedCashToInsurance(env, bob_, t.exchange, Number(999));

        // The unfunded ask is deleted, carol's fills 5 of 10, and the rest kills the order.
        env(order(alice_, t.market, Number(201, -2), Number(10), tfFillOrKill), Ter(tecKILLED));
        BEAST_EXPECT(!env.le(bobAsk));
        BEAST_EXPECT(!env.le(positionKeylet(bob_, t)));
        BEAST_EXPECT(env.ownerCount(bob_) == ownerCount);
        auto const carol = env.le(carolAsk);
        BEAST_EXPECT(carol && (*carol)[sfSize] == Number(5));
        BEAST_EXPECT(!env.le(positionKeylet(alice_, t)));
        auto const market = env.le(t.market);
        BEAST_EXPECT(market && (*market)[sfOrderCount] == 1);
        BEAST_EXPECT(market && (*market)[sfOpenInterestLong] == Number());

        // A fill-or-kill that fills in full succeeds.
        env(order(alice_, t.market, Number(201, -2), Number(5), tfFillOrKill));
        BEAST_EXPECT(sizeOf(env, alice_, t) == Number(5));
        expectAccounting(env, t);
    }

    void
    testFillCap()
    {
        testcase("fill cap");
        using namespace test::jtx;
        Env env{*this, all_};
        auto t = setupTrading(env);

        // 65 makers, one ask of 1 each on its own price level.
        std::vector<Keylet> asks;
        for (std::int64_t i = 0; i < 65; ++i)
        {
            // Quotes older than kIndexMaxAge would leave the market Stale.
            if (i % 5 == 0)
                freshenOracles(env, t.world);
            Account const maker{"m" + std::to_string(i)};
            env.fund(XRP(10'000), maker);
            env.close();
            env.trust(usd_(1'000), maker);
            env(pay(gw_, maker, usd_(100)));
            env(Perp::deposit(maker, t.exchange.key, usd_(100)));
            asks.push_back(Perp::orderKeylet(env, maker));
            env(order(maker, t.market, Number(20'000 + i, -4), Number(1), tfPerpSell));
            env.close();
        }

        // The taker visits 64 makers and drops the rest; the 65th ask stays.
        freshenOracles(env, t.world);
        env(order(alice_, t.market, Number(21, -1), Number(65)));
        auto const meta = env.meta();
        env.close();
        BEAST_EXPECT(sizeOf(env, alice_, t) == Number(64));
        for (std::size_t i = 0; i < 64; ++i)
            BEAST_EXPECT(!env.le(asks[i]));
        BEAST_EXPECT(env.le(asks[64]));
        auto const position = env.le(positionKeylet(alice_, t));
        BEAST_EXPECT(position && (*position)[sfOpenBuy] == Number());
        BEAST_EXPECT(position && (*position)[sfOrderCount] == 0);
        if (BEAST_EXPECT(meta))
        {
            auto const affected = meta->getFieldArray(sfAffectedNodes).size();
            log << "64-fill PerpOrderCreate affected entries: " << affected << std::endl;
            BEAST_EXPECT(affected <= 400);
        }
        expectAccounting(env, t);
    }

    void
    testIsolatedOnly()
    {
        testcase("isolated only");
        using namespace test::jtx;
        Env env{*this, all_};
        setupMarketWorld(env, {{"XRP", "USD", 20, 1}, {"ETH", "USD", 20, 1}});
        auto const exchange = createExchange(env, owner_, usd_);
        auto args = liveArgs();
        auto isolated = Perp::marketSet(owner_, exchange.key, args);
        isolated[jss::Flags] = tfPerpIsolatedOnly;
        env(isolated);
        args.baseAsset = "ETH";
        env(Perp::marketSet(owner_, exchange.key, args));
        env.close();
        Keylet const xrp = marketKeylet(exchange.key);
        Keylet const eth = marketKeylet(exchange.key, "ETH");
        for (auto const* trader : {&alice_, &bob_})
            env(Perp::deposit(*trader, exchange.key, usd_(1'000)));
        env.close();

        // An account with another position cannot trade an isolated-only market.
        env(order(alice_, eth, Number(19, -1), Number(1)));
        env(order(alice_, xrp, Number(19, -1), Number(1)), Ter(tecPERP_POSITION_LIMIT));

        // An account with an isolated-only position cannot trade another market.
        env(order(bob_, xrp, Number(19, -1), Number(1)));
        env(order(bob_, xrp, Number(18, -1), Number(1)));
        env(order(bob_, eth, Number(19, -1), Number(1)), Ter(tecPERP_POSITION_LIMIT));

        // A second XRPL account isolates it.
        env(Perp::deposit(carol_, exchange.key, usd_(100)));
        env(order(carol_, eth, Number(19, -1), Number(1)));
        env.close();
        auto const margin = env.le(marginKeylet(carol_, exchange));
        BEAST_EXPECT(margin && margin->getFieldV256(sfMarkets).size() == 1);
    }

    void
    testPositionLimit()
    {
        testcase("position limit");
        using namespace test::jtx;
        Env env{*this, all_};

        // 17 pairs: ten in document 1 of each oracle, seven in document 2.
        std::vector<std::string> bases;
        bases.reserve(17);
        for (int i = 0; i < 17; ++i)
            bases.push_back(std::string("C") + (i < 10 ? "0" : "") + std::to_string(i));
        oracle::DataSeries first;
        oracle::DataSeries second;
        for (std::size_t i = 0; i < bases.size(); ++i)
            (i < 10 ? first : second).emplace_back(bases[i], "USD", 20, 1);
        setupMarketWorld(env, first);
        for (auto const& provider : {o1_, o2_, o3_})
        {
            oracle::Oracle const doc2(
                env, {.owner = provider.id(), .documentID = 2, .series = second});
        }
        env.close();

        auto const exchange = createExchange(env, owner_, usd_);
        std::vector<Keylet> markets;
        for (std::size_t i = 0; i < bases.size(); ++i)
        {
            auto args = liveArgs();
            args.baseAsset = bases[i];
            if (i >= 10)
            {
                for (auto& ref : args.oracles)
                    ref.documentID = 2;
            }
            env(Perp::marketSet(owner_, exchange.key, args));
            markets.push_back(marketKeylet(exchange.key, bases[i]));
        }
        env.close();
        env(Perp::deposit(alice_, exchange.key, usd_(10'000)));
        env.close();

        // Sixteen positions fill sfMarkets; a seventeenth is refused.
        for (std::size_t i = 0; i < 16; ++i)
            env(order(alice_, markets[i], Number(19, -1), Number(1)));
        env.close();
        env(order(alice_, markets[16], Number(19, -1), Number(1)), Ter(tecPERP_POSITION_LIMIT));
        auto const margin = env.le(marginKeylet(alice_, exchange));
        BEAST_EXPECT(margin && margin->getFieldV256(sfMarkets).size() == 16);

        // No order may take the worst-case notional above the last tier's 1 000 000.
        env(pay(gw_, bob_, usd_(200'000)));
        env(Perp::deposit(bob_, exchange.key, usd_(300'000)));
        env.close();
        env(order(bob_, markets[0], Number(2), Number(500'001)), Ter(tecPERP_POSITION_LIMIT));
        env(order(bob_, markets[0], Number(2), Number(500'000)));
    }

    void
    testOfferSequenceReplace()
    {
        testcase("offer sequence replace");
        using namespace test::jtx;
        Env env{*this, all_};
        auto const t = setupTrading(env);

        // Alice's cash covers one bid of 4000 at 2.0 (IM 800 of 1000), not two.
        auto const first = Perp::orderKeylet(env, alice_);
        auto const firstSeq = env.seq(alice_);
        env(order(alice_, t.market, Number(2), Number(4'000)));
        env.close();
        env(order(alice_, t.market, Number(2), Number(4'000)), Ter(tecPERP_INSUFFICIENT_MARGIN));
        env.close();

        auto const ownerCount = env.ownerCount(alice_);
        auto const second = Perp::orderKeylet(env, alice_);
        auto replace = order(alice_, t.market, Number(199, -2), Number(4'000));
        replace[sfOfferSequence] = firstSeq;
        env(replace);
        env.close();
        BEAST_EXPECT(!env.le(first));
        auto const sle = env.le(second);
        BEAST_EXPECT(sle && (*sle)[sfLimitPrice] == Number(199, -2));
        BEAST_EXPECT(env.ownerCount(alice_) == ownerCount);
        auto const position = env.le(positionKeylet(alice_, t));
        BEAST_EXPECT(position && (*position)[sfOpenBuy] == Number(4'000));
        BEAST_EXPECT(position && (*position)[sfOrderCount] == 1);

        // Naming an order that is gone cancels nothing and still places the new one.
        auto again = order(alice_, t.market, Number(19, -1), Number(1));
        again[sfOfferSequence] = firstSeq;
        env(again);
        env.close();
        auto const after = env.le(positionKeylet(alice_, t));
        BEAST_EXPECT(after && (*after)[sfOrderCount] == 2);
    }

    void
    testCancelByAnyoneSettledOrExpired()
    {
        testcase("cancel by anyone settled or expired");
        using namespace test::jtx;
        Env env{*this, all_};
        auto const t = setupTrading(env);

        auto const now = env.current()->parentCloseTime().time_since_epoch().count();
        auto expiring = order(alice_, t.market, Number(19, -1), Number(1));
        expiring[sfExpiration] = static_cast<std::uint32_t>(now + 30);
        auto const expiringSeq = env.seq(alice_);
        auto const expiringOrder = Perp::orderKeylet(env, alice_);
        env(expiring);

        // An order already expired is not placed.
        auto stale = order(alice_, t.market, Number(19, -1), Number(1));
        stale[sfExpiration] = static_cast<std::uint32_t>(now);
        env(stale, Ter(tecEXPIRED));

        // Carol's expiring ask is deleted unfilled by the first taker after it expires.
        auto carolAsk = order(carol_, t.market, Number(2), Number(5), tfPerpSell);
        carolAsk[sfExpiration] = static_cast<std::uint32_t>(now + 30);
        auto const carolOrder = Perp::orderKeylet(env, carol_);
        env(carolAsk);
        auto const otherSeq = env.seq(alice_);
        auto const other = Perp::orderKeylet(env, alice_);
        env(order(alice_, t.market, Number(18, -1), Number(1)));
        env.close();

        auto byBob = Perp::cancel(bob_, expiringSeq);
        byBob[sfOwner] = alice_.human();
        env(byBob, Ter(tecNO_PERMISSION));
        env.close(std::chrono::seconds{60});

        env(byBob);
        env.close();
        BEAST_EXPECT(!env.le(expiringOrder));
        BEAST_EXPECT(env.le(other));

        env(order(bob_, t.market, Number(2), Number(5)));
        env.close();
        BEAST_EXPECT(!env.le(carolOrder));
        BEAST_EXPECT(sizeOf(env, bob_, t) == Number());
        BEAST_EXPECT(!env.le(positionKeylet(carol_, t)));

        // In a Settled market anyone cancels any order, and no order is placed.
        auto settle = Perp::marketUpdate(owner_, t.market.key);
        settle[jss::Flags] = tfPerpMarketSettle;
        env(settle);
        env.close();
        env(order(alice_, t.market, Number(2), Number(1)), Ter(tecPERP_MARKET_SETTLED));
        auto byCarol = Perp::cancel(carol_, otherSeq);
        byCarol[sfOwner] = alice_.human();
        env(byCarol);
        env.close();
        BEAST_EXPECT(!env.le(other));
    }

public:
    void
    run() override
    {
        testRestAndCancel();
        testPriceTimePriority();
        testLimitPriceDigits();
        testPriceBand();
        testFillZeroSum();
        testRealizedAndFlip();
        testFees();
        testBuilderFee();
        testSelfTrade();
        testMakerUnfundedDeleted();
        testMakerWouldGoNegative();
        testReduceOnly();
        testOpenInterestCap();
        testCoverGate();
        testPostOnly();
        testPostOnlyAtOpenInterestCap();
        testPriceBelowQualityFloor();
        testPostMatchInsufficientMargin();
        testFillOrKillKeepsCleanup();
        testFillCap();
        testIsolatedOnly();
        testPositionLimit();
        testOfferSequenceReplace();
        testCancelByAnyoneSettledOrExpired();
    }
};

BEAST_DEFINE_TESTSUITE(PerpOrder, perp, xrpl);

}  // namespace xrpl

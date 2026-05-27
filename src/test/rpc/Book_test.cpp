#include <test/jtx/Env.h>
#include <test/jtx/WSClient.h>
#include <test/jtx/amount.h>
#include <test/jtx/domain.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/offer.h>
#include <test/jtx/owners.h>
#include <test/jtx/pay.h>
#include <test/jtx/permissioned_dex.h>
#include <test/jtx/rate.h>
#include <test/jtx/require.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>

#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <chrono>
#include <memory>
#include <optional>
#include <string>

namespace xrpl::test {

class Book_test : public beast::unit_test::Suite
{
    static std::string
    getBookDir(
        jtx::Env& env,
        Issue const& in,
        Issue const& out,
        std::optional<uint256> const& domain = std::nullopt)
    {
        std::string dir;
        auto uBookBase = getBookBase({in, out, domain});
        auto uBookEnd = getQualityNext(uBookBase);
        auto view = env.closed();
        auto key = view->succ(uBookBase, uBookEnd);
        if (key)
        {
            auto sleOfferDir = view->read(keylet::page(key.value()));
            uint256 offerIndex;
            unsigned int bookEntry = 0;
            cdirFirst(*view, sleOfferDir->key(), sleOfferDir, bookEntry, offerIndex);
            auto sleOffer = view->read(keylet::offer(offerIndex));
            dir = to_string(sleOffer->getFieldH256(sfBookDirectory));
        }
        return dir;
    }

public:
    void
    testOneSideEmptyBook()
    {
        testcase("One Side Empty Book");
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this);
        env.fund(XRP(10000), "alice");
        auto usd = Account("alice")["USD"];
        auto wsc = makeWSClient(env.app().config());
        json::Value books;

        {
            // RPC subscribe to books stream
            books[jss::books] = json::ValueType::Array;
            {
                auto& j = books[jss::books].append(json::ValueType::Object);
                j[jss::snapshot] = true;
                j[jss::taker_gets][jss::currency] = "XRP";
                j[jss::taker_pays][jss::currency] = "USD";
                j[jss::taker_pays][jss::issuer] = Account("alice").human();
            }

            auto jv = wsc->invoke("subscribe", books);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            if (!BEAST_EXPECT(jv[jss::status] == "success"))
                return;
            BEAST_EXPECT(
                jv[jss::result].isMember(jss::offers) && jv[jss::result][jss::offers].size() == 0);
            BEAST_EXPECT(!jv[jss::result].isMember(jss::asks));
            BEAST_EXPECT(!jv[jss::result].isMember(jss::bids));
        }

        {
            // Create an ask: TakerPays 700, TakerGets 100/USD
            env(offer("alice", XRP(700), usd(100)), Require(Owners("alice", 1)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                auto const& t = jv[jss::transaction];
                return t[jss::TransactionType] == jss::OfferCreate &&
                    t[jss::TakerGets] == usd(100).value().getJson(JsonOptions::Values::None) &&
                    t[jss::TakerPays] == XRP(700).value().getJson(JsonOptions::Values::None);
            }));
        }

        {
            // Create a bid: TakerPays 100/USD, TakerGets 75
            env(offer("alice", usd(100), XRP(75)), Require(Owners("alice", 2)));
            env.close();
            BEAST_EXPECT(!wsc->getMsg(10ms));
        }

        // RPC unsubscribe
        auto jv = wsc->invoke("unsubscribe", books);
        BEAST_EXPECT(jv[jss::status] == "success");
        if (wsc->version() == 2)
        {
            BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
        }
    }

    void
    testOneSideOffersInBook()
    {
        testcase("One Side Offers In Book");
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this);
        env.fund(XRP(10000), "alice");
        auto usd = Account("alice")["USD"];
        auto wsc = makeWSClient(env.app().config());
        json::Value books;

        // Create an ask: TakerPays 500, TakerGets 100/USD
        env(offer("alice", XRP(500), usd(100)), Require(Owners("alice", 1)));

        // Create a bid: TakerPays 100/USD, TakerGets 200
        env(offer("alice", usd(100), XRP(200)), Require(Owners("alice", 2)));
        env.close();

        {
            // RPC subscribe to books stream
            books[jss::books] = json::ValueType::Array;
            {
                auto& j = books[jss::books].append(json::ValueType::Object);
                j[jss::snapshot] = true;
                j[jss::taker_gets][jss::currency] = "XRP";
                j[jss::taker_pays][jss::currency] = "USD";
                j[jss::taker_pays][jss::issuer] = Account("alice").human();
            }

            auto jv = wsc->invoke("subscribe", books);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            if (!BEAST_EXPECT(jv[jss::status] == "success"))
                return;
            BEAST_EXPECT(
                jv[jss::result].isMember(jss::offers) && jv[jss::result][jss::offers].size() == 1);
            BEAST_EXPECT(
                jv[jss::result][jss::offers][0u][jss::TakerGets] ==
                XRP(200).value().getJson(JsonOptions::Values::None));
            BEAST_EXPECT(
                jv[jss::result][jss::offers][0u][jss::TakerPays] ==
                usd(100).value().getJson(JsonOptions::Values::None));
            BEAST_EXPECT(!jv[jss::result].isMember(jss::asks));
            BEAST_EXPECT(!jv[jss::result].isMember(jss::bids));
        }

        {
            // Create an ask: TakerPays 700, TakerGets 100/USD
            env(offer("alice", XRP(700), usd(100)), Require(Owners("alice", 3)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                auto const& t = jv[jss::transaction];
                return t[jss::TransactionType] == jss::OfferCreate &&
                    t[jss::TakerGets] == usd(100).value().getJson(JsonOptions::Values::None) &&
                    t[jss::TakerPays] == XRP(700).value().getJson(JsonOptions::Values::None);
            }));
        }

        {
            // Create a bid: TakerPays 100/USD, TakerGets 75
            env(offer("alice", usd(100), XRP(75)), Require(Owners("alice", 4)));
            env.close();
            BEAST_EXPECT(!wsc->getMsg(10ms));
        }

        // RPC unsubscribe
        auto jv = wsc->invoke("unsubscribe", books);
        BEAST_EXPECT(jv[jss::status] == "success");
        if (wsc->version() == 2)
        {
            BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
        }
    }

    void
    testBothSidesEmptyBook()
    {
        testcase("Both Sides Empty Book");
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this);
        env.fund(XRP(10000), "alice");
        auto usd = Account("alice")["USD"];
        auto wsc = makeWSClient(env.app().config());
        json::Value books;

        {
            // RPC subscribe to books stream
            books[jss::books] = json::ValueType::Array;
            {
                auto& j = books[jss::books].append(json::ValueType::Object);
                j[jss::snapshot] = true;
                j[jss::both] = true;
                j[jss::taker_gets][jss::currency] = "XRP";
                j[jss::taker_pays][jss::currency] = "USD";
                j[jss::taker_pays][jss::issuer] = Account("alice").human();
            }

            auto jv = wsc->invoke("subscribe", books);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            if (!BEAST_EXPECT(jv[jss::status] == "success"))
                return;
            BEAST_EXPECT(
                jv[jss::result].isMember(jss::asks) && jv[jss::result][jss::asks].size() == 0);
            BEAST_EXPECT(
                jv[jss::result].isMember(jss::bids) && jv[jss::result][jss::bids].size() == 0);
            BEAST_EXPECT(!jv[jss::result].isMember(jss::offers));
        }

        {
            // Create an ask: TakerPays 700, TakerGets 100/USD
            env(offer("alice", XRP(700), usd(100)), Require(Owners("alice", 1)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                auto const& t = jv[jss::transaction];
                return t[jss::TransactionType] == jss::OfferCreate &&
                    t[jss::TakerGets] == usd(100).value().getJson(JsonOptions::Values::None) &&
                    t[jss::TakerPays] == XRP(700).value().getJson(JsonOptions::Values::None);
            }));
        }

        {
            // Create a bid: TakerPays 100/USD, TakerGets 75
            env(offer("alice", usd(100), XRP(75)), Require(Owners("alice", 2)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                auto const& t = jv[jss::transaction];
                return t[jss::TransactionType] == jss::OfferCreate &&
                    t[jss::TakerGets] == XRP(75).value().getJson(JsonOptions::Values::None) &&
                    t[jss::TakerPays] == usd(100).value().getJson(JsonOptions::Values::None);
            }));
        }

        // RPC unsubscribe
        auto jv = wsc->invoke("unsubscribe", books);
        BEAST_EXPECT(jv[jss::status] == "success");
        if (wsc->version() == 2)
        {
            BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
        }
    }

    void
    testBothSidesOffersInBook()
    {
        testcase("Both Sides Offers In Book");
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this);
        env.fund(XRP(10000), "alice");
        auto usd = Account("alice")["USD"];
        auto wsc = makeWSClient(env.app().config());
        json::Value books;

        // Create an ask: TakerPays 500, TakerGets 100/USD
        env(offer("alice", XRP(500), usd(100)), Require(Owners("alice", 1)));

        // Create a bid: TakerPays 100/USD, TakerGets 200
        env(offer("alice", usd(100), XRP(200)), Require(Owners("alice", 2)));
        env.close();

        {
            // RPC subscribe to books stream
            books[jss::books] = json::ValueType::Array;
            {
                auto& j = books[jss::books].append(json::ValueType::Object);
                j[jss::snapshot] = true;
                j[jss::both] = true;
                j[jss::taker_gets][jss::currency] = "XRP";
                j[jss::taker_pays][jss::currency] = "USD";
                j[jss::taker_pays][jss::issuer] = Account("alice").human();
            }

            auto jv = wsc->invoke("subscribe", books);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            if (!BEAST_EXPECT(jv[jss::status] == "success"))
                return;
            BEAST_EXPECT(
                jv[jss::result].isMember(jss::asks) && jv[jss::result][jss::asks].size() == 1);
            BEAST_EXPECT(
                jv[jss::result].isMember(jss::bids) && jv[jss::result][jss::bids].size() == 1);
            BEAST_EXPECT(
                jv[jss::result][jss::asks][0u][jss::TakerGets] ==
                usd(100).value().getJson(JsonOptions::Values::None));
            BEAST_EXPECT(
                jv[jss::result][jss::asks][0u][jss::TakerPays] ==
                XRP(500).value().getJson(JsonOptions::Values::None));
            BEAST_EXPECT(
                jv[jss::result][jss::bids][0u][jss::TakerGets] ==
                XRP(200).value().getJson(JsonOptions::Values::None));
            BEAST_EXPECT(
                jv[jss::result][jss::bids][0u][jss::TakerPays] ==
                usd(100).value().getJson(JsonOptions::Values::None));
            BEAST_EXPECT(!jv[jss::result].isMember(jss::offers));
        }

        {
            // Create an ask: TakerPays 700, TakerGets 100/USD
            env(offer("alice", XRP(700), usd(100)), Require(Owners("alice", 3)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                auto const& t = jv[jss::transaction];
                return t[jss::TransactionType] == jss::OfferCreate &&
                    t[jss::TakerGets] == usd(100).value().getJson(JsonOptions::Values::None) &&
                    t[jss::TakerPays] == XRP(700).value().getJson(JsonOptions::Values::None);
            }));
        }

        {
            // Create a bid: TakerPays 100/USD, TakerGets 75
            env(offer("alice", usd(100), XRP(75)), Require(Owners("alice", 4)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                auto const& t = jv[jss::transaction];
                return t[jss::TransactionType] == jss::OfferCreate &&
                    t[jss::TakerGets] == XRP(75).value().getJson(JsonOptions::Values::None) &&
                    t[jss::TakerPays] == usd(100).value().getJson(JsonOptions::Values::None);
            }));
        }

        // RPC unsubscribe
        auto jv = wsc->invoke("unsubscribe", books);
        BEAST_EXPECT(jv[jss::status] == "success");
        if (wsc->version() == 2)
        {
            BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
        }
    }

    void
    testMultipleBooksOneSideEmptyBook()
    {
        testcase("Multiple Books, One Side Empty");
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this);
        env.fund(XRP(10000), "alice");
        auto usd = Account("alice")["USD"];
        auto cny = Account("alice")["CNY"];
        auto jpy = Account("alice")["JPY"];
        auto wsc = makeWSClient(env.app().config());
        json::Value books;

        {
            // RPC subscribe to books stream
            books[jss::books] = json::ValueType::Array;
            {
                auto& j = books[jss::books].append(json::ValueType::Object);
                j[jss::snapshot] = true;
                j[jss::taker_gets][jss::currency] = "XRP";
                j[jss::taker_pays][jss::currency] = "USD";
                j[jss::taker_pays][jss::issuer] = Account("alice").human();
            }
            {
                auto& j = books[jss::books].append(json::ValueType::Object);
                j[jss::snapshot] = true;
                j[jss::taker_gets][jss::currency] = "CNY";
                j[jss::taker_gets][jss::issuer] = Account("alice").human();
                j[jss::taker_pays][jss::currency] = "JPY";
                j[jss::taker_pays][jss::issuer] = Account("alice").human();
            }

            auto jv = wsc->invoke("subscribe", books);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            if (!BEAST_EXPECT(jv[jss::status] == "success"))
                return;
            BEAST_EXPECT(
                jv[jss::result].isMember(jss::offers) && jv[jss::result][jss::offers].size() == 0);
            BEAST_EXPECT(!jv[jss::result].isMember(jss::asks));
            BEAST_EXPECT(!jv[jss::result].isMember(jss::bids));
        }

        {
            // Create an ask: TakerPays 700, TakerGets 100/USD
            env(offer("alice", XRP(700), usd(100)), Require(Owners("alice", 1)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                auto const& t = jv[jss::transaction];
                return t[jss::TransactionType] == jss::OfferCreate &&
                    t[jss::TakerGets] == usd(100).value().getJson(JsonOptions::Values::None) &&
                    t[jss::TakerPays] == XRP(700).value().getJson(JsonOptions::Values::None);
            }));
        }

        {
            // Create a bid: TakerPays 100/USD, TakerGets 75
            env(offer("alice", usd(100), XRP(75)), Require(Owners("alice", 2)));
            env.close();
            BEAST_EXPECT(!wsc->getMsg(10ms));
        }

        {
            // Create an ask: TakerPays 700/CNY, TakerGets 100/JPY
            env(offer("alice", cny(700), jpy(100)), Require(Owners("alice", 3)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                auto const& t = jv[jss::transaction];
                return t[jss::TransactionType] == jss::OfferCreate &&
                    t[jss::TakerGets] == jpy(100).value().getJson(JsonOptions::Values::None) &&
                    t[jss::TakerPays] == cny(700).value().getJson(JsonOptions::Values::None);
            }));
        }

        {
            // Create a bid: TakerPays 100/JPY, TakerGets 75/CNY
            env(offer("alice", jpy(100), cny(75)), Require(Owners("alice", 4)));
            env.close();
            BEAST_EXPECT(!wsc->getMsg(10ms));
        }

        // RPC unsubscribe
        auto jv = wsc->invoke("unsubscribe", books);
        BEAST_EXPECT(jv[jss::status] == "success");
        if (wsc->version() == 2)
        {
            BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
        }
    }

    void
    testMultipleBooksOneSideOffersInBook()
    {
        testcase("Multiple Books, One Side Offers In Book");
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this);
        env.fund(XRP(10000), "alice");
        auto usd = Account("alice")["USD"];
        auto cny = Account("alice")["CNY"];
        auto jpy = Account("alice")["JPY"];
        auto wsc = makeWSClient(env.app().config());
        json::Value books;

        // Create an ask: TakerPays 500, TakerGets 100/USD
        env(offer("alice", XRP(500), usd(100)), Require(Owners("alice", 1)));

        // Create an ask: TakerPays 500/CNY, TakerGets 100/JPY
        env(offer("alice", cny(500), jpy(100)), Require(Owners("alice", 2)));

        // Create a bid: TakerPays 100/USD, TakerGets 200
        env(offer("alice", usd(100), XRP(200)), Require(Owners("alice", 3)));

        // Create a bid: TakerPays 100/JPY, TakerGets 200/CNY
        env(offer("alice", jpy(100), cny(200)), Require(Owners("alice", 4)));
        env.close();

        {
            // RPC subscribe to books stream
            books[jss::books] = json::ValueType::Array;
            {
                auto& j = books[jss::books].append(json::ValueType::Object);
                j[jss::snapshot] = true;
                j[jss::taker_gets][jss::currency] = "XRP";
                j[jss::taker_pays][jss::currency] = "USD";
                j[jss::taker_pays][jss::issuer] = Account("alice").human();
            }
            {
                auto& j = books[jss::books].append(json::ValueType::Object);
                j[jss::snapshot] = true;
                j[jss::taker_gets][jss::currency] = "CNY";
                j[jss::taker_gets][jss::issuer] = Account("alice").human();
                j[jss::taker_pays][jss::currency] = "JPY";
                j[jss::taker_pays][jss::issuer] = Account("alice").human();
            }

            auto jv = wsc->invoke("subscribe", books);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            if (!BEAST_EXPECT(jv[jss::status] == "success"))
                return;
            BEAST_EXPECT(
                jv[jss::result].isMember(jss::offers) && jv[jss::result][jss::offers].size() == 2);
            BEAST_EXPECT(
                jv[jss::result][jss::offers][0u][jss::TakerGets] ==
                XRP(200).value().getJson(JsonOptions::Values::None));
            BEAST_EXPECT(
                jv[jss::result][jss::offers][0u][jss::TakerPays] ==
                usd(100).value().getJson(JsonOptions::Values::None));
            BEAST_EXPECT(
                jv[jss::result][jss::offers][1u][jss::TakerGets] ==
                cny(200).value().getJson(JsonOptions::Values::None));
            BEAST_EXPECT(
                jv[jss::result][jss::offers][1u][jss::TakerPays] ==
                jpy(100).value().getJson(JsonOptions::Values::None));
            BEAST_EXPECT(!jv[jss::result].isMember(jss::asks));
            BEAST_EXPECT(!jv[jss::result].isMember(jss::bids));
        }

        {
            // Create an ask: TakerPays 700, TakerGets 100/USD
            env(offer("alice", XRP(700), usd(100)), Require(Owners("alice", 5)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                auto const& t = jv[jss::transaction];
                return t[jss::TransactionType] == jss::OfferCreate &&
                    t[jss::TakerGets] == usd(100).value().getJson(JsonOptions::Values::None) &&
                    t[jss::TakerPays] == XRP(700).value().getJson(JsonOptions::Values::None);
            }));
        }

        {
            // Create a bid: TakerPays 100/USD, TakerGets 75
            env(offer("alice", usd(100), XRP(75)), Require(Owners("alice", 6)));
            env.close();
            BEAST_EXPECT(!wsc->getMsg(10ms));
        }

        {
            // Create an ask: TakerPays 700/CNY, TakerGets 100/JPY
            env(offer("alice", cny(700), jpy(100)), Require(Owners("alice", 7)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                auto const& t = jv[jss::transaction];
                return t[jss::TransactionType] == jss::OfferCreate &&
                    t[jss::TakerGets] == jpy(100).value().getJson(JsonOptions::Values::None) &&
                    t[jss::TakerPays] == cny(700).value().getJson(JsonOptions::Values::None);
            }));
        }

        {
            // Create a bid: TakerPays 100/JPY, TakerGets 75/CNY
            env(offer("alice", jpy(100), cny(75)), Require(Owners("alice", 8)));
            env.close();
            BEAST_EXPECT(!wsc->getMsg(10ms));
        }

        // RPC unsubscribe
        auto jv = wsc->invoke("unsubscribe", books);
        BEAST_EXPECT(jv[jss::status] == "success");
        if (wsc->version() == 2)
        {
            BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
        }
    }

    void
    testMultipleBooksBothSidesEmptyBook()
    {
        testcase("Multiple Books, Both Sides Empty Book");
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this);
        env.fund(XRP(10000), "alice");
        auto usd = Account("alice")["USD"];
        auto cny = Account("alice")["CNY"];
        auto jpy = Account("alice")["JPY"];
        auto wsc = makeWSClient(env.app().config());
        json::Value books;

        {
            // RPC subscribe to books stream
            books[jss::books] = json::ValueType::Array;
            {
                auto& j = books[jss::books].append(json::ValueType::Object);
                j[jss::snapshot] = true;
                j[jss::both] = true;
                j[jss::taker_gets][jss::currency] = "XRP";
                j[jss::taker_pays][jss::currency] = "USD";
                j[jss::taker_pays][jss::issuer] = Account("alice").human();
            }
            {
                auto& j = books[jss::books].append(json::ValueType::Object);
                j[jss::snapshot] = true;
                j[jss::both] = true;
                j[jss::taker_gets][jss::currency] = "CNY";
                j[jss::taker_gets][jss::issuer] = Account("alice").human();
                j[jss::taker_pays][jss::currency] = "JPY";
                j[jss::taker_pays][jss::issuer] = Account("alice").human();
            }

            auto jv = wsc->invoke("subscribe", books);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            if (!BEAST_EXPECT(jv[jss::status] == "success"))
                return;
            BEAST_EXPECT(
                jv[jss::result].isMember(jss::asks) && jv[jss::result][jss::asks].size() == 0);
            BEAST_EXPECT(
                jv[jss::result].isMember(jss::bids) && jv[jss::result][jss::bids].size() == 0);
            BEAST_EXPECT(!jv[jss::result].isMember(jss::offers));
        }

        {
            // Create an ask: TakerPays 700, TakerGets 100/USD
            env(offer("alice", XRP(700), usd(100)), Require(Owners("alice", 1)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                auto const& t = jv[jss::transaction];
                return t[jss::TransactionType] == jss::OfferCreate &&
                    t[jss::TakerGets] == usd(100).value().getJson(JsonOptions::Values::None) &&
                    t[jss::TakerPays] == XRP(700).value().getJson(JsonOptions::Values::None);
            }));
        }

        {
            // Create a bid: TakerPays 100/USD, TakerGets 75
            env(offer("alice", usd(100), XRP(75)), Require(Owners("alice", 2)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                auto const& t = jv[jss::transaction];
                return t[jss::TransactionType] == jss::OfferCreate &&
                    t[jss::TakerGets] == XRP(75).value().getJson(JsonOptions::Values::None) &&
                    t[jss::TakerPays] == usd(100).value().getJson(JsonOptions::Values::None);
            }));
        }

        {
            // Create an ask: TakerPays 700/CNY, TakerGets 100/JPY
            env(offer("alice", cny(700), jpy(100)), Require(Owners("alice", 3)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                auto const& t = jv[jss::transaction];
                return t[jss::TransactionType] == jss::OfferCreate &&
                    t[jss::TakerGets] == jpy(100).value().getJson(JsonOptions::Values::None) &&
                    t[jss::TakerPays] == cny(700).value().getJson(JsonOptions::Values::None);
            }));
        }

        {
            // Create a bid: TakerPays 100/JPY, TakerGets 75/CNY
            env(offer("alice", jpy(100), cny(75)), Require(Owners("alice", 4)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                auto const& t = jv[jss::transaction];
                return t[jss::TransactionType] == jss::OfferCreate &&
                    t[jss::TakerGets] == cny(75).value().getJson(JsonOptions::Values::None) &&
                    t[jss::TakerPays] == jpy(100).value().getJson(JsonOptions::Values::None);
            }));
        }

        // RPC unsubscribe
        auto jv = wsc->invoke("unsubscribe", books);
        BEAST_EXPECT(jv[jss::status] == "success");
        if (wsc->version() == 2)
        {
            BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
        }
    }

    void
    testMultipleBooksBothSidesOffersInBook()
    {
        testcase("Multiple Books, Both Sides Offers In Book");
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this);
        env.fund(XRP(10000), "alice");
        auto usd = Account("alice")["USD"];
        auto cny = Account("alice")["CNY"];
        auto jpy = Account("alice")["JPY"];
        auto wsc = makeWSClient(env.app().config());
        json::Value books;

        // Create an ask: TakerPays 500, TakerGets 100/USD
        env(offer("alice", XRP(500), usd(100)), Require(Owners("alice", 1)));

        // Create an ask: TakerPays 500/CNY, TakerGets 100/JPY
        env(offer("alice", cny(500), jpy(100)), Require(Owners("alice", 2)));

        // Create a bid: TakerPays 100/USD, TakerGets 200
        env(offer("alice", usd(100), XRP(200)), Require(Owners("alice", 3)));

        // Create a bid: TakerPays 100/JPY, TakerGets 200/CNY
        env(offer("alice", jpy(100), cny(200)), Require(Owners("alice", 4)));
        env.close();

        {
            // RPC subscribe to books stream
            books[jss::books] = json::ValueType::Array;
            {
                auto& j = books[jss::books].append(json::ValueType::Object);
                j[jss::snapshot] = true;
                j[jss::both] = true;
                j[jss::taker_gets][jss::currency] = "XRP";
                j[jss::taker_pays][jss::currency] = "USD";
                j[jss::taker_pays][jss::issuer] = Account("alice").human();
            }
            // RPC subscribe to books stream
            {
                auto& j = books[jss::books].append(json::ValueType::Object);
                j[jss::snapshot] = true;
                j[jss::both] = true;
                j[jss::taker_gets][jss::currency] = "CNY";
                j[jss::taker_gets][jss::issuer] = Account("alice").human();
                j[jss::taker_pays][jss::currency] = "JPY";
                j[jss::taker_pays][jss::issuer] = Account("alice").human();
            }

            auto jv = wsc->invoke("subscribe", books);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            if (!BEAST_EXPECT(jv[jss::status] == "success"))
                return;
            BEAST_EXPECT(
                jv[jss::result].isMember(jss::asks) && jv[jss::result][jss::asks].size() == 2);
            BEAST_EXPECT(
                jv[jss::result].isMember(jss::bids) && jv[jss::result][jss::bids].size() == 2);
            BEAST_EXPECT(
                jv[jss::result][jss::asks][0u][jss::TakerGets] ==
                usd(100).value().getJson(JsonOptions::Values::None));
            BEAST_EXPECT(
                jv[jss::result][jss::asks][0u][jss::TakerPays] ==
                XRP(500).value().getJson(JsonOptions::Values::None));
            BEAST_EXPECT(
                jv[jss::result][jss::asks][1u][jss::TakerGets] ==
                jpy(100).value().getJson(JsonOptions::Values::None));
            BEAST_EXPECT(
                jv[jss::result][jss::asks][1u][jss::TakerPays] ==
                cny(500).value().getJson(JsonOptions::Values::None));
            BEAST_EXPECT(
                jv[jss::result][jss::bids][0u][jss::TakerGets] ==
                XRP(200).value().getJson(JsonOptions::Values::None));
            BEAST_EXPECT(
                jv[jss::result][jss::bids][0u][jss::TakerPays] ==
                usd(100).value().getJson(JsonOptions::Values::None));
            BEAST_EXPECT(
                jv[jss::result][jss::bids][1u][jss::TakerGets] ==
                cny(200).value().getJson(JsonOptions::Values::None));
            BEAST_EXPECT(
                jv[jss::result][jss::bids][1u][jss::TakerPays] ==
                jpy(100).value().getJson(JsonOptions::Values::None));
            BEAST_EXPECT(!jv[jss::result].isMember(jss::offers));
        }

        {
            // Create an ask: TakerPays 700, TakerGets 100/USD
            env(offer("alice", XRP(700), usd(100)), Require(Owners("alice", 5)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                auto const& t = jv[jss::transaction];
                return t[jss::TransactionType] == jss::OfferCreate &&
                    t[jss::TakerGets] == usd(100).value().getJson(JsonOptions::Values::None) &&
                    t[jss::TakerPays] == XRP(700).value().getJson(JsonOptions::Values::None);
            }));
        }

        {
            // Create a bid: TakerPays 100/USD, TakerGets 75
            env(offer("alice", usd(100), XRP(75)), Require(Owners("alice", 6)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                auto const& t = jv[jss::transaction];
                return t[jss::TransactionType] == jss::OfferCreate &&
                    t[jss::TakerGets] == XRP(75).value().getJson(JsonOptions::Values::None) &&
                    t[jss::TakerPays] == usd(100).value().getJson(JsonOptions::Values::None);
            }));
        }

        {
            // Create an ask: TakerPays 700/CNY, TakerGets 100/JPY
            env(offer("alice", cny(700), jpy(100)), Require(Owners("alice", 7)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                auto const& t = jv[jss::transaction];
                return t[jss::TransactionType] == jss::OfferCreate &&
                    t[jss::TakerGets] == jpy(100).value().getJson(JsonOptions::Values::None) &&
                    t[jss::TakerPays] == cny(700).value().getJson(JsonOptions::Values::None);
            }));
        }

        {
            // Create a bid: TakerPays 100/JPY, TakerGets 75/CNY
            env(offer("alice", jpy(100), cny(75)), Require(Owners("alice", 8)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                auto const& t = jv[jss::transaction];
                return t[jss::TransactionType] == jss::OfferCreate &&
                    t[jss::TakerGets] == cny(75).value().getJson(JsonOptions::Values::None) &&
                    t[jss::TakerPays] == jpy(100).value().getJson(JsonOptions::Values::None);
            }));
        }

        // RPC unsubscribe
        auto jv = wsc->invoke("unsubscribe", books);
        BEAST_EXPECT(jv[jss::status] == "success");
        if (wsc->version() == 2)
        {
            BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
        }
    }

    void
    testTrackOffers()
    {
        testcase("TrackOffers");
        using namespace jtx;
        Env env(*this);
        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        auto wsc = makeWSClient(env.app().config());
        env.fund(XRP(20000), alice, bob, gw);
        env.close();
        auto usd = gw["USD"];

        json::Value books;
        {
            books[jss::books] = json::ValueType::Array;
            {
                auto& j = books[jss::books].append(json::ValueType::Object);
                j[jss::snapshot] = true;
                j[jss::taker_gets][jss::currency] = "XRP";
                j[jss::taker_pays][jss::currency] = "USD";
                j[jss::taker_pays][jss::issuer] = gw.human();
            }

            auto jv = wsc->invoke("subscribe", books);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            if (!BEAST_EXPECT(jv[jss::status] == "success"))
                return;
            BEAST_EXPECT(
                jv[jss::result].isMember(jss::offers) && jv[jss::result][jss::offers].size() == 0);
            BEAST_EXPECT(!jv[jss::result].isMember(jss::asks));
            BEAST_EXPECT(!jv[jss::result].isMember(jss::bids));
        }

        env(rate(gw, 1.1));
        env.close();
        env.trust(usd(1000), alice);
        env.trust(usd(1000), bob);
        env(pay(gw, alice, usd(100)));
        env(pay(gw, bob, usd(50)));
        env(offer(alice, XRP(4000), usd(10)));
        env.close();

        json::Value jvParams;
        jvParams[jss::taker] = env.master.human();
        jvParams[jss::taker_pays][jss::currency] = "XRP";
        jvParams[jss::ledger_index] = "validated";
        jvParams[jss::taker_gets][jss::currency] = "USD";
        jvParams[jss::taker_gets][jss::issuer] = gw.human();

        auto jv = wsc->invoke("book_offers", jvParams);
        if (wsc->version() == 2)
        {
            BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
        }
        auto jrr = jv[jss::result];

        BEAST_EXPECT(jrr[jss::offers].isArray());
        BEAST_EXPECT(jrr[jss::offers].size() == 1);
        auto const jrOffer = jrr[jss::offers][0u];
        BEAST_EXPECT(jrOffer[sfAccount.fieldName] == alice.human());
        BEAST_EXPECT(jrOffer[sfBookDirectory.fieldName] == getBookDir(env, XRP, usd));
        BEAST_EXPECT(jrOffer[sfBookNode.fieldName] == "0");
        BEAST_EXPECT(jrOffer[jss::Flags] == 0);
        BEAST_EXPECT(jrOffer[sfLedgerEntryType.fieldName] == jss::Offer);
        BEAST_EXPECT(jrOffer[sfOwnerNode.fieldName] == "0");
        BEAST_EXPECT(jrOffer[sfSequence.fieldName] == 5);
        BEAST_EXPECT(jrOffer[jss::TakerGets] == usd(10).value().getJson(JsonOptions::Values::None));
        BEAST_EXPECT(
            jrOffer[jss::TakerPays] == XRP(4000).value().getJson(JsonOptions::Values::None));
        BEAST_EXPECT(jrOffer[jss::owner_funds] == "100");
        BEAST_EXPECT(jrOffer[jss::quality] == "400000000");

        using namespace std::chrono_literals;
        BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jval) {
            auto const& t = jval[jss::transaction];
            return t[jss::TransactionType] == jss::OfferCreate &&
                t[jss::TakerGets] == usd(10).value().getJson(JsonOptions::Values::None) &&
                t[jss::owner_funds] == "100" &&
                t[jss::TakerPays] == XRP(4000).value().getJson(JsonOptions::Values::None);
        }));

        env(offer(bob, XRP(2000), usd(5)));
        env.close();

        BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jval) {
            auto const& t = jval[jss::transaction];
            return t[jss::TransactionType] == jss::OfferCreate &&
                t[jss::TakerGets] == usd(5).value().getJson(JsonOptions::Values::None) &&
                t[jss::owner_funds] == "50" &&
                t[jss::TakerPays] == XRP(2000).value().getJson(JsonOptions::Values::None);
        }));

        jv = wsc->invoke("book_offers", jvParams);
        if (wsc->version() == 2)
        {
            BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
        }
        jrr = jv[jss::result];

        BEAST_EXPECT(jrr[jss::offers].isArray());
        BEAST_EXPECT(jrr[jss::offers].size() == 2);
        auto const jrNextOffer = jrr[jss::offers][1u];
        BEAST_EXPECT(jrNextOffer[sfAccount.fieldName] == bob.human());
        BEAST_EXPECT(jrNextOffer[sfBookDirectory.fieldName] == getBookDir(env, XRP, usd));
        BEAST_EXPECT(jrNextOffer[sfBookNode.fieldName] == "0");
        BEAST_EXPECT(jrNextOffer[jss::Flags] == 0);
        BEAST_EXPECT(jrNextOffer[sfLedgerEntryType.fieldName] == jss::Offer);
        BEAST_EXPECT(jrNextOffer[sfOwnerNode.fieldName] == "0");
        BEAST_EXPECT(jrNextOffer[sfSequence.fieldName] == 5);
        BEAST_EXPECT(
            jrNextOffer[jss::TakerGets] == usd(5).value().getJson(JsonOptions::Values::None));
        BEAST_EXPECT(
            jrNextOffer[jss::TakerPays] == XRP(2000).value().getJson(JsonOptions::Values::None));
        BEAST_EXPECT(jrNextOffer[jss::owner_funds] == "50");
        BEAST_EXPECT(jrNextOffer[jss::quality] == "400000000");

        jv = wsc->invoke("unsubscribe", books);
        if (wsc->version() == 2)
        {
            BEAST_EXPECT(jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
        }
        BEAST_EXPECT(jv[jss::status] == "success");
    }

    // Check that a stream only sees the given OfferCreate once
    static bool
    offerOnlyOnceInStream(
        std::unique_ptr<WSClient> const& wsc,
        std::chrono::milliseconds const& timeout,
        jtx::PrettyAmount const& takerGets,
        jtx::PrettyAmount const& takerPays)
    {
        auto maybeJv = wsc->getMsg(timeout);
        // No message
        if (!maybeJv)
            return false;
        // wrong message
        if (!(*maybeJv).isMember(jss::transaction))
            return false;
        auto const& t = (*maybeJv)[jss::transaction];
        if (t[jss::TransactionType] != jss::OfferCreate ||
            t[jss::TakerGets] != takerGets.value().getJson(JsonOptions::Values::None) ||
            t[jss::TakerPays] != takerPays.value().getJson(JsonOptions::Values::None))
            return false;
        // Make sure no other message is waiting
        return wsc->getMsg(timeout) == std::nullopt;
    }

    void
    testCrossingSingleBookOffer()
    {
        testcase("Crossing single book offer");

        // This was added to check that an OfferCreate transaction is only
        // published once in a stream, even if it updates multiple offer
        // ledger entries

        using namespace jtx;
        Env env(*this);

        // Scenario is:
        //  - Alice and Bob place identical offers for USD -> XRP
        //  - Charlie places a crossing order that takes both Alice and Bob's

        auto const gw = Account("gateway");
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const charlie = Account("charlie");
        auto const usd = gw["USD"];

        env.fund(XRP(1000000), gw, alice, bob, charlie);
        env.close();

        env(trust(alice, usd(500)));
        env(trust(bob, usd(500)));
        env.close();

        env(pay(gw, alice, usd(500)));
        env(pay(gw, bob, usd(500)));
        env.close();

        // Alice and Bob offer $500 for 500 XRP
        env(offer(alice, XRP(500), usd(500)));
        env(offer(bob, XRP(500), usd(500)));
        env.close();

        auto wsc = makeWSClient(env.app().config());
        json::Value books;
        {
            // RPC subscribe to books stream
            books[jss::books] = json::ValueType::Array;
            {
                auto& j = books[jss::books].append(json::ValueType::Object);
                j[jss::snapshot] = false;
                j[jss::taker_gets][jss::currency] = "XRP";
                j[jss::taker_pays][jss::currency] = "USD";
                j[jss::taker_pays][jss::issuer] = gw.human();
            }

            auto jv = wsc->invoke("subscribe", books);
            if (!BEAST_EXPECT(jv[jss::status] == "success"))
                return;
        }

        // Charlie places an offer that crosses Alice and Charlie's offers
        env(offer(charlie, usd(1000), XRP(1000)));
        env.close();
        env.require(offers(alice, 0), offers(bob, 0), offers(charlie, 0));
        using namespace std::chrono_literals;
        BEAST_EXPECT(offerOnlyOnceInStream(wsc, 1s, XRP(1000), usd(1000)));

        // RPC unsubscribe
        auto jv = wsc->invoke("unsubscribe", books);
        BEAST_EXPECT(jv[jss::status] == "success");
    }

    void
    testCrossingMultiBookOffer()
    {
        testcase("Crossing multi-book offer");

        // This was added to check that an OfferCreate transaction is only
        // published once in a stream, even if it auto-bridges across several
        // books that are under subscription

        using namespace jtx;
        Env env(*this);

        // Scenario is:
        //  - Alice has 1 USD and wants 100 XRP
        //  - Bob has 100 XRP and wants 1 EUR
        //  - Charlie has 1 EUR and wants 1 USD and should auto-bridge through
        //    Alice and Bob

        auto const gw = Account("gateway");
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const charlie = Account("charlie");
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];

        env.fund(XRP(1000000), gw, alice, bob, charlie);
        env.close();

        for (auto const& account : {alice, bob, charlie})
        {
            for (auto const& iou : {usd, eur})
            {
                env(trust(account, iou(1)));
            }
        }
        env.close();

        env(pay(gw, alice, usd(1)));
        env(pay(gw, charlie, eur(1)));
        env.close();

        env(offer(alice, XRP(100), usd(1)));
        env(offer(bob, eur(1), XRP(100)));
        env.close();

        auto wsc = makeWSClient(env.app().config());
        json::Value books;

        {
            // RPC subscribe to multiple book streams
            books[jss::books] = json::ValueType::Array;
            {
                auto& j = books[jss::books].append(json::ValueType::Object);
                j[jss::snapshot] = false;
                j[jss::taker_gets][jss::currency] = "XRP";
                j[jss::taker_pays][jss::currency] = "USD";
                j[jss::taker_pays][jss::issuer] = gw.human();
            }

            {
                auto& j = books[jss::books].append(json::ValueType::Object);
                j[jss::snapshot] = false;
                j[jss::taker_gets][jss::currency] = "EUR";
                j[jss::taker_gets][jss::issuer] = gw.human();
                j[jss::taker_pays][jss::currency] = "XRP";
            }

            auto jv = wsc->invoke("subscribe", books);
            if (!BEAST_EXPECT(jv[jss::status] == "success"))
                return;
        }

        // Charlies places an on offer for EUR -> USD that should auto-bridge
        env(offer(charlie, usd(1), eur(1)));
        env.close();
        using namespace std::chrono_literals;
        BEAST_EXPECT(offerOnlyOnceInStream(wsc, 1s, eur(1), usd(1)));

        // RPC unsubscribe
        auto jv = wsc->invoke("unsubscribe", books);
        BEAST_EXPECT(jv[jss::status] == "success");
    }

    void
    testBookOfferErrors()
    {
        testcase("BookOffersRPC Errors");
        using namespace jtx;
        Env env(*this);
        Account const gw{"gw"};
        Account const alice{"alice"};
        env.fund(XRP(10000), alice, gw);
        env.close();
        auto usd = gw["USD"];

        {
            json::Value jvParams;
            jvParams[jss::ledger_index] = 10u;
            auto const jrr = env.rpc("json", "book_offers", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "lgrNotFound");
            BEAST_EXPECT(jrr[jss::error_message] == "ledgerNotFound");
        }

        {
            json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            auto const jrr = env.rpc("json", "book_offers", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] == "Missing field 'taker_pays'.");
        }

        {
            json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays] = json::ValueType::Object;
            auto const jrr = env.rpc("json", "book_offers", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] == "Missing field 'taker_gets'.");
        }

        {
            json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays] = "not an object";
            jvParams[jss::taker_gets] = json::ValueType::Object;
            auto const jrr = env.rpc("json", "book_offers", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] == "Invalid field 'taker_pays', not object.");
        }

        {
            json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays] = json::ValueType::Object;
            jvParams[jss::taker_gets] = "not an object";
            auto const jrr = env.rpc("json", "book_offers", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] == "Invalid field 'taker_gets', not object.");
        }

        {
            json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays] = json::ValueType::Object;
            jvParams[jss::taker_gets] = json::ValueType::Object;
            auto const jrr = env.rpc("json", "book_offers", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] == "Missing field 'taker_pays.currency'.");
        }

        {
            json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays][jss::currency] = 1;
            jvParams[jss::taker_gets] = json::ValueType::Object;
            auto const jrr = env.rpc("json", "book_offers", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(
                jrr[jss::error_message] == "Invalid field 'taker_pays.currency', not string.");
        }

        {
            json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays][jss::currency] = "XRP";
            jvParams[jss::taker_gets] = json::ValueType::Object;
            auto const jrr = env.rpc("json", "book_offers", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] == "Missing field 'taker_gets.currency'.");
        }

        {
            json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays][jss::currency] = "XRP";
            jvParams[jss::taker_gets][jss::currency] = 1;
            auto const jrr = env.rpc("json", "book_offers", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(
                jrr[jss::error_message] == "Invalid field 'taker_gets.currency', not string.");
        }

        {
            json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays][jss::currency] = "NOT_VALID";
            jvParams[jss::taker_gets][jss::currency] = "XRP";
            auto const jrr = env.rpc("json", "book_offers", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "srcCurMalformed");
            BEAST_EXPECT(
                jrr[jss::error_message] == "Invalid field 'taker_pays.currency', bad currency.");
        }

        {
            json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays][jss::currency] = "XRP";
            jvParams[jss::taker_gets][jss::currency] = "NOT_VALID";
            auto const jrr = env.rpc("json", "book_offers", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "dstAmtMalformed");
            BEAST_EXPECT(
                jrr[jss::error_message] == "Invalid field 'taker_gets.currency', bad currency.");
        }

        {
            json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays][jss::currency] = "XRP";
            jvParams[jss::taker_gets][jss::currency] = "USD";
            jvParams[jss::taker_gets][jss::issuer] = 1;
            auto const jrr = env.rpc("json", "book_offers", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(
                jrr[jss::error_message] == "Invalid field 'taker_gets.issuer', not string.");
        }

        {
            json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays][jss::currency] = "XRP";
            jvParams[jss::taker_pays][jss::issuer] = 1;
            jvParams[jss::taker_gets][jss::currency] = "USD";
            auto const jrr = env.rpc("json", "book_offers", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(
                jrr[jss::error_message] == "Invalid field 'taker_pays.issuer', not string.");
        }

        {
            json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays][jss::currency] = "XRP";
            jvParams[jss::taker_pays][jss::issuer] = gw.human() + "DEAD";
            jvParams[jss::taker_gets][jss::currency] = "USD";
            auto const jrr = env.rpc("json", "book_offers", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "srcIsrMalformed");
            BEAST_EXPECT(
                jrr[jss::error_message] == "Invalid field 'taker_pays.issuer', bad issuer.");
        }

        {
            json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays][jss::currency] = "XRP";
            jvParams[jss::taker_pays][jss::issuer] = toBase58(noAccount());
            jvParams[jss::taker_gets][jss::currency] = "USD";
            auto const jrr = env.rpc("json", "book_offers", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "srcIsrMalformed");
            BEAST_EXPECT(
                jrr[jss::error_message] ==
                "Invalid field 'taker_pays.issuer', bad issuer account one.");
        }

        {
            json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays][jss::currency] = "XRP";
            jvParams[jss::taker_gets][jss::currency] = "USD";
            jvParams[jss::taker_gets][jss::issuer] = gw.human() + "DEAD";
            auto const jrr = env.rpc("json", "book_offers", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "dstIsrMalformed");
            BEAST_EXPECT(
                jrr[jss::error_message] == "Invalid field 'taker_gets.issuer', bad issuer.");
        }

        {
            json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays][jss::currency] = "XRP";
            jvParams[jss::taker_gets][jss::currency] = "USD";
            jvParams[jss::taker_gets][jss::issuer] = toBase58(noAccount());
            auto const jrr = env.rpc("json", "book_offers", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "dstIsrMalformed");
            BEAST_EXPECT(
                jrr[jss::error_message] ==
                "Invalid field 'taker_gets.issuer', bad issuer account one.");
        }

        {
            json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays][jss::currency] = "XRP";
            jvParams[jss::taker_pays][jss::issuer] = alice.human();
            jvParams[jss::taker_gets][jss::currency] = "USD";
            jvParams[jss::taker_gets][jss::issuer] = gw.human();
            auto const jrr = env.rpc("json", "book_offers", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "srcIsrMalformed");
            BEAST_EXPECT(
                jrr[jss::error_message] ==
                "Unneeded field 'taker_pays.issuer' "
                "for XRP currency specification.");
        }

        {
            json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays][jss::currency] = "USD";
            jvParams[jss::taker_pays][jss::issuer] = toBase58(xrpAccount());
            jvParams[jss::taker_gets][jss::currency] = "USD";
            jvParams[jss::taker_gets][jss::issuer] = gw.human();
            auto const jrr = env.rpc("json", "book_offers", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "srcIsrMalformed");
            BEAST_EXPECT(
                jrr[jss::error_message] ==
                "Invalid field 'taker_pays.issuer', expected non-XRP issuer.");
        }

        {
            json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker] = 1;
            jvParams[jss::taker_pays][jss::currency] = "XRP";
            jvParams[jss::taker_gets][jss::currency] = "USD";
            jvParams[jss::taker_gets][jss::issuer] = gw.human();
            auto const jrr = env.rpc("json", "book_offers", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] == "Invalid field 'taker', not string.");
        }

        {
            json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker] = env.master.human() + "DEAD";
            jvParams[jss::taker_pays][jss::currency] = "XRP";
            jvParams[jss::taker_gets][jss::currency] = "USD";
            jvParams[jss::taker_gets][jss::issuer] = gw.human();
            auto const jrr = env.rpc("json", "book_offers", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] == "Invalid field 'taker'.");
        }

        {
            json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker] = env.master.human();
            jvParams[jss::taker_pays][jss::currency] = "USD";
            jvParams[jss::taker_pays][jss::issuer] = gw.human();
            jvParams[jss::taker_gets][jss::currency] = "USD";
            jvParams[jss::taker_gets][jss::issuer] = gw.human();
            auto const jrr = env.rpc("json", "book_offers", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "badMarket");
            BEAST_EXPECT(jrr[jss::error_message] == "No such market.");
        }

        {
            json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker] = env.master.human();
            jvParams[jss::limit] = "0";  // NOT an integer
            jvParams[jss::taker_pays][jss::currency] = "XRP";
            jvParams[jss::taker_gets][jss::currency] = "USD";
            jvParams[jss::taker_gets][jss::issuer] = gw.human();
            auto const jrr = env.rpc("json", "book_offers", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] == "Invalid field 'limit', not unsigned integer.");
        }

        {
            json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker] = env.master.human();
            jvParams[jss::limit] = 0;  // must be > 0
            jvParams[jss::taker_pays][jss::currency] = "XRP";
            jvParams[jss::taker_gets][jss::currency] = "USD";
            jvParams[jss::taker_gets][jss::issuer] = gw.human();
            auto const jrr = env.rpc("json", "book_offers", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] == "Invalid field 'limit'.");
        }

        {
            json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays][jss::currency] = "USD";
            jvParams[jss::taker_pays][jss::issuer] = gw.human();
            jvParams[jss::taker_gets][jss::currency] = "USD";
            auto const jrr = env.rpc("json", "book_offers", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "dstIsrMalformed");
            BEAST_EXPECT(
                jrr[jss::error_message] ==
                "Invalid field 'taker_gets.issuer', "
                "expected non-XRP issuer.");
        }

        {
            json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays][jss::currency] = "USD";
            jvParams[jss::taker_pays][jss::issuer] = gw.human();
            jvParams[jss::taker_gets][jss::currency] = "XRP";
            jvParams[jss::taker_gets][jss::issuer] = gw.human();
            auto const jrr = env.rpc("json", "book_offers", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "dstIsrMalformed");
            BEAST_EXPECT(
                jrr[jss::error_message] ==
                "Unneeded field 'taker_gets.issuer' "
                "for XRP currency specification.");
        }
        {
            json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays][jss::currency] = "USD";
            jvParams[jss::taker_pays][jss::issuer] = gw.human();
            jvParams[jss::taker_gets][jss::currency] = "EUR";
            jvParams[jss::taker_gets][jss::issuer] = gw.human();
            jvParams[jss::domain] = "badString";
            auto const jrr = env.rpc("json", "book_offers", to_string(jvParams))[jss::result];
            BEAST_EXPECT(jrr[jss::error] == "domainMalformed");
            BEAST_EXPECT(jrr[jss::error_message] == "Unable to parse domain.");
        }
    }

    void
    testBookOfferLimits(bool asAdmin)
    {
        testcase("BookOffer Limits");
        using namespace jtx;
        Env env{*this, asAdmin ? envconfig() : envconfig(noAdmin)};
        Account const gw{"gw"};
        env.fund(XRP(200000), gw);
        // Note that calls to env.close() fail without admin permission.
        if (asAdmin)
            env.close();

        auto usd = gw["USD"];

        for (auto i = 0; i <= RPC::Tuning::kBookOffers.rmax; i++)
            env(offer(gw, XRP(50 + (1 * i)), usd(1.0 + (0.1 * i))));

        if (asAdmin)
            env.close();

        json::Value jvParams;
        jvParams[jss::limit] = 1;
        jvParams[jss::ledger_index] = "validated";
        jvParams[jss::taker_pays][jss::currency] = "XRP";
        jvParams[jss::taker_gets][jss::currency] = "USD";
        jvParams[jss::taker_gets][jss::issuer] = gw.human();
        auto jrr = env.rpc("json", "book_offers", to_string(jvParams))[jss::result];
        BEAST_EXPECT(jrr[jss::offers].isArray());
        BEAST_EXPECT(jrr[jss::offers].size() == (asAdmin ? 1u : 0u));
        // NOTE - a marker field is not returned for this method

        jvParams[jss::limit] = RPC::Tuning::kBookOffers.rmax + 1;
        jrr = env.rpc("json", "book_offers", to_string(jvParams))[jss::result];
        BEAST_EXPECT(jrr[jss::offers].isArray());
        BEAST_EXPECT(jrr[jss::offers].size() == (asAdmin ? RPC::Tuning::kBookOffers.rmax + 1 : 0u));

        jvParams[jss::limit] = json::ValueType::Null;
        jrr = env.rpc("json", "book_offers", to_string(jvParams))[jss::result];
        BEAST_EXPECT(jrr[jss::offers].isArray());
        BEAST_EXPECT(jrr[jss::offers].size() == (asAdmin ? RPC::Tuning::kBookOffers.rDefault : 0u));
    }

    void
    testTrackDomainOffer()
    {
        testcase("TrackDomainOffer");
        using namespace jtx;

        FeatureBitset const all{
            jtx::testableAmendments() | featurePermissionedDomains | featureCredentials |
            featurePermissionedDEX};

        Env env(*this, all);
        PermissionedDEX const permDex(env);
        auto const alice = permDex.alice;
        auto const bob = permDex.bob;
        auto const carol = permDex.carol;
        auto const domainID = permDex.domainID;
        auto const gw = permDex.gw;
        auto const usd = permDex.usd;

        auto wsc = makeWSClient(env.app().config());

        env(offer(alice, XRP(10), usd(10)), Domain(domainID));
        env.close();

        auto checkBookOffers = [&](json::Value const& jrr) {
            BEAST_EXPECT(jrr[jss::offers].isArray());
            BEAST_EXPECT(jrr[jss::offers].size() == 1);
            auto const jrOffer = jrr[jss::offers][0u];
            BEAST_EXPECT(jrOffer[sfAccount.fieldName] == alice.human());
            BEAST_EXPECT(
                jrOffer[sfBookDirectory.fieldName] == getBookDir(env, XRP, usd.issue(), domainID));
            BEAST_EXPECT(jrOffer[sfBookNode.fieldName] == "0");
            BEAST_EXPECT(jrOffer[jss::Flags] == 0);
            BEAST_EXPECT(jrOffer[sfLedgerEntryType.fieldName] == jss::Offer);
            BEAST_EXPECT(jrOffer[sfOwnerNode.fieldName] == "0");
            BEAST_EXPECT(
                jrOffer[jss::TakerGets] == usd(10).value().getJson(JsonOptions::Values::None));
            BEAST_EXPECT(
                jrOffer[jss::TakerPays] == XRP(10).value().getJson(JsonOptions::Values::None));
            BEAST_EXPECT(jrOffer[sfDomainID.jsonName].asString() == to_string(domainID));
        };

        // book_offers: open book doesn't return offer
        {
            json::Value jvParams;
            jvParams[jss::taker] = env.master.human();
            jvParams[jss::taker_pays][jss::currency] = "XRP";
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_gets][jss::currency] = "USD";
            jvParams[jss::taker_gets][jss::issuer] = gw.human();

            auto jv = wsc->invoke("book_offers", jvParams);
            auto jrr = jv[jss::result];
            BEAST_EXPECT(jrr[jss::offers].isArray());
            BEAST_EXPECT(jrr[jss::offers].size() == 0);
        }

        auto checkSubBooks = [&](json::Value const& jv) {
            BEAST_EXPECT(
                jv[jss::result].isMember(jss::offers) && jv[jss::result][jss::offers].size() == 1);
            BEAST_EXPECT(
                jv[jss::result][jss::offers][0u][jss::TakerGets] ==
                usd(10).value().getJson(JsonOptions::Values::None));
            BEAST_EXPECT(
                jv[jss::result][jss::offers][0u][jss::TakerPays] ==
                XRP(10).value().getJson(JsonOptions::Values::None));
            BEAST_EXPECT(
                jv[jss::result][jss::offers][0u][sfDomainID.jsonName].asString() ==
                to_string(domainID));
        };

        // book_offers: requesting domain book returns hybrid offer
        {
            json::Value jvParams;
            jvParams[jss::taker] = env.master.human();
            jvParams[jss::taker_pays][jss::currency] = "XRP";
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_gets][jss::currency] = "USD";
            jvParams[jss::taker_gets][jss::issuer] = gw.human();
            jvParams[jss::domain] = to_string(domainID);

            auto jv = wsc->invoke("book_offers", jvParams);
            auto jrr = jv[jss::result];
            checkBookOffers(jrr);
        }

        // subscribe to domain book should return domain offer
        {
            json::Value books;
            books[jss::books] = json::ValueType::Array;
            {
                auto& j = books[jss::books].append(json::ValueType::Object);
                j[jss::snapshot] = true;
                j[jss::taker_pays][jss::currency] = "XRP";
                j[jss::taker_gets][jss::currency] = "USD";
                j[jss::taker_gets][jss::issuer] = gw.human();
                j[jss::domain] = to_string(domainID);
            }

            auto jv = wsc->invoke("subscribe", books);
            if (!BEAST_EXPECT(jv[jss::status] == "success"))
                return;
            checkSubBooks(jv);
        }

        // subscribe to open book should not return domain offer
        {
            json::Value books;
            books[jss::books] = json::ValueType::Array;
            {
                auto& j = books[jss::books].append(json::ValueType::Object);
                j[jss::snapshot] = true;
                j[jss::taker_pays][jss::currency] = "XRP";
                j[jss::taker_gets][jss::currency] = "USD";
                j[jss::taker_gets][jss::issuer] = gw.human();
            }

            auto jv = wsc->invoke("subscribe", books);
            if (!BEAST_EXPECT(jv[jss::status] == "success"))
                return;
            BEAST_EXPECT(
                jv[jss::result].isMember(jss::offers) && jv[jss::result][jss::offers].size() == 0);
        }
    }

    void
    testTrackHybridOffer()
    {
        testcase("TrackHybridOffer");
        using namespace jtx;

        FeatureBitset const all{
            jtx::testableAmendments() | featurePermissionedDomains | featureCredentials |
            featurePermissionedDEX};

        Env env(*this, all);
        PermissionedDEX const permDex(env);
        auto const alice = permDex.alice;
        auto const bob = permDex.bob;
        auto const carol = permDex.carol;
        auto const domainID = permDex.domainID;
        auto const gw = permDex.gw;
        auto const usd = permDex.usd;

        auto wsc = makeWSClient(env.app().config());

        env(offer(alice, XRP(10), usd(10)), Domain(domainID), Txflags(tfHybrid));
        env.close();

        auto checkBookOffers = [&](json::Value const& jrr) {
            BEAST_EXPECT(jrr[jss::offers].isArray());
            BEAST_EXPECT(jrr[jss::offers].size() == 1);
            auto const jrOffer = jrr[jss::offers][0u];
            BEAST_EXPECT(jrOffer[sfAccount.fieldName] == alice.human());
            BEAST_EXPECT(
                jrOffer[sfBookDirectory.fieldName] == getBookDir(env, XRP, usd.issue(), domainID));
            BEAST_EXPECT(jrOffer[sfBookNode.fieldName] == "0");
            BEAST_EXPECT(jrOffer[jss::Flags] == lsfHybrid);
            BEAST_EXPECT(jrOffer[sfLedgerEntryType.fieldName] == jss::Offer);
            BEAST_EXPECT(jrOffer[sfOwnerNode.fieldName] == "0");
            BEAST_EXPECT(
                jrOffer[jss::TakerGets] == usd(10).value().getJson(JsonOptions::Values::None));
            BEAST_EXPECT(
                jrOffer[jss::TakerPays] == XRP(10).value().getJson(JsonOptions::Values::None));
            BEAST_EXPECT(jrOffer[sfDomainID.jsonName].asString() == to_string(domainID));
            BEAST_EXPECT(jrOffer[sfAdditionalBooks.jsonName].size() == 1);
        };

        // book_offers: open book returns hybrid offer
        {
            json::Value jvParams;
            jvParams[jss::taker] = env.master.human();
            jvParams[jss::taker_pays][jss::currency] = "XRP";
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_gets][jss::currency] = "USD";
            jvParams[jss::taker_gets][jss::issuer] = gw.human();

            auto jv = wsc->invoke("book_offers", jvParams);
            auto jrr = jv[jss::result];
            checkBookOffers(jrr);
        }

        auto checkSubBooks = [&](json::Value const& jv) {
            BEAST_EXPECT(
                jv[jss::result].isMember(jss::offers) && jv[jss::result][jss::offers].size() == 1);
            BEAST_EXPECT(
                jv[jss::result][jss::offers][0u][jss::TakerGets] ==
                usd(10).value().getJson(JsonOptions::Values::None));
            BEAST_EXPECT(
                jv[jss::result][jss::offers][0u][jss::TakerPays] ==
                XRP(10).value().getJson(JsonOptions::Values::None));
            BEAST_EXPECT(
                jv[jss::result][jss::offers][0u][sfDomainID.jsonName].asString() ==
                to_string(domainID));
        };

        // book_offers: requesting domain book returns hybrid offer
        {
            json::Value jvParams;
            jvParams[jss::taker] = env.master.human();
            jvParams[jss::taker_pays][jss::currency] = "XRP";
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_gets][jss::currency] = "USD";
            jvParams[jss::taker_gets][jss::issuer] = gw.human();
            jvParams[jss::domain] = to_string(domainID);

            auto jv = wsc->invoke("book_offers", jvParams);
            auto jrr = jv[jss::result];
            checkBookOffers(jrr);
        }

        // subscribe to domain book should return hybrid offer
        {
            json::Value books;
            books[jss::books] = json::ValueType::Array;
            {
                auto& j = books[jss::books].append(json::ValueType::Object);
                j[jss::snapshot] = true;
                j[jss::taker_pays][jss::currency] = "XRP";
                j[jss::taker_gets][jss::currency] = "USD";
                j[jss::taker_gets][jss::issuer] = gw.human();
                j[jss::domain] = to_string(domainID);
            }

            auto jv = wsc->invoke("subscribe", books);
            if (!BEAST_EXPECT(jv[jss::status] == "success"))
                return;
            checkSubBooks(jv);

            // RPC unsubscribe
            auto unsubJv = wsc->invoke("unsubscribe", books);
            if (wsc->version() == 2)
                BEAST_EXPECT(unsubJv[jss::status] == "success");
        }

        // subscribe to open book should return hybrid offer
        {
            json::Value books;
            books[jss::books] = json::ValueType::Array;
            {
                auto& j = books[jss::books].append(json::ValueType::Object);
                j[jss::snapshot] = true;
                j[jss::taker_pays][jss::currency] = "XRP";
                j[jss::taker_gets][jss::currency] = "USD";
                j[jss::taker_gets][jss::issuer] = gw.human();
            }

            auto jv = wsc->invoke("subscribe", books);
            if (!BEAST_EXPECT(jv[jss::status] == "success"))
                return;
            checkSubBooks(jv);
        }
    }

    void
    run() override
    {
        testOneSideEmptyBook();
        testOneSideOffersInBook();
        testBothSidesEmptyBook();
        testBothSidesOffersInBook();
        testMultipleBooksOneSideEmptyBook();
        testMultipleBooksOneSideOffersInBook();
        testMultipleBooksBothSidesEmptyBook();
        testMultipleBooksBothSidesOffersInBook();
        testTrackOffers();
        testCrossingSingleBookOffer();
        testCrossingMultiBookOffer();
        testBookOfferErrors();
        testBookOfferLimits(true);
        testBookOfferLimits(false);
        testTrackDomainOffer();
        testTrackHybridOffer();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(Book, rpc, xrpl, 1);

}  // namespace xrpl::test

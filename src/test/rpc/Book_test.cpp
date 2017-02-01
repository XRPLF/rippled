//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.
    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.
    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <BeastConfig.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/Indexes.h>
#include <test/jtx/WSClient.h>
#include <test/jtx.h>
#include <ripple/beast/unit_test.h>
#include <ripple/rpc/impl/Tuning.h>

namespace ripple {
namespace test {

class Book_test : public beast::unit_test::suite
{
    std::string getBookDir(jtx::Env & env, Issue const& in, Issue const& out)
    {
        std::string dir;
        auto uBookBase = getBookBase({in, out});
        auto uBookEnd = getQualityNext(uBookBase);
        auto view = env.closed();
        auto key = view->succ(uBookBase, uBookEnd);
        if (key)
        {
            auto sleOfferDir = view->read(keylet::page(key.value()));
            uint256 offerIndex;
            unsigned int bookEntry;
            cdirFirst(*view, sleOfferDir->key(), sleOfferDir, bookEntry, offerIndex, env.journal);
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
        auto USD = Account("alice")["USD"];
        auto wsc = makeWSClient(env.app().config());
        Json::Value books;

        {
            // RPC subscribe to books stream
            books[jss::books] = Json::arrayValue;
            {
                auto &j = books[jss::books].append(Json::objectValue);
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
            if(! BEAST_EXPECT(jv[jss::status] == "success"))
                return;
            BEAST_EXPECT(jv[jss::result].isMember(jss::offers) &&
                jv[jss::result][jss::offers].size() == 0);
            BEAST_EXPECT(! jv[jss::result].isMember(jss::asks));
            BEAST_EXPECT(! jv[jss::result].isMember(jss::bids));
        }

        {
            // Create an ask: TakerPays 700, TakerGets 100/USD
            env(offer("alice", XRP(700), USD(100)),
                require(owners("alice", 1)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    auto const& t = jv[jss::transaction];
                    return t[jss::TransactionType] == "OfferCreate" &&
                        t[jss::TakerGets] == USD(100).value().getJson(0) &&
                            t[jss::TakerPays] == XRP(700).value().getJson(0);
                }));
        }

        {
            // Create a bid: TakerPays 100/USD, TakerGets 75
            env(offer("alice", USD(100), XRP(75)),
                require(owners("alice", 2)));
            env.close();
            BEAST_EXPECT(! wsc->getMsg(10ms));
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
        auto USD = Account("alice")["USD"];
        auto wsc = makeWSClient(env.app().config());
        Json::Value books;

        // Create an ask: TakerPays 500, TakerGets 100/USD
        env(offer("alice", XRP(500), USD(100)),
            require(owners("alice", 1)));

        // Create a bid: TakerPays 100/USD, TakerGets 200
        env(offer("alice", USD(100), XRP(200)),
            require(owners("alice", 2)));
        env.close();

        {
            // RPC subscribe to books stream
            books[jss::books] = Json::arrayValue;
            {
                auto &j = books[jss::books].append(Json::objectValue);
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
            if(! BEAST_EXPECT(jv[jss::status] == "success"))
                return;
            BEAST_EXPECT(jv[jss::result].isMember(jss::offers) &&
                jv[jss::result][jss::offers].size() == 1);
            BEAST_EXPECT(jv[jss::result][jss::offers][0u][jss::TakerGets] ==
                XRP(200).value().getJson(0));
            BEAST_EXPECT(jv[jss::result][jss::offers][0u][jss::TakerPays] ==
                USD(100).value().getJson(0));
            BEAST_EXPECT(! jv[jss::result].isMember(jss::asks));
            BEAST_EXPECT(! jv[jss::result].isMember(jss::bids));
        }

        {
            // Create an ask: TakerPays 700, TakerGets 100/USD
            env(offer("alice", XRP(700), USD(100)),
                require(owners("alice", 3)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    auto const& t = jv[jss::transaction];
                    return t[jss::TransactionType] == "OfferCreate" &&
                        t[jss::TakerGets] == USD(100).value().getJson(0) &&
                            t[jss::TakerPays] == XRP(700).value().getJson(0);
                }));
        }

        {
            // Create a bid: TakerPays 100/USD, TakerGets 75
            env(offer("alice", USD(100), XRP(75)),
                require(owners("alice", 4)));
            env.close();
            BEAST_EXPECT(! wsc->getMsg(10ms));
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
        auto USD = Account("alice")["USD"];
        auto wsc = makeWSClient(env.app().config());
        Json::Value books;

        {
            // RPC subscribe to books stream
            books[jss::books] = Json::arrayValue;
            {
                auto &j = books[jss::books].append(Json::objectValue);
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
            if(! BEAST_EXPECT(jv[jss::status] == "success"))
                return;
            BEAST_EXPECT(jv[jss::result].isMember(jss::asks) &&
                jv[jss::result][jss::asks].size() == 0);
            BEAST_EXPECT(jv[jss::result].isMember(jss::bids) &&
                jv[jss::result][jss::bids].size() == 0);
            BEAST_EXPECT(! jv[jss::result].isMember(jss::offers));
        }

        {
            // Create an ask: TakerPays 700, TakerGets 100/USD
            env(offer("alice", XRP(700), USD(100)),
                require(owners("alice", 1)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    auto const& t = jv[jss::transaction];
                    return t[jss::TransactionType] == "OfferCreate" &&
                        t[jss::TakerGets] == USD(100).value().getJson(0) &&
                            t[jss::TakerPays] == XRP(700).value().getJson(0);
                }));
        }

        {
            // Create a bid: TakerPays 100/USD, TakerGets 75
            env(offer("alice", USD(100), XRP(75)),
                require(owners("alice", 2)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    auto const& t = jv[jss::transaction];
                    return t[jss::TransactionType] == "OfferCreate" &&
                        t[jss::TakerGets] == XRP(75).value().getJson(0) &&
                            t[jss::TakerPays] == USD(100).value().getJson(0);
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
        auto USD = Account("alice")["USD"];
        auto wsc = makeWSClient(env.app().config());
        Json::Value books;

        // Create an ask: TakerPays 500, TakerGets 100/USD
        env(offer("alice", XRP(500), USD(100)),
            require(owners("alice", 1)));

        // Create a bid: TakerPays 100/USD, TakerGets 200
        env(offer("alice", USD(100), XRP(200)),
            require(owners("alice", 2)));
        env.close();

        {
            // RPC subscribe to books stream
            books[jss::books] = Json::arrayValue;
            {
                auto &j = books[jss::books].append(Json::objectValue);
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
            if(! BEAST_EXPECT(jv[jss::status] == "success"))
                return;
            BEAST_EXPECT(jv[jss::result].isMember(jss::asks) &&
                jv[jss::result][jss::asks].size() == 1);
            BEAST_EXPECT(jv[jss::result].isMember(jss::bids) &&
                jv[jss::result][jss::bids].size() == 1);
            BEAST_EXPECT(jv[jss::result][jss::asks][0u][jss::TakerGets] ==
                USD(100).value().getJson(0));
            BEAST_EXPECT(jv[jss::result][jss::asks][0u][jss::TakerPays] ==
                XRP(500).value().getJson(0));
            BEAST_EXPECT(jv[jss::result][jss::bids][0u][jss::TakerGets] ==
                XRP(200).value().getJson(0));
            BEAST_EXPECT(jv[jss::result][jss::bids][0u][jss::TakerPays] ==
                USD(100).value().getJson(0));
            BEAST_EXPECT(! jv[jss::result].isMember(jss::offers));
        }

        {
            // Create an ask: TakerPays 700, TakerGets 100/USD
            env(offer("alice", XRP(700), USD(100)),
                require(owners("alice", 3)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    auto const& t = jv[jss::transaction];
                    return t[jss::TransactionType] == "OfferCreate" &&
                        t[jss::TakerGets] == USD(100).value().getJson(0) &&
                            t[jss::TakerPays] == XRP(700).value().getJson(0);
                }));
        }

        {
            // Create a bid: TakerPays 100/USD, TakerGets 75
            env(offer("alice", USD(100), XRP(75)),
                require(owners("alice", 4)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    auto const& t = jv[jss::transaction];
                    return t[jss::TransactionType] == "OfferCreate" &&
                        t[jss::TakerGets] == XRP(75).value().getJson(0) &&
                            t[jss::TakerPays] == USD(100).value().getJson(0);
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
        auto USD = Account("alice")["USD"];
        auto CNY = Account("alice")["CNY"];
        auto JPY = Account("alice")["JPY"];
        auto wsc = makeWSClient(env.app().config());
        Json::Value books;

        {
            // RPC subscribe to books stream
            books[jss::books] = Json::arrayValue;
            {
                auto &j = books[jss::books].append(Json::objectValue);
                j[jss::snapshot] = true;
                j[jss::taker_gets][jss::currency] = "XRP";
                j[jss::taker_pays][jss::currency] = "USD";
                j[jss::taker_pays][jss::issuer] = Account("alice").human();
            }
            {
                auto &j = books[jss::books].append(Json::objectValue);
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
            if(! BEAST_EXPECT(jv[jss::status] == "success"))
                return;
            BEAST_EXPECT(jv[jss::result].isMember(jss::offers) &&
                jv[jss::result][jss::offers].size() == 0);
            BEAST_EXPECT(! jv[jss::result].isMember(jss::asks));
            BEAST_EXPECT(! jv[jss::result].isMember(jss::bids));
        }

        {
            // Create an ask: TakerPays 700, TakerGets 100/USD
            env(offer("alice", XRP(700), USD(100)),
                require(owners("alice", 1)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    auto const& t = jv[jss::transaction];
                    return t[jss::TransactionType] == "OfferCreate" &&
                        t[jss::TakerGets] == USD(100).value().getJson(0) &&
                            t[jss::TakerPays] == XRP(700).value().getJson(0);
                }));
        }

        {
            // Create a bid: TakerPays 100/USD, TakerGets 75
            env(offer("alice", USD(100), XRP(75)),
                require(owners("alice", 2)));
            env.close();
            BEAST_EXPECT(! wsc->getMsg(10ms));
        }

        {
            // Create an ask: TakerPays 700/CNY, TakerGets 100/JPY
            env(offer("alice", CNY(700), JPY(100)),
                require(owners("alice", 3)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    auto const& t = jv[jss::transaction];
                    return t[jss::TransactionType] == "OfferCreate" &&
                        t[jss::TakerGets] == JPY(100).value().getJson(0) &&
                            t[jss::TakerPays] == CNY(700).value().getJson(0);
                }));
        }

        {
            // Create a bid: TakerPays 100/JPY, TakerGets 75/CNY
            env(offer("alice", JPY(100), CNY(75)),
                require(owners("alice", 4)));
            env.close();
            BEAST_EXPECT(! wsc->getMsg(10ms));
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
        auto USD = Account("alice")["USD"];
        auto CNY = Account("alice")["CNY"];
        auto JPY = Account("alice")["JPY"];
        auto wsc = makeWSClient(env.app().config());
        Json::Value books;

        // Create an ask: TakerPays 500, TakerGets 100/USD
        env(offer("alice", XRP(500), USD(100)),
            require(owners("alice", 1)));

        // Create an ask: TakerPays 500/CNY, TakerGets 100/JPY
        env(offer("alice", CNY(500), JPY(100)),
            require(owners("alice", 2)));

        // Create a bid: TakerPays 100/USD, TakerGets 200
        env(offer("alice", USD(100), XRP(200)),
            require(owners("alice", 3)));

        // Create a bid: TakerPays 100/JPY, TakerGets 200/CNY
        env(offer("alice", JPY(100), CNY(200)),
            require(owners("alice", 4)));
        env.close();

        {
            // RPC subscribe to books stream
            books[jss::books] = Json::arrayValue;
            {
                auto &j = books[jss::books].append(Json::objectValue);
                j[jss::snapshot] = true;
                j[jss::taker_gets][jss::currency] = "XRP";
                j[jss::taker_pays][jss::currency] = "USD";
                j[jss::taker_pays][jss::issuer] = Account("alice").human();
            }
            {
                auto &j = books[jss::books].append(Json::objectValue);
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
            if(! BEAST_EXPECT(jv[jss::status] == "success"))
                return;
            BEAST_EXPECT(jv[jss::result].isMember(jss::offers) &&
                jv[jss::result][jss::offers].size() == 2);
            BEAST_EXPECT(jv[jss::result][jss::offers][0u][jss::TakerGets] ==
                XRP(200).value().getJson(0));
            BEAST_EXPECT(jv[jss::result][jss::offers][0u][jss::TakerPays] ==
                USD(100).value().getJson(0));
            BEAST_EXPECT(jv[jss::result][jss::offers][1u][jss::TakerGets] ==
                CNY(200).value().getJson(0));
            BEAST_EXPECT(jv[jss::result][jss::offers][1u][jss::TakerPays] ==
                JPY(100).value().getJson(0));
            BEAST_EXPECT(! jv[jss::result].isMember(jss::asks));
            BEAST_EXPECT(! jv[jss::result].isMember(jss::bids));
        }

        {
            // Create an ask: TakerPays 700, TakerGets 100/USD
            env(offer("alice", XRP(700), USD(100)),
                require(owners("alice", 5)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    auto const& t = jv[jss::transaction];
                    return t[jss::TransactionType] == "OfferCreate" &&
                        t[jss::TakerGets] == USD(100).value().getJson(0) &&
                            t[jss::TakerPays] == XRP(700).value().getJson(0);
                }));
        }

        {
            // Create a bid: TakerPays 100/USD, TakerGets 75
            env(offer("alice", USD(100), XRP(75)),
                require(owners("alice", 6)));
            env.close();
            BEAST_EXPECT(! wsc->getMsg(10ms));
        }

        {
            // Create an ask: TakerPays 700/CNY, TakerGets 100/JPY
            env(offer("alice", CNY(700), JPY(100)),
                require(owners("alice", 7)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    auto const& t = jv[jss::transaction];
                    return t[jss::TransactionType] == "OfferCreate" &&
                        t[jss::TakerGets] == JPY(100).value().getJson(0) &&
                            t[jss::TakerPays] == CNY(700).value().getJson(0);
                }));
        }

        {
            // Create a bid: TakerPays 100/JPY, TakerGets 75/CNY
            env(offer("alice", JPY(100), CNY(75)),
                require(owners("alice", 8)));
            env.close();
            BEAST_EXPECT(! wsc->getMsg(10ms));
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
        auto USD = Account("alice")["USD"];
        auto CNY = Account("alice")["CNY"];
        auto JPY = Account("alice")["JPY"];
        auto wsc = makeWSClient(env.app().config());
        Json::Value books;

        {
            // RPC subscribe to books stream
            books[jss::books] = Json::arrayValue;
            {
                auto &j = books[jss::books].append(Json::objectValue);
                j[jss::snapshot] = true;
                j[jss::both] = true;
                j[jss::taker_gets][jss::currency] = "XRP";
                j[jss::taker_pays][jss::currency] = "USD";
                j[jss::taker_pays][jss::issuer] = Account("alice").human();
            }
            {
                auto &j = books[jss::books].append(Json::objectValue);
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
            if(! BEAST_EXPECT(jv[jss::status] == "success"))
                return;
            BEAST_EXPECT(jv[jss::result].isMember(jss::asks) &&
                jv[jss::result][jss::asks].size() == 0);
            BEAST_EXPECT(jv[jss::result].isMember(jss::bids) &&
                jv[jss::result][jss::bids].size() == 0);
            BEAST_EXPECT(! jv[jss::result].isMember(jss::offers));
        }

        {
            // Create an ask: TakerPays 700, TakerGets 100/USD
            env(offer("alice", XRP(700), USD(100)),
                require(owners("alice", 1)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    auto const& t = jv[jss::transaction];
                    return t[jss::TransactionType] == "OfferCreate" &&
                        t[jss::TakerGets] == USD(100).value().getJson(0) &&
                            t[jss::TakerPays] == XRP(700).value().getJson(0);
                }));
        }

        {
            // Create a bid: TakerPays 100/USD, TakerGets 75
            env(offer("alice", USD(100), XRP(75)),
                require(owners("alice", 2)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    auto const& t = jv[jss::transaction];
                    return t[jss::TransactionType] == "OfferCreate" &&
                        t[jss::TakerGets] == XRP(75).value().getJson(0) &&
                            t[jss::TakerPays] == USD(100).value().getJson(0);
                }));
        }

        {
            // Create an ask: TakerPays 700/CNY, TakerGets 100/JPY
            env(offer("alice", CNY(700), JPY(100)),
                require(owners("alice", 3)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    auto const& t = jv[jss::transaction];
                    return t[jss::TransactionType] == "OfferCreate" &&
                        t[jss::TakerGets] == JPY(100).value().getJson(0) &&
                            t[jss::TakerPays] == CNY(700).value().getJson(0);
                }));
        }

        {
            // Create a bid: TakerPays 100/JPY, TakerGets 75/CNY
            env(offer("alice", JPY(100), CNY(75)),
                require(owners("alice", 4)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    auto const& t = jv[jss::transaction];
                    return t[jss::TransactionType] == "OfferCreate" &&
                        t[jss::TakerGets] == CNY(75).value().getJson(0) &&
                            t[jss::TakerPays] == JPY(100).value().getJson(0);
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
        auto USD = Account("alice")["USD"];
        auto CNY = Account("alice")["CNY"];
        auto JPY = Account("alice")["JPY"];
        auto wsc = makeWSClient(env.app().config());
        Json::Value books;

        // Create an ask: TakerPays 500, TakerGets 100/USD
        env(offer("alice", XRP(500), USD(100)),
            require(owners("alice", 1)));

        // Create an ask: TakerPays 500/CNY, TakerGets 100/JPY
        env(offer("alice", CNY(500), JPY(100)),
            require(owners("alice", 2)));

        // Create a bid: TakerPays 100/USD, TakerGets 200
        env(offer("alice", USD(100), XRP(200)),
            require(owners("alice", 3)));

        // Create a bid: TakerPays 100/JPY, TakerGets 200/CNY
        env(offer("alice", JPY(100), CNY(200)),
            require(owners("alice", 4)));
        env.close();

        {
            // RPC subscribe to books stream
            books[jss::books] = Json::arrayValue;
            {
                auto &j = books[jss::books].append(Json::objectValue);
                j[jss::snapshot] = true;
                j[jss::both] = true;
                j[jss::taker_gets][jss::currency] = "XRP";
                j[jss::taker_pays][jss::currency] = "USD";
                j[jss::taker_pays][jss::issuer] = Account("alice").human();
            }
            // RPC subscribe to books stream
            {
                auto &j = books[jss::books].append(Json::objectValue);
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
            if(! BEAST_EXPECT(jv[jss::status] == "success"))
                return;
            BEAST_EXPECT(jv[jss::result].isMember(jss::asks) &&
                jv[jss::result][jss::asks].size() == 2);
            BEAST_EXPECT(jv[jss::result].isMember(jss::bids) &&
                jv[jss::result][jss::bids].size() == 2);
            BEAST_EXPECT(jv[jss::result][jss::asks][0u][jss::TakerGets] ==
                USD(100).value().getJson(0));
            BEAST_EXPECT(jv[jss::result][jss::asks][0u][jss::TakerPays] ==
                XRP(500).value().getJson(0));
            BEAST_EXPECT(jv[jss::result][jss::asks][1u][jss::TakerGets] ==
                JPY(100).value().getJson(0));
            BEAST_EXPECT(jv[jss::result][jss::asks][1u][jss::TakerPays] ==
                CNY(500).value().getJson(0));
            BEAST_EXPECT(jv[jss::result][jss::bids][0u][jss::TakerGets] ==
                XRP(200).value().getJson(0));
            BEAST_EXPECT(jv[jss::result][jss::bids][0u][jss::TakerPays] ==
                USD(100).value().getJson(0));
            BEAST_EXPECT(jv[jss::result][jss::bids][1u][jss::TakerGets] ==
                CNY(200).value().getJson(0));
            BEAST_EXPECT(jv[jss::result][jss::bids][1u][jss::TakerPays] ==
                JPY(100).value().getJson(0));
            BEAST_EXPECT(! jv[jss::result].isMember(jss::offers));
        }

        {
            // Create an ask: TakerPays 700, TakerGets 100/USD
            env(offer("alice", XRP(700), USD(100)),
                require(owners("alice", 5)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    auto const& t = jv[jss::transaction];
                    return t[jss::TransactionType] == "OfferCreate" &&
                        t[jss::TakerGets] == USD(100).value().getJson(0) &&
                            t[jss::TakerPays] == XRP(700).value().getJson(0);
                }));
        }

        {
            // Create a bid: TakerPays 100/USD, TakerGets 75
            env(offer("alice", USD(100), XRP(75)),
                require(owners("alice", 6)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    auto const& t = jv[jss::transaction];
                    return t[jss::TransactionType] == "OfferCreate" &&
                        t[jss::TakerGets] == XRP(75).value().getJson(0) &&
                            t[jss::TakerPays] == USD(100).value().getJson(0);
                }));
        }

        {
            // Create an ask: TakerPays 700/CNY, TakerGets 100/JPY
            env(offer("alice", CNY(700), JPY(100)),
                require(owners("alice", 7)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    auto const& t = jv[jss::transaction];
                    return t[jss::TransactionType] == "OfferCreate" &&
                        t[jss::TakerGets] == JPY(100).value().getJson(0) &&
                            t[jss::TakerPays] == CNY(700).value().getJson(0);
                }));
        }

        {
            // Create a bid: TakerPays 100/JPY, TakerGets 75/CNY
            env(offer("alice", JPY(100), CNY(75)),
                require(owners("alice", 8)));
            env.close();

            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    auto const& t = jv[jss::transaction];
                    return t[jss::TransactionType] == "OfferCreate" &&
                        t[jss::TakerGets] == CNY(75).value().getJson(0) &&
                            t[jss::TakerPays] == JPY(100).value().getJson(0);
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
        Account gw {"gw"};
        Account alice {"alice"};
        Account bob {"bob"};
        auto wsc = makeWSClient(env.app().config());
        env.fund(XRP(20000), alice, bob, gw);
        env.close();
        auto USD = gw["USD"];

        Json::Value books;
        {
            books[jss::books] = Json::arrayValue;
            {
                auto &j = books[jss::books].append(Json::objectValue);
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
            if(! BEAST_EXPECT(jv[jss::status] == "success"))
                return;
            BEAST_EXPECT(jv[jss::result].isMember(jss::offers) &&
                jv[jss::result][jss::offers].size() == 0);
            BEAST_EXPECT(! jv[jss::result].isMember(jss::asks));
            BEAST_EXPECT(! jv[jss::result].isMember(jss::bids));
        }

        env(rate(gw, 1.1));
        env.close();
        env.trust(USD(1000), alice);
        env.trust(USD(1000), bob);
        env(pay(gw, alice, USD(100)));
        env(pay(gw, bob, USD(50)));
        env(offer(alice, XRP(4000), USD(10)));
        env.close();

        Json::Value jvParams;
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
        BEAST_EXPECT(jrOffer[sfBookDirectory.fieldName] ==
            getBookDir(env, XRP, USD.issue()));
        BEAST_EXPECT(jrOffer[sfBookNode.fieldName] == "0000000000000000");
        BEAST_EXPECT(jrOffer[jss::Flags] == 0);
        BEAST_EXPECT(jrOffer[sfLedgerEntryType.fieldName] == "Offer");
        BEAST_EXPECT(jrOffer[sfOwnerNode.fieldName] == "0000000000000000");
        BEAST_EXPECT(jrOffer[sfSequence.fieldName] == 3);
        BEAST_EXPECT(jrOffer[jss::TakerGets] == USD(10).value().getJson(0));
        BEAST_EXPECT(jrOffer[jss::TakerPays] == XRP(4000).value().getJson(0));
        BEAST_EXPECT(jrOffer[jss::owner_funds] == "100");
        BEAST_EXPECT(jrOffer[jss::quality] == "400000000");

        BEAST_EXPECT(wsc->findMsg(5s,
            [&](auto const& jv)
            {
                auto const& t = jv[jss::transaction];
                return t[jss::TransactionType] == "OfferCreate" &&
                       t[jss::TakerGets] == USD(10).value().getJson(0) &&
                       t[jss::owner_funds] == "100" &&
                       t[jss::TakerPays] == XRP(4000).value().getJson(0);
            }));

        env(offer(bob, XRP(2000), USD(5)));
        env.close();

        BEAST_EXPECT(wsc->findMsg(5s,
            [&](auto const& jv)
            {
                auto const& t = jv[jss::transaction];
                return t[jss::TransactionType] == "OfferCreate" &&
                       t[jss::TakerGets] == USD(5).value().getJson(0) &&
                       t[jss::owner_funds] == "50" &&
                       t[jss::TakerPays] == XRP(2000).value().getJson(0);
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
        BEAST_EXPECT(jrNextOffer[sfBookDirectory.fieldName] ==
            getBookDir(env, XRP, USD.issue()));
        BEAST_EXPECT(jrNextOffer[sfBookNode.fieldName] == "0000000000000000");
        BEAST_EXPECT(jrNextOffer[jss::Flags] == 0);
        BEAST_EXPECT(jrNextOffer[sfLedgerEntryType.fieldName] == "Offer");
        BEAST_EXPECT(jrNextOffer[sfOwnerNode.fieldName] == "0000000000000000");
        BEAST_EXPECT(jrNextOffer[sfSequence.fieldName] == 3);
        BEAST_EXPECT(jrNextOffer[jss::TakerGets] == USD(5).value().getJson(0));
        BEAST_EXPECT(jrNextOffer[jss::TakerPays] ==
			XRP(2000).value().getJson(0));
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

    void
    testBookOfferErrors()
    {
        testcase("BookOffersRPC Errors");
        using namespace jtx;
        Env env(*this);
        Account gw {"gw"};
        Account alice {"alice"};
        env.fund(XRP(10000), alice, gw);
        env.close();
        auto USD = gw["USD"];

        {
            Json::Value jvParams;
            jvParams[jss::ledger_index] = 10u;
            auto const jrr = env.rpc(
                "json", "book_offers", to_string(jvParams)) [jss::result];
            BEAST_EXPECT(jrr[jss::error] == "lgrNotFound");
            BEAST_EXPECT(jrr[jss::error_message] == "ledgerNotFound");
        }

        {
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            auto const jrr = env.rpc(
                "json", "book_offers", to_string(jvParams)) [jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] ==
                "Missing field 'taker_pays'.");
        }

        {
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays] = Json::objectValue;
            auto const jrr = env.rpc(
                "json", "book_offers", to_string(jvParams)) [jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] ==
                "Missing field 'taker_gets'.");
        }

        {
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays] = "not an object";
            jvParams[jss::taker_gets] = Json::objectValue;
            auto const jrr = env.rpc(
                "json", "book_offers", to_string(jvParams)) [jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] ==
                "Invalid field 'taker_pays', not object.");
        }

        {
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays] = Json::objectValue;
            jvParams[jss::taker_gets] = "not an object";
            auto const jrr = env.rpc(
                "json", "book_offers", to_string(jvParams)) [jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] ==
                "Invalid field 'taker_gets', not object.");
        }

        {
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays] = Json::objectValue;
            jvParams[jss::taker_gets] = Json::objectValue;
            auto const jrr = env.rpc(
                "json", "book_offers", to_string(jvParams)) [jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] ==
                "Missing field 'taker_pays.currency'.");
        }

        {
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays][jss::currency] = 1;
            jvParams[jss::taker_gets] = Json::objectValue;
            auto const jrr = env.rpc(
                "json", "book_offers", to_string(jvParams)) [jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] ==
                "Invalid field 'taker_pays.currency', not string.");
        }

        {
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays][jss::currency] = "XRP";
            jvParams[jss::taker_gets] = Json::objectValue;
            auto const jrr = env.rpc(
                "json", "book_offers", to_string(jvParams)) [jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] ==
                "Missing field 'taker_gets.currency'.");
        }

        {
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays][jss::currency] = "XRP";
            jvParams[jss::taker_gets][jss::currency] = 1;
            auto const jrr = env.rpc(
                "json", "book_offers", to_string(jvParams)) [jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] ==
                "Invalid field 'taker_gets.currency', not string.");
        }

        {
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays][jss::currency] = "NOT_VALID";
            jvParams[jss::taker_gets][jss::currency] = "XRP";
            auto const jrr = env.rpc(
                "json", "book_offers", to_string(jvParams)) [jss::result];
            BEAST_EXPECT(jrr[jss::error] == "srcCurMalformed");
            BEAST_EXPECT(jrr[jss::error_message] ==
                "Invalid field 'taker_pays.currency', bad currency.");
        }

        {
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays][jss::currency] = "XRP";
            jvParams[jss::taker_gets][jss::currency] = "NOT_VALID";
            auto const jrr = env.rpc(
                "json", "book_offers", to_string(jvParams)) [jss::result];
            BEAST_EXPECT(jrr[jss::error] == "dstAmtMalformed");
            BEAST_EXPECT(jrr[jss::error_message] ==
                "Invalid field 'taker_gets.currency', bad currency.");
        }

        {
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays][jss::currency] = "XRP";
            jvParams[jss::taker_gets][jss::currency] = "USD";
            jvParams[jss::taker_gets][jss::issuer]   = 1;
            auto const jrr = env.rpc(
                "json", "book_offers", to_string(jvParams)) [jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] ==
                "Invalid field 'taker_gets.issuer', not string.");
        }

        {
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays][jss::currency] = "XRP";
            jvParams[jss::taker_pays][jss::issuer]   = 1;
            jvParams[jss::taker_gets][jss::currency] = "USD";
            auto const jrr = env.rpc(
                "json", "book_offers", to_string(jvParams)) [jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] ==
                "Invalid field 'taker_pays.issuer', not string.");
        }

        {
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays][jss::currency] = "XRP";
            jvParams[jss::taker_pays][jss::issuer]   = gw.human() + "DEAD";
            jvParams[jss::taker_gets][jss::currency] = "USD";
            auto const jrr = env.rpc(
                "json", "book_offers", to_string(jvParams)) [jss::result];
            BEAST_EXPECT(jrr[jss::error] == "srcIsrMalformed");
            BEAST_EXPECT(jrr[jss::error_message] ==
                "Invalid field 'taker_pays.issuer', bad issuer.");
        }

        {
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays][jss::currency] = "XRP";
            jvParams[jss::taker_pays][jss::issuer]   = toBase58(noAccount());
            jvParams[jss::taker_gets][jss::currency] = "USD";
            auto const jrr = env.rpc(
                "json", "book_offers", to_string(jvParams)) [jss::result];
            BEAST_EXPECT(jrr[jss::error] == "srcIsrMalformed");
            BEAST_EXPECT(jrr[jss::error_message] ==
                "Invalid field 'taker_pays.issuer', bad issuer account one.");
        }

        {
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays][jss::currency] = "XRP";
            jvParams[jss::taker_gets][jss::currency] = "USD";
            jvParams[jss::taker_gets][jss::issuer]   = gw.human() + "DEAD";
            auto const jrr = env.rpc(
                "json", "book_offers", to_string(jvParams)) [jss::result];
            BEAST_EXPECT(jrr[jss::error] == "dstIsrMalformed");
            BEAST_EXPECT(jrr[jss::error_message] ==
                "Invalid field 'taker_gets.issuer', bad issuer.");
        }

        {
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays][jss::currency] = "XRP";
            jvParams[jss::taker_gets][jss::currency] = "USD";
            jvParams[jss::taker_gets][jss::issuer]   = toBase58(noAccount());
            auto const jrr = env.rpc(
                "json", "book_offers", to_string(jvParams)) [jss::result];
            BEAST_EXPECT(jrr[jss::error] == "dstIsrMalformed");
            BEAST_EXPECT(jrr[jss::error_message] ==
                "Invalid field 'taker_gets.issuer', bad issuer account one.");
        }

        {
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays][jss::currency] = "XRP";
            jvParams[jss::taker_pays][jss::issuer]   = alice.human();
            jvParams[jss::taker_gets][jss::currency] = "USD";
            jvParams[jss::taker_gets][jss::issuer]   = gw.human();
            auto const jrr = env.rpc(
                "json", "book_offers", to_string(jvParams)) [jss::result];
            BEAST_EXPECT(jrr[jss::error] == "srcIsrMalformed");
            BEAST_EXPECT(jrr[jss::error_message] ==
                "Unneeded field 'taker_pays.issuer' "
                "for XRP currency specification.");
        }

        {
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays][jss::currency] = "USD";
            jvParams[jss::taker_pays][jss::issuer]   = toBase58(xrpAccount());
            jvParams[jss::taker_gets][jss::currency] = "USD";
            jvParams[jss::taker_gets][jss::issuer]   = gw.human();
            auto const jrr = env.rpc(
                "json", "book_offers", to_string(jvParams)) [jss::result];
            BEAST_EXPECT(jrr[jss::error] == "srcIsrMalformed");
            BEAST_EXPECT(jrr[jss::error_message] ==
                "Invalid field 'taker_pays.issuer', expected non-XRP issuer.");
        }

        {
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker] = 1;
            jvParams[jss::taker_pays][jss::currency] = "XRP";
            jvParams[jss::taker_gets][jss::currency] = "USD";
            jvParams[jss::taker_gets][jss::issuer]   = gw.human();
            auto const jrr = env.rpc(
                "json", "book_offers", to_string(jvParams)) [jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] ==
                "Invalid field 'taker', not string.");
        }

        {
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker] = env.master.human() + "DEAD";
            jvParams[jss::taker_pays][jss::currency] = "XRP";
            jvParams[jss::taker_gets][jss::currency] = "USD";
            jvParams[jss::taker_gets][jss::issuer]   = gw.human();
            auto const jrr = env.rpc(
                "json", "book_offers", to_string(jvParams)) [jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] ==
                "Invalid field 'taker'.");
        }

        {
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker] = env.master.human();
            jvParams[jss::taker_pays][jss::currency] = "USD";
            jvParams[jss::taker_pays][jss::issuer]   = gw.human();
            jvParams[jss::taker_gets][jss::currency] = "USD";
            jvParams[jss::taker_gets][jss::issuer]   = gw.human();
            auto const jrr = env.rpc(
                "json", "book_offers", to_string(jvParams)) [jss::result];
            BEAST_EXPECT(jrr[jss::error] == "badMarket");
            BEAST_EXPECT(jrr[jss::error_message] == "No such market.");
        }

        {
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker] = env.master.human();
            jvParams[jss::limit]   = "0"; // NOT an integer
            jvParams[jss::taker_pays][jss::currency] = "XRP";
            jvParams[jss::taker_gets][jss::currency] = "USD";
            jvParams[jss::taker_gets][jss::issuer]   = gw.human();
            auto const jrr = env.rpc(
                "json", "book_offers", to_string(jvParams)) [jss::result];
            BEAST_EXPECT(jrr[jss::error] == "invalidParams");
            BEAST_EXPECT(jrr[jss::error_message] ==
                "Invalid field 'limit', not unsigned integer.");
        }

        {
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays][jss::currency] = "USD";
            jvParams[jss::taker_pays][jss::issuer] = gw.human();
            jvParams[jss::taker_gets][jss::currency] = "USD";
            auto const jrr = env.rpc(
                "json", "book_offers", to_string(jvParams)) [jss::result];
            BEAST_EXPECT(jrr[jss::error] == "dstIsrMalformed");
            BEAST_EXPECT(jrr[jss::error_message] ==
                "Invalid field 'taker_gets.issuer', "
                "expected non-XRP issuer.");
        }

        {
            Json::Value jvParams;
            jvParams[jss::ledger_index] = "validated";
            jvParams[jss::taker_pays][jss::currency] = "USD";
            jvParams[jss::taker_pays][jss::issuer]   = gw.human();
            jvParams[jss::taker_gets][jss::currency] = "XRP";
            jvParams[jss::taker_gets][jss::issuer]   = gw.human();
            auto const jrr = env.rpc(
                "json", "book_offers", to_string(jvParams)) [jss::result];
            BEAST_EXPECT(jrr[jss::error] == "dstIsrMalformed");
            BEAST_EXPECT(jrr[jss::error_message] ==
                "Unneeded field 'taker_gets.issuer' "
                "for XRP currency specification.");
        }

    }

    void
    testBookOfferLimits(bool asAdmin)
    {
        testcase("BookOffer Limits");
        using namespace jtx;
        Env env {*this, std::make_unique<Config>(
            asAdmin ? defaultConf : nonAdminConf)};
        Account gw {"gw"};
        env.fund(XRP(200000), gw);
        env.close();
        auto USD = gw["USD"];

        for(auto i = 0; i <= RPC::Tuning::bookOffers.rmax; i++)
            env(offer(gw, XRP(50 + 1*i), USD(1.0 + 0.1*i)));
        env.close();

        Json::Value jvParams;
        jvParams[jss::limit] = 1;
        jvParams[jss::ledger_index] = "validated";
        jvParams[jss::taker_pays][jss::currency] = "XRP";
        jvParams[jss::taker_gets][jss::currency] = "USD";
        jvParams[jss::taker_gets][jss::issuer] = gw.human();
        auto jrr =
            env.rpc("json", "book_offers", to_string(jvParams)) [jss::result];
        BEAST_EXPECT(jrr[jss::offers].isArray());
        BEAST_EXPECT(jrr[jss::offers].size() == (asAdmin ? 1u : 0u));
        // NOTE - a marker field is not returned for this method

        jvParams[jss::limit] = 0u;
        jrr =
            env.rpc("json", "book_offers", to_string(jvParams)) [jss::result];
        BEAST_EXPECT(jrr[jss::offers].isArray());
        BEAST_EXPECT(jrr[jss::offers].size() == 0u);

        jvParams[jss::limit] = RPC::Tuning::bookOffers.rmax + 1;
        jrr =
            env.rpc("json", "book_offers", to_string(jvParams)) [jss::result];
        BEAST_EXPECT(jrr[jss::offers].isArray());
        BEAST_EXPECT(jrr[jss::offers].size() ==
                (asAdmin ?  RPC::Tuning::bookOffers.rmax + 1 : 0u));

        jvParams[jss::limit] = Json::nullValue;
        jrr =
            env.rpc("json", "book_offers", to_string(jvParams)) [jss::result];
        BEAST_EXPECT(jrr[jss::offers].isArray());
        BEAST_EXPECT(jrr[jss::offers].size() ==
                (asAdmin ?  RPC::Tuning::bookOffers.rdefault : 0u));
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
        testBookOfferErrors();
        testBookOfferLimits(true);
        testBookOfferLimits(false);
    }
};

BEAST_DEFINE_TESTSUITE(Book,app,ripple);

} // test
} // ripple


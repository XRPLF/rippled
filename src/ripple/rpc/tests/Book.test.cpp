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
#include <ripple/test/WSClient.h>
#include <ripple/test/jtx.h>
#include <beast/unit_test/suite.h>

namespace ripple {
namespace test {

class Book_test : public beast::unit_test::suite
{
public:
    void
    testOneSideEmptyBook()
    {
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
            expect(jv[jss::status] == "success");
            expect(jv[jss::result].isMember(jss::offers) &&
                jv[jss::result][jss::offers].size() == 0);
            expect(! jv[jss::result].isMember(jss::asks));
            expect(! jv[jss::result].isMember(jss::bids));
        }

        {
            // Create an ask: TakerPays 700, TakerGets 100/USD
            env(offer("alice", XRP(700), USD(100)),
                require(owners("alice", 1)));
            env.close();

            // Check stream update
            expect(wsc->findMsg(5s,
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
            expect(! wsc->getMsg(10ms));
        }

        // RPC unsubscribe
        expect(wsc->invoke("unsubscribe",
            books)[jss::status] == "success");
    }

    void
    testOneSideOffersInBook()
    {
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
            expect(jv[jss::status] == "success");
            expect(jv[jss::result].isMember(jss::offers) &&
                jv[jss::result][jss::offers].size() == 1);
            expect(jv[jss::result][jss::offers][0u][jss::TakerGets] ==
                XRP(200).value().getJson(0));
            expect(jv[jss::result][jss::offers][0u][jss::TakerPays] ==
                USD(100).value().getJson(0));
            expect(! jv[jss::result].isMember(jss::asks));
            expect(! jv[jss::result].isMember(jss::bids));
        }

        {
            // Create an ask: TakerPays 700, TakerGets 100/USD
            env(offer("alice", XRP(700), USD(100)),
                require(owners("alice", 3)));
            env.close();

            // Check stream update
            expect(wsc->findMsg(5s,
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
            expect(! wsc->getMsg(10ms));
        }

        // RPC unsubscribe
        expect(wsc->invoke("unsubscribe",
            books)[jss::status] == "success");
    }

    void
    testBothSidesEmptyBook()
    {
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
            expect(jv[jss::status] == "success");
            expect(jv[jss::result].isMember(jss::asks) &&
                jv[jss::result][jss::asks].size() == 0);
            expect(jv[jss::result].isMember(jss::bids) &&
                jv[jss::result][jss::bids].size() == 0);
            expect(! jv[jss::result].isMember(jss::offers));
        }

        {
            // Create an ask: TakerPays 700, TakerGets 100/USD
            env(offer("alice", XRP(700), USD(100)),
                require(owners("alice", 1)));
            env.close();

            // Check stream update
            expect(wsc->findMsg(5s,
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
            expect(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    auto const& t = jv[jss::transaction];
                    return t[jss::TransactionType] == "OfferCreate" &&
                        t[jss::TakerGets] == XRP(75).value().getJson(0) &&
                            t[jss::TakerPays] == USD(100).value().getJson(0);
                }));
        }

        // RPC unsubscribe
        expect(wsc->invoke("unsubscribe",
            books)[jss::status] == "success");
    }

    void
    testBothSidesOffersInBook()
    {
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
            expect(jv[jss::status] == "success");
            expect(jv[jss::result].isMember(jss::asks) &&
                jv[jss::result][jss::asks].size() == 1);
            expect(jv[jss::result].isMember(jss::bids) &&
                jv[jss::result][jss::bids].size() == 1);
            expect(jv[jss::result][jss::asks][0u][jss::TakerGets] ==
                USD(100).value().getJson(0));
            expect(jv[jss::result][jss::asks][0u][jss::TakerPays] ==
                XRP(500).value().getJson(0));
            expect(jv[jss::result][jss::bids][0u][jss::TakerGets] ==
                XRP(200).value().getJson(0));
            expect(jv[jss::result][jss::bids][0u][jss::TakerPays] ==
                USD(100).value().getJson(0));
            expect(! jv[jss::result].isMember(jss::offers));
        }

        {
            // Create an ask: TakerPays 700, TakerGets 100/USD
            env(offer("alice", XRP(700), USD(100)),
                require(owners("alice", 3)));
            env.close();

            // Check stream update
            expect(wsc->findMsg(5s,
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
            expect(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    auto const& t = jv[jss::transaction];
                    return t[jss::TransactionType] == "OfferCreate" &&
                        t[jss::TakerGets] == XRP(75).value().getJson(0) &&
                            t[jss::TakerPays] == USD(100).value().getJson(0);
                }));
        }

        // RPC unsubscribe
        expect(wsc->invoke("unsubscribe",
            books)[jss::status] == "success");
    }

    void
    testMultipleBooksOneSideEmptyBook()
    {
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
            expect(jv[jss::status] == "success");
            expect(jv[jss::result].isMember(jss::offers) &&
                jv[jss::result][jss::offers].size() == 0);
            expect(! jv[jss::result].isMember(jss::asks));
            expect(! jv[jss::result].isMember(jss::bids));
        }

        {
            // Create an ask: TakerPays 700, TakerGets 100/USD
            env(offer("alice", XRP(700), USD(100)),
                require(owners("alice", 1)));
            env.close();

            // Check stream update
            expect(wsc->findMsg(5s,
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
            expect(! wsc->getMsg(10ms));
        }

        {
            // Create an ask: TakerPays 700/CNY, TakerGets 100/JPY
            env(offer("alice", CNY(700), JPY(100)),
                require(owners("alice", 3)));
            env.close();

            // Check stream update
            expect(wsc->findMsg(5s,
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
            expect(! wsc->getMsg(10ms));
        }

        // RPC unsubscribe
        expect(wsc->invoke("unsubscribe",
            books)[jss::status] == "success");
    }

    void
    testMultipleBooksOneSideOffersInBook()
    {
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
            expect(jv[jss::status] == "success");
            expect(jv[jss::result].isMember(jss::offers) &&
                jv[jss::result][jss::offers].size() == 2);
            expect(jv[jss::result][jss::offers][0u][jss::TakerGets] ==
                XRP(200).value().getJson(0));
            expect(jv[jss::result][jss::offers][0u][jss::TakerPays] ==
                USD(100).value().getJson(0));
            expect(jv[jss::result][jss::offers][1u][jss::TakerGets] ==
                CNY(200).value().getJson(0));
            expect(jv[jss::result][jss::offers][1u][jss::TakerPays] ==
                JPY(100).value().getJson(0));
            expect(! jv[jss::result].isMember(jss::asks));
            expect(! jv[jss::result].isMember(jss::bids));
        }

        {
            // Create an ask: TakerPays 700, TakerGets 100/USD
            env(offer("alice", XRP(700), USD(100)),
                require(owners("alice", 5)));
            env.close();

            // Check stream update
            expect(wsc->findMsg(5s,
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
            expect(! wsc->getMsg(10ms));
        }

        {
            // Create an ask: TakerPays 700/CNY, TakerGets 100/JPY
            env(offer("alice", CNY(700), JPY(100)),
                require(owners("alice", 7)));
            env.close();

            // Check stream update
            expect(wsc->findMsg(5s,
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
            expect(! wsc->getMsg(10ms));
        }

        // RPC unsubscribe
        expect(wsc->invoke("unsubscribe",
            books)[jss::status] == "success");
    }

    void
    testMultipleBooksBothSidesEmptyBook()
    {
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
            expect(jv[jss::status] == "success");
            expect(jv[jss::result].isMember(jss::asks) &&
                jv[jss::result][jss::asks].size() == 0);
            expect(jv[jss::result].isMember(jss::bids) &&
                jv[jss::result][jss::bids].size() == 0);
            expect(! jv[jss::result].isMember(jss::offers));
        }

        {
            // Create an ask: TakerPays 700, TakerGets 100/USD
            env(offer("alice", XRP(700), USD(100)),
                require(owners("alice", 1)));
            env.close();

            // Check stream update
            expect(wsc->findMsg(5s,
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
            expect(wsc->findMsg(5s,
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
            expect(wsc->findMsg(5s,
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
            expect(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    auto const& t = jv[jss::transaction];
                    return t[jss::TransactionType] == "OfferCreate" &&
                        t[jss::TakerGets] == CNY(75).value().getJson(0) &&
                            t[jss::TakerPays] == JPY(100).value().getJson(0);
                }));
        }

        // RPC unsubscribe
        expect(wsc->invoke("unsubscribe",
            books)[jss::status] == "success");
    }

    void
    testMultipleBooksBothSidesOffersInBook()
    {
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
            expect(jv[jss::status] == "success");
            expect(jv[jss::result].isMember(jss::asks) &&
                jv[jss::result][jss::asks].size() == 2);
            expect(jv[jss::result].isMember(jss::bids) &&
                jv[jss::result][jss::bids].size() == 2);
            expect(jv[jss::result][jss::asks][0u][jss::TakerGets] ==
                USD(100).value().getJson(0));
            expect(jv[jss::result][jss::asks][0u][jss::TakerPays] ==
                XRP(500).value().getJson(0));
            expect(jv[jss::result][jss::asks][1u][jss::TakerGets] ==
                JPY(100).value().getJson(0));
            expect(jv[jss::result][jss::asks][1u][jss::TakerPays] ==
                CNY(500).value().getJson(0));
            expect(jv[jss::result][jss::bids][0u][jss::TakerGets] ==
                XRP(200).value().getJson(0));
            expect(jv[jss::result][jss::bids][0u][jss::TakerPays] ==
                USD(100).value().getJson(0));
            expect(jv[jss::result][jss::bids][1u][jss::TakerGets] ==
                CNY(200).value().getJson(0));
            expect(jv[jss::result][jss::bids][1u][jss::TakerPays] ==
                JPY(100).value().getJson(0));
            expect(! jv[jss::result].isMember(jss::offers));
        }

        {
            // Create an ask: TakerPays 700, TakerGets 100/USD
            env(offer("alice", XRP(700), USD(100)),
                require(owners("alice", 5)));
            env.close();

            // Check stream update
            expect(wsc->findMsg(5s,
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
            expect(wsc->findMsg(5s,
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
            expect(wsc->findMsg(5s,
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
            expect(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    auto const& t = jv[jss::transaction];
                    return t[jss::TransactionType] == "OfferCreate" &&
                        t[jss::TakerGets] == CNY(75).value().getJson(0) &&
                            t[jss::TakerPays] == JPY(100).value().getJson(0);
                }));
        }

        // RPC unsubscribe
        expect(wsc->invoke("unsubscribe",
            books)[jss::status] == "success");
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
    }
};

BEAST_DEFINE_TESTSUITE(Book,app,ripple);

} // test
} // ripple

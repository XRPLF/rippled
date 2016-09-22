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
#include <ripple/beast/unit_test.h>

namespace ripple {
namespace test {

class Book_test : public beast::unit_test::suite
{
public:
    void
    testcaseOneSideEmptyBook()
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
            BEAST_EXPECT(jv[jss::status] == "success");
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
        BEAST_EXPECT(wsc->invoke("unsubscribe",
            books)[jss::status] == "success");
    }

    void
    testcaseOneSideOffersInBook()
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
            BEAST_EXPECT(jv[jss::status] == "success");
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
        BEAST_EXPECT(wsc->invoke("unsubscribe",
            books)[jss::status] == "success");
    }

    void
    testcaseBothSidesEmptyBook()
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
            BEAST_EXPECT(jv[jss::status] == "success");
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
        BEAST_EXPECT(wsc->invoke("unsubscribe",
            books)[jss::status] == "success");
    }

    void
    testcaseBothSidesOffersInBook()
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
            BEAST_EXPECT(jv[jss::status] == "success");
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
        BEAST_EXPECT(wsc->invoke("unsubscribe",
            books)[jss::status] == "success");
    }

    void
    testcaseMultipleBooksOneSideEmptyBook()
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
            BEAST_EXPECT(jv[jss::status] == "success");
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
        BEAST_EXPECT(wsc->invoke("unsubscribe",
            books)[jss::status] == "success");
    }

    void
    testcaseMultipleBooksOneSideOffersInBook()
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
            BEAST_EXPECT(jv[jss::status] == "success");
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
        BEAST_EXPECT(wsc->invoke("unsubscribe",
            books)[jss::status] == "success");
    }

    void
    testcaseMultipleBooksBothSidesEmptyBook()
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
            BEAST_EXPECT(jv[jss::status] == "success");
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
        BEAST_EXPECT(wsc->invoke("unsubscribe",
            books)[jss::status] == "success");
    }

    void
    testcaseMultipleBooksBothSidesOffersInBook()
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
            BEAST_EXPECT(jv[jss::status] == "success");
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
        BEAST_EXPECT(wsc->invoke("unsubscribe",
            books)[jss::status] == "success");
    }

    void
    testcaseTrackOffers()
    {
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
            BEAST_EXPECT(jv[jss::status] == "success");
            BEAST_EXPECT(jv[jss::result].isMember(jss::offers) &&
                jv[jss::result][jss::offers].size() == 0);
            BEAST_EXPECT(! jv[jss::result].isMember(jss::asks));
            BEAST_EXPECT(! jv[jss::result].isMember(jss::bids));
        }

        env(rate(gw, 1.1));
        env.close();
        env.trust( USD(1000), alice );
        env.trust( USD(1000), bob );
        env(pay(gw, alice, USD(100)));
        env(pay(gw, bob,   USD(50)));
        env(offer(alice, drops(4000), USD(10)));
        env.close();

        Json::Value jvParams;
        jvParams[jss::taker] = env.master.human();
        jvParams[jss::taker_pays][jss::currency] = "XRP";
        jvParams[jss::ledger_index] = "validated";
        jvParams[jss::taker_gets][jss::currency] = "USD";
        jvParams[jss::taker_gets][jss::issuer] = gw.human();
        auto const jrr =
            env.rpc("json", "book_offers", to_string(jvParams)) [jss::result];
        env.close();
        BEAST_EXPECT(jrr[jss::offers].isArray());
        BEAST_EXPECT(jrr[jss::offers].size() == 1);
        auto const jrOffer = jrr[jss::offers][0u];
        BEAST_EXPECT(jrOffer[sfAccount.fieldName] == alice.human());
        BEAST_EXPECT(jrOffer[sfBookDirectory.fieldName] ==
            "1C5C34DB7DBE43E1EA72EE080416E88A87C18B2AD29BD8C4570E35FA931A0000");
        BEAST_EXPECT(jrOffer[sfBookNode.fieldName] == "0000000000000000");
        BEAST_EXPECT(jrOffer[jss::Flags] == 0);
        BEAST_EXPECT(jrOffer[sfLedgerEntryType.fieldName] == "Offer");
        BEAST_EXPECT(jrOffer[sfOwnerNode.fieldName] == "0000000000000000");
        BEAST_EXPECT(jrOffer[sfSequence.fieldName] == 3);
        BEAST_EXPECT(jrOffer[jss::TakerGets] == USD(10).value().getJson(0));
        BEAST_EXPECT(jrOffer[jss::TakerPays] == drops(4000).value().getJson(0));
        BEAST_EXPECT(jrOffer[jss::index] ==
            "2A432F386EF28151AF60885CE201CC9331FF494A163D40531A9D253C97E81D61");
        BEAST_EXPECT(jrOffer[jss::owner_funds] == "100");
        BEAST_EXPECT(jrOffer[jss::quality] == "400");

        BEAST_EXPECT(wsc->findMsg(5s,
            [&](auto const& jv)
            {
                auto const& t = jv[jss::transaction];
                return t[jss::TransactionType] == "OfferCreate" &&
                       t[jss::TakerGets] == USD(10).value().getJson(0) &&
                       t[jss::owner_funds] == "100" &&
                       t[jss::TakerPays] == drops(4000).value().getJson(0);
            }));

        env(offer(bob, drops(2000), USD(5)));
        env.close();

        BEAST_EXPECT(wsc->findMsg(5s,
            [&](auto const& jv)
            {
                auto const& t = jv[jss::transaction];
                return t[jss::TransactionType] == "OfferCreate" &&
                       t[jss::TakerGets] == USD(5).value().getJson(0) &&
                       t[jss::owner_funds] == "50" &&
                       t[jss::TakerPays] == drops(2000).value().getJson(0);
            }));

        BEAST_EXPECT(wsc->invoke("unsubscribe",
            books)[jss::status] == "success");
    }

    void
    testcaseBookOfferErrors()
    {
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
    testcaseBookOfferLimits()
    {
        using namespace jtx;
        Env env(*this);
        Account gw {"gw"};
        env.fund(XRP(20000), gw);
        env.close();
        auto USD = gw["USD"];

        env(offer(gw, XRP(500), USD(100)));
        env(offer(gw, XRP(100), USD(2)));
        env(offer(gw, XRP(500), USD(101)));
        env(offer(gw, XRP(500), USD(99)));
        env(offer(gw, XRP(50), USD(10)));
        env(offer(gw, XRP(50), USD(9)));
        env.close();

        Json::Value jvParams;
        jvParams[jss::limit] = 1;
        jvParams[jss::ledger_index] = "validated";
        jvParams[jss::taker_pays][jss::currency] = "XRP";
        jvParams[jss::taker_gets][jss::currency] = "USD";
        jvParams[jss::taker_gets][jss::issuer] = gw.human();
        auto const jrr =
            env.rpc("json", "book_offers", to_string(jvParams)) [jss::result];
        BEAST_EXPECT(jrr[jss::offers].isArray());
        BEAST_EXPECT(jrr[jss::offers].size() == 1);
        // NOTE - a marker field is not returned for this method
    }

    void
    run() override
    {
        testcaseOneSideEmptyBook();
        testcaseOneSideOffersInBook();

        testcaseBothSidesEmptyBook();
        testcaseBothSidesOffersInBook();

        testcaseMultipleBooksOneSideEmptyBook();
        testcaseMultipleBooksOneSideOffersInBook();

        testcaseMultipleBooksBothSidesEmptyBook();
        testcaseMultipleBooksBothSidesOffersInBook();

        testcaseTrackOffers();
        testcaseBookOfferErrors();
        testcaseBookOfferLimits();
    }
};

BEAST_DEFINE_TESTSUITE(Book,app,ripple);

} // test
} // ripple


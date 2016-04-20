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
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/core/LoadFeeTrack.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/test/WSClient.h>
#include <ripple/test/jtx.h>
#include <ripple/beast/unit_test.h>

namespace ripple {
namespace test {

class Subscribe_test : public beast::unit_test::suite
{
public:
    void testServer()
    {
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this);
        auto wsc = makeWSClient(env.app().config());
        Json::Value stream;

        {
            // RPC subscribe to server stream
            stream[jss::streams] = Json::arrayValue;
            stream[jss::streams].append("server");
            auto jv = wsc->invoke("subscribe", stream);
            expect(jv[jss::status] == "success");
        }

        {
            // Raise fee to cause an update
            for(int i = 0; i < 5; ++i)
                env.app().getFeeTrack().raiseLocalFee();
            env.app().getOPs().reportFeeChange();

            // Check stream update
            expect(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    return jv[jss::type] == "serverStatus";
                }));
        }

        {
            // RPC unsubscribe
            auto jv = wsc->invoke("unsubscribe", stream);
            expect(jv[jss::status] == "success");
        }

        {
            // Raise fee to cause an update
            for (int i = 0; i < 5; ++i)
                env.app().getFeeTrack().raiseLocalFee();
            env.app().getOPs().reportFeeChange();

            // Check stream update
            expect(! wsc->getMsg(10ms));
        }
    }

    void testLedger()
    {
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this);
        auto wsc = makeWSClient(env.app().config());
        Json::Value stream;

        {
            // RPC subscribe to ledger stream
            stream[jss::streams] = Json::arrayValue;
            stream[jss::streams].append("ledger");
            auto jv = wsc->invoke("subscribe", stream);
            expect(jv[jss::result][jss::ledger_index] == 2);
        }

        {
            // Accept a ledger
            env.close();

            // Check stream update
            expect(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    return jv[jss::ledger_index] == 3;
                }));
        }

        {
            // Accept another ledger
            env.close();

            // Check stream update
            expect(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    return jv[jss::ledger_index] == 4;
                }));
        }

        // RPC unsubscribe
        auto jv = wsc->invoke("unsubscribe", stream);
        expect(jv[jss::status] == "success");
    }

    void testTransactions()
    {
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this);
        auto wsc = makeWSClient(env.app().config());
        Json::Value stream;

        {
            // RPC subscribe to transactions stream
            stream[jss::streams] = Json::arrayValue;
            stream[jss::streams].append("transactions");
            auto jv = wsc->invoke("subscribe", stream);
            expect(jv[jss::status] == "success");
        }

        {
            env.fund(XRP(10000), "alice");
            env.close();

            // Check stream update for payment transaction
            expect(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    return jv[jss::meta]["AffectedNodes"][1u]
                        ["CreatedNode"]["NewFields"][jss::Account] ==
                            Account("alice").human();
                }));

            // Check stream update for accountset transaction
            expect(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    return jv[jss::meta]["AffectedNodes"][0u]
                        ["ModifiedNode"]["FinalFields"][jss::Account] ==
                            Account("alice").human();
                }));

            env.fund(XRP(10000), "bob");
            env.close();

            // Check stream update for payment transaction
            expect(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    return jv[jss::meta]["AffectedNodes"][1u]
                        ["CreatedNode"]["NewFields"][jss::Account] ==
                            Account("bob").human();
                }));

            // Check stream update for accountset transaction
            expect(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    return jv[jss::meta]["AffectedNodes"][0u]
                        ["ModifiedNode"]["FinalFields"][jss::Account] ==
                            Account("bob").human();
                }));
        }

        {
            // RPC unsubscribe
            auto jv = wsc->invoke("unsubscribe", stream);
            expect(jv[jss::status] == "success");
        }

        {
            // RPC subscribe to accounts stream
            stream = Json::objectValue;
            stream[jss::accounts] = Json::arrayValue;
            stream[jss::accounts].append(Account("alice").human());
            auto jv = wsc->invoke("subscribe", stream);
            expect(jv[jss::status] == "success");
        }

        {
            // Transaction that does not affect stream
            env.fund(XRP(10000), "carol");
            env.close();
            expect(! wsc->getMsg(10ms));

            // Transactions concerning alice
            env.trust(Account("bob")["USD"](100), "alice");
            env.close();

            // Check stream updates
            expect(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    return jv[jss::meta]["AffectedNodes"][1u]
                        ["ModifiedNode"]["FinalFields"][jss::Account] ==
                            Account("alice").human();
                }));

            expect(wsc->findMsg(5s,
                [&](auto const& jv)
                {
                    return jv[jss::meta]["AffectedNodes"][1u]
                        ["CreatedNode"]["NewFields"]["LowLimit"]
                            [jss::issuer] == Account("alice").human();
                }));
        }

        // RPC unsubscribe
        auto jv = wsc->invoke("unsubscribe", stream);
        expect(jv[jss::status] == "success");
    }

    void testManifests()
    {
        using namespace jtx;
        Env env(*this);
        auto wsc = makeWSClient(env.app().config());
        Json::Value stream;

        {
            // RPC subscribe to manifests stream
            stream[jss::streams] = Json::arrayValue;
            stream[jss::streams].append("manifests");
            auto jv = wsc->invoke("subscribe", stream);
            expect(jv[jss::status] == "success");
        }

        // RPC unsubscribe
        auto jv = wsc->invoke("unsubscribe", stream);
        expect(jv[jss::status] == "success");
    }

    void testValidations()
    {
        using namespace jtx;
        Env env(*this);
        auto wsc = makeWSClient(env.app().config());
        Json::Value stream;

        {
            // RPC subscribe to validations stream
            stream[jss::streams] = Json::arrayValue;
            stream[jss::streams].append("validations");
            auto jv = wsc->invoke("subscribe", stream);
            expect(jv[jss::status] == "success");
        }

        // RPC unsubscribe
        auto jv = wsc->invoke("unsubscribe", stream);
        expect(jv[jss::status] == "success");
    }

    void run() override
    {
        testServer();
        testLedger();
        testTransactions();
        testManifests();
        testValidations();
    }
};

BEAST_DEFINE_TESTSUITE(Subscribe,app,ripple);

} // test
} // ripple

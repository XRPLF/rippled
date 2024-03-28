//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Dev Null Productions

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

#include <ripple/basics/BasicConfig.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>
#include <test/jtx/Env.h>

namespace ripple {
namespace test {

class NetworkID_test : public beast::unit_test::suite
{
public:
    void
    run() override
    {
        testNetworkID();
    }

    std::unique_ptr<Config>
    makeNetworkConfig(uint32_t networkID)
    {
        using namespace jtx;
        return envconfig([&](std::unique_ptr<Config> cfg) {
            cfg->NETWORK_ID = networkID;
            return cfg;
        });
    }

    void
    testNetworkID()
    {
        testcase(
            "Require txn NetworkID to be specified (or not) depending on the "
            "network ID of the node");
        using namespace jtx;

        auto const alice = Account{"alice"};

        auto const runTx = [&](test::jtx::Env& env,
                               Json::Value const& jv,
                               TER expectedOutcome) {
            env.memoize(env.master);
            env.memoize(alice);

            // fund alice
            {
                Json::Value jv;
                jv[jss::Account] = env.master.human();
                jv[jss::Destination] = alice.human();
                jv[jss::TransactionType] = "Payment";
                jv[jss::Amount] = "10000000000";
                env(jv, fee(1000), sig(env.master));
            }

            env(jv, fee(1000), ter(expectedOutcome));
            env.close();
        };

        // test mainnet
        {
            test::jtx::Env env{*this, makeNetworkConfig(0)};
            BEAST_EXPECT(env.app().config().NETWORK_ID == 0);

            // try to submit a txn without network id, this should work
            Json::Value jv;
            jv[jss::Account] = alice.human();
            jv[jss::TransactionType] = jss::AccountSet;
            runTx(env, jv, tesSUCCESS);

            // try to submit a txn with NetworkID present against a mainnet
            // node, this will fail
            jv[jss::NetworkID] = 0;
            runTx(env, jv, telNETWORK_ID_MAKES_TX_NON_CANONICAL);

            // change network id to something else, should still return same
            // error
            jv[jss::NetworkID] = 10000;
            runTx(env, jv, telNETWORK_ID_MAKES_TX_NON_CANONICAL);
        }

        // any network up to and including networkid 1024 cannot support
        // NetworkID
        {
            test::jtx::Env env{*this, makeNetworkConfig(1024)};
            BEAST_EXPECT(env.app().config().NETWORK_ID == 1024);

            // try to submit a txn without network id, this should work
            Json::Value jv;
            jv[jss::Account] = alice.human();
            jv[jss::TransactionType] = jss::AccountSet;
            runTx(env, jv, tesSUCCESS);

            // now submit with a network id, this will fail
            jv[jss::NetworkID] = 1024;
            runTx(env, jv, telNETWORK_ID_MAKES_TX_NON_CANONICAL);

            jv[jss::NetworkID] = 1000;
            runTx(env, jv, telNETWORK_ID_MAKES_TX_NON_CANONICAL);
        }

        // any network above networkid 1024 will produce an error if fed a txn
        // absent networkid
        {
            test::jtx::Env env{*this, makeNetworkConfig(1025)};
            BEAST_EXPECT(env.app().config().NETWORK_ID == 1025);
            {
                env.fund(XRP(200), alice);
                // try to submit a txn without network id, this should not work
                Json::Value jvn;
                jvn[jss::Account] = alice.human();
                jvn[jss::TransactionType] = jss::AccountSet;
                jvn[jss::Fee] = to_string(env.current()->fees().base);
                jvn[jss::Sequence] = env.seq(alice);
                jvn[jss::LastLedgerSequence] = env.current()->info().seq + 2;
                auto jt = env.jtnofill(jvn);
                Serializer s;
                jt.stx->add(s);
                BEAST_EXPECT(
                    env.rpc(
                        "submit",
                        strHex(s.slice()))[jss::result][jss::engine_result] ==
                    "telREQUIRES_NETWORK_ID");
                env.close();
            }

            Json::Value jv;
            jv[jss::Account] = alice.human();
            jv[jss::TransactionType] = jss::AccountSet;

            // try to submit with wrong network id
            jv[jss::NetworkID] = 0;
            runTx(env, jv, telWRONG_NETWORK);

            jv[jss::NetworkID] = 1024;
            runTx(env, jv, telWRONG_NETWORK);

            // submit the correct network id
            jv[jss::NetworkID] = 1025;
            runTx(env, jv, tesSUCCESS);
        }
    }
};

BEAST_DEFINE_TESTSUITE(NetworkID, app, ripple);

}  // namespace test
}  // namespace ripple

//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 XRPL-Labs.

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

#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/jss.h>
#include <test/jtx.h>

namespace xrpl {
namespace test {
class Wildcard_test : public beast::unit_test::suite
{
    std::unique_ptr<Config>
    makeNetworkConfig(uint32_t networkID)
    {
        using namespace jtx;
        return envconfig([&](std::unique_ptr<Config> cfg) {
            cfg->NETWORK_ID = networkID;
            Section config;
            config.append(
                {"reference_fee = 10",
                 "account_reserve = 1000000",
                 "owner_reserve = 200000"});
            auto setup = setup_FeeVote(config);
            cfg->FEES = setup;
            return cfg;
        });
    }

    void
    testWildcardSign(FeatureBitset features)
    {
        using namespace test::jtx;

        testcase("wildcard sign");

        for (bool const wildcardNetwork : {true, false})
        {
            auto const network = wildcardNetwork ? 65535 : 21337;
            Env env{*this, makeNetworkConfig(network), features};
            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const carol{"carol"};
            Account const dave{"dave"};
            env.fund(XRP(1000), alice, bob, carol, dave);
            env.close();

            Json::Value jv;
            jv[jss::Account] = alice.human();
            jv[jss::Destination] = bob.human();
            jv[jss::TransactionType] = "Payment";
            jv[jss::Amount] = "1000000";

            // lambda that submits an STTx and returns the resulting JSON.
            auto submitSTTx = [&env](STTx const& stx) {
                Json::Value jvResult;
                jvResult[jss::tx_blob] = strHex(stx.getSerializer().slice());
                return env.rpc("json", "submit", to_string(jvResult));
            };

            // Account/RegularKey Sign
            {
                JTx tx = env.jt(jv, sig(bob));
                STTx local = *(tx.stx);
                auto const info = submitSTTx(local);
                auto const tecResult =
                    wildcardNetwork ? "tesSUCCESS" : "tefBAD_AUTH";
                BEAST_EXPECT(
                    info[jss::result][jss::engine_result] == tecResult);
            }

            // Multi Sign
            {
                env(signers(alice, 1, {{bob, 1}, {carol, 1}}));
                env.close();

                JTx tx = env.jt(jv, msig(dave), fee(XRP(1)));
                STTx local = *(tx.stx);
                auto const info = submitSTTx(local);
                auto const tecResult =
                    wildcardNetwork ? "tesSUCCESS" : "tefBAD_SIGNATURE";
                BEAST_EXPECT(
                    info[jss::result][jss::engine_result] == tecResult);
            }
        }
    }

    void
    testSimplePayment(FeatureBitset features)
    {
        using namespace test::jtx;

        testcase("simple payment");

        Env env{*this, envconfig(), features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const dave{"dave", KeyType::dilithium};
        env.fund(XRP(1000), alice, bob, carol, dave);
        env.close();

        env(pay(dave, bob, XRP(100)));
        env.close();

        Json::Value params;
        params[jss::ledger_index] = env.current()->seq() - 1;
        params[jss::transactions] = true;
        params[jss::expand] = true;
        auto const jrr = env.rpc("json", "ledger", to_string(params));
        std::cout << jrr << std::endl;
    }

    void
    testWithFeats(FeatureBitset features)
    {
        // testWildcardSign(features);
        testSimplePayment(features);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        auto const sa = testable_amendments();
        testWithFeats(sa);
    }
};

BEAST_DEFINE_TESTSUITE(Wildcard, app, ripple);
}  // namespace test
}  // namespace xrpl

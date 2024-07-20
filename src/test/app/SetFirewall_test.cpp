//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2023 Ripple Labs Inc.

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

#include <test/jtx.h>
#include <xrpld/ledger/Directory.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Firewall.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {
struct SetFirewall_test : public beast::unit_test::suite
{

    static std::size_t
    ownerDirCount(ReadView const& view, jtx::Account const& acct)
    {
        ripple::Dir const ownerDir(view, keylet::ownerDir(acct.id()));
        return std::distance(ownerDir.begin(), ownerDir.end());
    };

    static Buffer
    sigFirewallAuth(
        PublicKey const& pk,
        SecretKey const& sk,
        Json::Value const& authAccounts)
    {
        Serializer msg;
        serializeFirewallAuthorization(msg, authAccounts);
        return sign(pk, sk, msg.slice());
    }

    void
    testEnabled(FeatureBitset features)
    {
        testcase("enabled");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        // setup env
        Account const alice = Account("alice");

        for (bool const withFirewall : {false, true})
        {
            // If the Firewall amendment is not enabled, you should not be able
            // to set or delete firewall.
            auto const amend = withFirewall ? features : features - featureFirewall;
            Env env{*this, amend};
            env.fund(XRP(1000), alice);
            env.close();

            auto const txResult = withFirewall ? ter(tesSUCCESS) : ter(temDISABLED);
            auto const ownerDir = withFirewall ? 1 : 0;

            // SET
            env(firewall::set(alice), txResult);
            env.close();
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == ownerDir);
        }
    }

    void
    testEnabled1(FeatureBitset features)
    {
        testcase("enabled1");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        // setup env
        Account const alice = Account("alice");
        Account const bob = Account("bob");
        Account const carol = Account("carol");

        {
            Env env{*this, features};
            // Env env{*this, envconfig(), features, nullptr,
            //     // beast::severities::kWarning
            //     beast::severities::kTrace
            // };
            env.fund(XRP(1000), alice, bob, carol);
            env.close();

            // FIREWALL SET
            env(firewall::set(alice),
                firewall::auth(carol),
                firewall::amt(XRP(10)),
                firewall::pk(strHex(carol.pk().slice())),
                ter(tesSUCCESS));
            env.close();

            {
                Json::Value params;
                params[jss::transaction] = env.tx()->getJson(JsonOptions::none)[jss::hash];
                auto jrr = env.rpc("json", "tx", to_string(params))[jss::result];
                std::cout << "RESULT: " << jrr << "\n";
            }

            env(pay(alice, bob, XRP(100)), ter(tecFIREWALL_BLOCK));
            env.close();

            auto tx = firewall::set(alice);
            tx[jss::AuthAccounts] = Json::Value{Json::arrayValue};
            tx[jss::AuthAccounts][0U] = Json::Value{};
            tx[jss::AuthAccounts][0U][jss::AuthAccount] = Json::Value{};
            tx[jss::AuthAccounts][0U][jss::AuthAccount][jss::Account] = bob.human();
            tx[jss::AuthAccounts][0U][jss::AuthAccount][jss::Amount] = "100000000";
            auto const sig = sigFirewallAuth(carol.pk(), carol.sk(), tx[jss::AuthAccounts]);
            env(tx,
                firewall::sig(strHex(Slice(sig))),
                ter(tesSUCCESS));
            env.close();

            {
                Json::Value params;
                params[jss::transaction] = env.tx()->getJson(JsonOptions::none)[jss::hash];
                auto jrr = env.rpc("json", "tx", to_string(params))[jss::result];
                std::cout << "RESULT: " << jrr << "\n";
            }

            env(pay(alice, bob, XRP(100)), ter(tesSUCCESS));
            env.close();
        }
    }

    void
    testWithFeats(FeatureBitset features)
    {
        // testEnabled(features);
        testEnabled1(features);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{supported_amendments()};
        testWithFeats(all);
    }
};

BEAST_DEFINE_TESTSUITE(SetFirewall, app, ripple);
}  // namespace test
}  // namespace ripple

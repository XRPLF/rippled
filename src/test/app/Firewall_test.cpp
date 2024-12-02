//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2024 Transia, LLC.

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
#include <xrpld/ledger/Dir.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {
struct Firewall_test : public beast::unit_test::suite
{
    static std::size_t
    ownerDirCount(ReadView const& view, jtx::Account const& acct)
    {
        ripple::Dir const ownerDir(view, keylet::ownerDir(acct.id()));
        return std::distance(ownerDir.begin(), ownerDir.end());
    };

    static std::pair<uint256, std::shared_ptr<SLE const>>
    firewallKeyAndSle(ReadView const& view, jtx::Account const& account)
    {
        auto const k = keylet::firewall(account);
        return {k.key, view.read(k)};
    }

    void
    verifyFirewallSle(
        ReadView const& view,
        jtx::Account const& account,
        jtx::Account const& issuer,
        std::optional<STAmount> const& amount = std::nullopt,
        std::optional<uint32_t> const& timePeriod = std::nullopt,
        std::optional<uint32_t> const& timeStart = std::nullopt,
        std::optional<STAmount> const& totalOut = std::nullopt)
    {
        auto [key, sle] = firewallKeyAndSle(view, account);
        BEAST_EXPECT((*sle)[sfOwner] == account.id());
        BEAST_EXPECT((*sle)[sfIssuer] == issuer.id());
        if (amount)
        {
            std::cout << "amount: " << *amount << std::endl;
            BEAST_EXPECT((*sle)[sfAmount] == *amount);
        }
        if (timePeriod)
        {
            std::cout << "timePeriod: " << *timePeriod << std::endl;
            BEAST_EXPECT((*sle)[sfTimePeriod] == *timePeriod);
        }
        if (timeStart)
        {
            std::cout << "timeStart: " << *timeStart << std::endl;
            BEAST_EXPECT((*sle)[sfTimePeriodStart] == *timeStart);
        }
        if (totalOut)
        {
            std::cout << "totalOut: " << *totalOut << std::endl;
            BEAST_EXPECT((*sle)[sfTotalOut] == *totalOut);
        }
    }

    void
    testEnabled(FeatureBitset features)
    {
        testcase("enabled");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        Account const alice = Account("alice");
        Account const bob = Account("bob");
        Account const carol = Account("carol");

        for (bool const withFirewall : {true, false})
        {
            // If the Firewall amendment is not enabled, you should not be able
            // to set or delete firewall.
            auto const amend =
                withFirewall ? features : features - featureFirewall;
            Env env{*this, amend};
            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const txResult =
                withFirewall ? ter(tesSUCCESS) : ter(temDISABLED);
            auto const dirCount = withFirewall ? 2 : 0;

            auto const seq = env.seq(alice);
            auto const fee = env.current()->fees().base;
            env(firewall::set(alice, seq, fee),
                firewall::auth(bob),
                firewall::issuer(carol),
                txResult);
            env.close();
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == dirCount);
        }
    }

    void
    testPreflight(FeatureBitset features)
    {
        testcase("preflight");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        Account const alice = Account("alice");
        Account const bob = Account("bob");
        Account const carol = Account("carol");

        // preflight
        // ---------------------------------------------------------

        // temINVALID_ACCOUNT_ID
        // temCANNOT_PREAUTH_SELF
    }

    void
    testPreclaim(FeatureBitset features)
    {
        testcase("preclaim");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        Account const alice = Account("alice");
        Account const bob = Account("bob");
        Account const carol = Account("carol");

        // preclaim
        // ---------------------------------------------------------

        // Set - Create
        // temMALFORMED: Firewall: Set must not contain a sfSignature
        // temMALFORMED: Firewall: Set must contain a sfAuthorize
        // temMALFORMED: Firewall: Set must contain a sfPublicKey

        // Set - Update
        // temMALFORMED: Firewall: Update must contain a sfSignature
        // temMALFORMED: Firewall: Update cannot contain a sfAuthorize
        // temMALFORMED: Firewall: Update cannot contain both sfPublicKey & sfAmount
        // temBAD_SIGNATURE: Firewall: Bad Signature for update sfPublicKey
        // temBAD_SIGNATURE: Firewall: Bad Signature for update sfAmount
    }

    void
    testDoApply(FeatureBitset features)
    {
        testcase("doApply");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        Account const alice = Account("alice");
        Account const bob = Account("bob");
        Account const carol = Account("carol");

        // doApply
        // ---------------------------------------------------------

        // All
        // tefINTERNAL: Firewall: Owner account not found

        // Set - Create
        // tecDIR_FULL: Firewall: failed to insert owner dir
        // tecINSUFFICIENT_RESERVE: Firewall: Insufficient reserve to set firewall
        // tecDIR_FULL: Firewall: failed to insert owner dir

        // Set - Update

    }

    void
    testFirewallSet(FeatureBitset features)
    {
        testcase("firewall set");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        Account const alice = Account("alice");
        Account const bob = Account("bob");
        Account const carol = Account("carol");

        // No Amount
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol);
            env.close();

            auto const seq = env.seq(alice);
            auto const fee = env.current()->fees().base;
            env(firewall::set(alice, seq, fee),
                firewall::auth(bob),
                firewall::issuer(carol),
                ter(tesSUCCESS));
            env.close();
            verifyFirewallSle(*env.current(), alice, carol);
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 2);
        }

        // Amount w/out Time Period
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol);
            env.close();

            auto const seq = env.seq(alice);
            auto const fee = env.current()->fees().base;
            env(firewall::set(alice, seq, fee),
                firewall::auth(bob),
                firewall::amt(XRP(10)),
                firewall::issuer(carol),
                ter(tesSUCCESS));
            env.close();

            verifyFirewallSle(*env.current(), alice, carol, XRP(10));
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 2);
        }

        // Amount w/ Time Period
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol);
            env.close();

            auto const timeStart = env.now();
            auto const seq = env.seq(alice);
            auto const fee = env.current()->fees().base;
            env(firewall::set(alice, seq, fee),
                firewall::auth(bob),
                firewall::amt(XRP(10)),
                firewall::time_period(3600),
                firewall::issuer(carol),
                ter(tesSUCCESS));
            env.close();

            verifyFirewallSle(
                *env.current(),
                alice,
                carol,
                XRP(10),
                3600,
                timeStart.time_since_epoch().count(),
                STAmount(0));
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 2);
        }
    }

    void
    testFirewallBlock(FeatureBitset features)
    {
        testcase("firewall block");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        Account const alice = Account("alice");
        Account const bob = Account("bob");
        Account const carol = Account("carol");
        Account const dave = Account("dave");

        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol, dave);
            env.close();

            auto const seq = env.seq(alice);
            auto const fee = env.current()->fees().base;
            env(firewall::set(alice, seq, fee),
                firewall::auth(bob),
                firewall::amt(XRP(10)),
                firewall::issuer(carol),
                ter(tesSUCCESS));
            env.close();

            {
                Json::Value params;
                params[jss::ledger_index] = env.current()->seq() - 1;
                params[jss::transactions] = true;
                params[jss::expand] = true;
                auto const jrr = env.rpc("json", "ledger", to_string(params));
                std::cout << "jrr: " << jrr << "\n";
            }

            env(pay(alice, dave, XRP(100)), ter(tecFIREWALL_BLOCK));
            env.close();

            {
                Json::Value params;
                params[jss::ledger_index] = env.current()->seq() - 1;
                params[jss::transactions] = true;
                params[jss::expand] = true;
                auto const jrr = env.rpc("json", "ledger", to_string(params));
                std::cout << "jrr: " << jrr << "\n";
            }
        }
    }

    void
    testFirewallSetUpdate(FeatureBitset features)
    {
        testcase("set update");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        Account const alice = Account("alice");
        Account const bob = Account("bob");
        Account const carol = Account("carol");
        Account const dave = Account("dave");
        Account const elsa = Account("elsa");

        // Update Amount w/out time limit
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
            env.fund(XRP(1000), alice, bob, carol, dave);
            env.close();

            env(firewall::set(alice, env.seq(alice), baseFee),
                firewall::auth(bob),
                firewall::amt(XRP(10)),
                firewall::issuer(carol),
                ter(tesSUCCESS));
            env.close();

            env(pay(alice, dave, XRP(100)), ter(tecFIREWALL_BLOCK));
            env.close();

            env(firewall::set(alice, env.seq(alice), baseFee),
                firewall::amt(XRP(101)),
                firewall::sig(carol),
                ter(tesSUCCESS));
            env.close();

            verifyFirewallSle(*env.current(), alice, carol, XRP(101));
            env(pay(alice, dave, XRP(100)), ter(tesSUCCESS));
            env.close();
        }

        // Update Amount w/ time limit
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
            env.fund(XRP(1000), alice, bob, carol, dave);
            env.close();

            env(firewall::set(alice, env.seq(alice), baseFee),
                firewall::auth(bob),
                firewall::amt(XRP(10)),
                firewall::time_period(300),
                firewall::issuer(carol),
                ter(tesSUCCESS));
            env.close();

            verifyFirewallSle(
                *env.current(),
                alice,
                carol,
                XRP(10),
                300,
                env.now().time_since_epoch().count(),
                STAmount(0));

            env(pay(alice, dave, XRP(100)), ter(tecFIREWALL_BLOCK));
            env.close();

            env(firewall::set(alice, env.seq(alice), baseFee),
                firewall::amt(XRP(101)),
                firewall::time_period(3600),
                firewall::sig(carol),
                ter(tesSUCCESS));
            env.close();

            verifyFirewallSle(
                *env.current(),
                alice,
                carol,
                XRP(101),
                3600,
                env.now().time_since_epoch().count(),
                STAmount(0));

            env(pay(alice, dave, XRP(100)), ter(tesSUCCESS));
            env.close();
        }

        // // Update Issuer
        // {
        //     Env env{*this, features};
        //     auto const baseFee = env.current()->fees().base;
        //     env.fund(XRP(1000), alice, bob, carol, dave);
        //     env.close();

        //     env(firewall::set(alice, env.seq(alice), baseFee),
        //         firewall::auth(bob),
        //         firewall::issuer(carol),
        //         ter(tesSUCCESS));
        //     env.close();

        //     verifyFirewallSle(
        //         *env.current(),
        //         alice,
        //         carol);

        //     env(pay(alice, bob, XRP(100)), ter(tecFIREWALL_BLOCK));
        //     env.close();

        //     env(firewall::set(alice, env.seq(alice), baseFee),
        //         firewall::issuer(dave),
        //         firewall::sig(carol),
        //         ter(tesSUCCESS));
        //     env.close();

        //     verifyFirewallSle(
        //         *env.current(),
        //         alice,
        //         dave);

        //     env(pay(alice, bob, XRP(100)), ter(tesSUCCESS));
        //     env.close();
        // }
    }

    // void
    // testMasterDisable(FeatureBitset features)
    // {
    //     testcase("master disable");
    //     using namespace jtx;
    //     using namespace std::literals::chrono_literals;

    //     Account const alice = Account("alice");
    //     Account const bob = Account("bob");
    //     Account const carol = Account("carol");
    //     Account const dave = Account("dave");

    //     {
    //         Env env{*this, features};
    //         env.fund(XRP(1000), alice, bob, carol, dave);
    //         env.close();

    //         env(firewall::set(alice),
    //             firewall::auth(carol),
    //             firewall::amt(XRP(10)),
    //             firewall::issuer(carol),
    //             ter(tesSUCCESS));
    //         env.close();

    //         // verifyFirewall(*env.current(), alice, XRP(10), carol.pk());

    //         env(fset(alice, asfDisableMaster), ter(tecNO_PERMISSION));
    //         env.close();
    //     }
    // }
    
    // void
    // testTransactionTypes(FeatureBitset features)
    // {
    //     testcase("transaction types");
    //     using namespace jtx;
    //     using namespace std::literals::chrono_literals;

    //     Account const alice = Account("alice");
    //     Account const bob = Account("bob");
    //     Account const carol = Account("carol");
    //     Account const dave = Account("dave");

    //     // Payment
    //     {
    //         env(pay(alice, bob, XRP(100)), ter(tesSUCCESS));
    //     }
    // }

    void
    testWithFeats(FeatureBitset features)
    {
        // testEnabled(features);
        // testPreflight(features);
        // testPreclaim(features);
        // testDoApply(features);
        // testFirewallSet(features);
        // testFirewallBlock(features);
        // testFirewallDelete(features);
        testFirewallSetUpdate(features);
        // testUpdatePK(features);
        // testMasterDisable(features);
        // testTransactionTypes(features);

        // // Bad Amount
            // {
            //     env(pay(alice, bob, XRP(100)), ter(tecFIREWALL_BLOCK));
            //     env.close();
            // }
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

BEAST_DEFINE_TESTSUITE(Firewall, app, ripple);
}  // namespace test
}  // namespace ripple

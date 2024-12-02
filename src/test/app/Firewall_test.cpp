//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2024 Ripple Labs Inc.

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
#include <xrpl/protocol/Firewall.h>
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

    static Buffer
    sigFirewallAuthAmount(
        PublicKey const& pk,
        SecretKey const& sk,
        AccountID const& account,
        STAmount const& amount)
    {
        Serializer msg;
        serializeFirewallAuthorization(msg, account, amount);
        return sign(pk, sk, msg.slice());
    }

    static Buffer
    sigFirewallAuthPK(
        PublicKey const& pk,
        SecretKey const& sk,
        AccountID const& account,
        PublicKey const& _pk)
    {
        Serializer msg;
        serializeFirewallAuthorization(msg, account, _pk);
        return sign(pk, sk, msg.slice());
    }

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
        PublicKey const& pk,
        std::optional<STAmount> const& amount = std::nullopt,
        std::optional<uint32_t> const& timePeriod = std::nullopt,
        std::optional<uint32_t> const& timeStart = std::nullopt,
        std::optional<STAmount> const& totalOut = std::nullopt)
    {
        auto [key, sle] = firewallKeyAndSle(view, account);
        BEAST_EXPECT((*sle)[sfOwner] == account.id());
        BEAST_EXPECT(strHex((*sle)[sfPublicKey]) == strHex(pk.slice()));
        if (amount)
            BEAST_EXPECT((*sle)[sfAmount] == *amount);
        if (timePeriod)
            BEAST_EXPECT((*sle)[sfTimePeriod] == *timePeriod);
        if (timeStart)
            BEAST_EXPECT((*sle)[sfTimePeriodStart] == *timeStart);
        if (totalOut)
            BEAST_EXPECT((*sle)[sfTotalOut] == *totalOut);
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

            env(firewall::set(alice),
                firewall::auth(bob),
                firewall::pk(carol.pk()),
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

            env(firewall::set(alice),
                firewall::auth(carol),
                firewall::pk(carol.pk()),
                ter(tesSUCCESS));
            env.close();
            verifyFirewallSle(*env.current(), alice, carol.pk());
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 2);
        }

        // Amount w/out Time Period
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol);
            env.close();

            env(firewall::set(alice),
                firewall::auth(carol),
                firewall::amt(XRP(10)),
                firewall::pk(carol.pk()),
                ter(tesSUCCESS));
            env.close();

            verifyFirewallSle(*env.current(), alice, carol.pk(), XRP(10));
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 2);
        }

        // Amount w/ Time Period
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol);
            env.close();

            auto const timeStart = env.now();
            env(firewall::set(alice),
                firewall::auth(carol),
                firewall::amt(XRP(10)),
                firewall::time_period(3600),
                firewall::pk(carol.pk()),
                ter(tesSUCCESS));
            env.close();

            verifyFirewallSle(
                *env.current(),
                alice,
                carol.pk(),
                XRP(10),
                3600,
                timeStart.time_since_epoch().count(),
                STAmount(0));
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 2);
        }
    }

    void
    testUpdateAmount(FeatureBitset features)
    {
        testcase("update amount");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        Account const alice = Account("alice");
        Account const bob = Account("bob");
        Account const carol = Account("carol");

        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, carol);
            env.close();

            env(firewall::set(alice),
                firewall::auth(carol),
                firewall::amt(XRP(10)),
                firewall::pk(carol.pk()),
                ter(tesSUCCESS));
            env.close();

            env(pay(alice, bob, XRP(100)), ter(tecFIREWALL_BLOCK));
            env.close();

            auto const sig = sigFirewallAuthAmount(
                carol.pk(), carol.sk(), alice.id(), XRP(100));
            env(firewall::set(alice),
                firewall::amt(XRP(100)),
                firewall::sig(sig),
                ter(tesSUCCESS));
            env.close();

            // verifyFirewall(*env.current(), alice, XRP(100), carol.pk());

            env(pay(alice, bob, XRP(100)), ter(tesSUCCESS));
            env.close();
        }
    }

    void
    testUpdatePK(FeatureBitset features)
    {
        testcase("update pk");
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

            env(firewall::set(alice),
                firewall::auth(carol),
                firewall::amt(XRP(10)),
                firewall::pk(carol.pk()),
                ter(tesSUCCESS));
            env.close();

            // verifyFirewall(*env.current(), alice, XRP(10), carol.pk());

            env(pay(alice, bob, XRP(100)), ter(tecFIREWALL_BLOCK));
            env.close();

            auto const sig1 = sigFirewallAuthPK(
                carol.pk(), carol.sk(), alice.id(), dave.pk());
            env(firewall::set(alice),
                firewall::pk(dave.pk()),
                firewall::sig(sig1),
                ter(tesSUCCESS));
            env.close();

            // verifyFirewall(*env.current(), alice, XRP(10), dave.pk());

            auto const sig2 = sigFirewallAuthAmount(
                dave.pk(), dave.sk(), alice.id(), XRP(100));
            env(firewall::set(alice),
                firewall::amt(XRP(100)),
                firewall::sig(sig2),
                ter(tesSUCCESS));
            env.close();

            // verifyFirewall(*env.current(), alice, XRP(100), dave.pk());

            env(pay(alice, bob, XRP(100)), ter(tesSUCCESS));
            env.close();
        }
    }

    void
    testMasterDisable(FeatureBitset features)
    {
        testcase("master disable");
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

            env(firewall::set(alice),
                firewall::auth(carol),
                firewall::amt(XRP(10)),
                firewall::pk(carol.pk()),
                ter(tesSUCCESS));
            env.close();

            // verifyFirewall(*env.current(), alice, XRP(10), carol.pk());

            env(fset(alice, asfDisableMaster), ter(tecNO_PERMISSION));
            env.close();
        }
    }
    
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
        testFirewallSet(features);
        // testFirewallDelete(features);
        // testUpdateAmount(features);
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

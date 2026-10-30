#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/delegate.h>
#include <test/jtx/fee.h>
#include <test/jtx/sig.h>
#include <test/jtx/ter.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>

namespace xrpl::test {

class FirewallLimits_test : public beast::unit_test::Suite
{
    // A counterparty signature adds one base fee to the minimum.
    static jtx::Fee
    signedFee(jtx::Env& env)
    {
        return jtx::Fee{STAmount{env.current()->fees().base * 2}};
    }

    static UInt256
    firewallID(jtx::Account const& account)
    {
        return keylet::firewall(account.id()).key;
    }

    // The firewall's UnlistedAssets, 0 when absent.
    static std::uint8_t
    unlistedAssets(jtx::Env const& env, jtx::Account const& account)
    {
        auto const sle = env.le(keylet::firewall(account.id()));
        return sle ? (*sle)[~sfUnlistedAssets].value_or(0) : 0;
    }

    void
    testFirewallSetUnlistedAssets(FeatureBitset features)
    {
        testcase("FirewallSet UnlistedAssets");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const dave{"dave"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, bob, carol, dave, backup);
        env.close();

        // Only 0 and 1 are values.
        {
            auto jv = firewall::set(alice.id(), carol.id(), backup.id());
            jv[sfUnlistedAssets.jsonName] = 2;
            env(jv, Ter(temMALFORMED));
        }

        // A create may set it.
        {
            auto jv = firewall::set(bob.id(), carol.id(), backup.id());
            jv[sfUnlistedAssets.jsonName] = 1;
            env(jv);
        }
        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();
        BEAST_EXPECT(unlistedAssets(env, bob) == 1);
        BEAST_EXPECT(unlistedAssets(env, alice) == 0);
        auto const fwID = firewallID(alice);

        auto unlisted = [&](std::uint8_t value) {
            auto jv = firewall::set(alice.id(), fwID);
            jv[sfUnlistedAssets.jsonName] = value;
            return jv;
        };

        // Setting it to 1 alone tightens the firewall: the owner needs no
        // counterparty, and pays no counterparty fee.
        env(unlisted(1));
        env.close();
        BEAST_EXPECT(unlistedAssets(env, alice) == 1);

        // A counterparty signature that is present is still verified.
        env(unlisted(1), Sig(sfCounterpartySignature, dave), signedFee(env), Ter(tefBAD_AUTH));

        // Setting it back to 0 loosens it.
        env(unlisted(0), Ter(temBAD_SIGNER));
        env.close();
        BEAST_EXPECT(unlistedAssets(env, alice) == 1);
        env(unlisted(0), Sig(sfCounterpartySignature, carol), signedFee(env));
        env.close();
        BEAST_EXPECT(unlistedAssets(env, alice) == 0);
        auto const sle = env.le(keylet::firewall(alice.id()));
        BEAST_EXPECT(sle && !sle->isFieldPresent(sfUnlistedAssets));

        // With any other field the update needs the counterparty.
        {
            auto jv = unlisted(1);
            jv[sfMaxFee.jsonName] = to_string(XRP(1).value());
            env(jv, Ter(temBAD_SIGNER));
        }
        {
            auto jv = firewall::set(alice.id(), fwID);
            jv[sfMaxFee.jsonName] = to_string(XRP(1).value());
            env(jv, Ter(temBAD_SIGNER));
        }
        env(firewall::set(alice.id(), fwID), firewall::kCounterparty(dave), Ter(temBAD_SIGNER));
        env.close();
        auto const after = env.le(keylet::firewall(alice.id()));
        BEAST_EXPECT(after && !after->isFieldPresent(sfMaxFee));
        BEAST_EXPECT(after && after->getAccountID(sfCounterparty) == carol.id());
        BEAST_EXPECT(unlistedAssets(env, alice) == 0);
    }

    void
    testDisabled(FeatureBitset features)
    {
        testcase("disabled");
        using namespace jtx;

        auto const firewallLimits = getRegisteredFeature("FirewallLimits");
        if (!BEAST_EXPECT(firewallLimits))
            return;

        Env env{*this, features - *firewallLimits};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, bob, carol, backup);
        env.close();

        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();
        auto const ownersBefore = ownerCount(env, alice);

        // FirewallLimitSet is refused.
        json::Value limit;
        limit[jss::TransactionType] = "FirewallLimitSet";
        limit[jss::Account] = alice.human();
        limit["Asset"][jss::currency] = "XRP";
        limit["LimitAmount"] = "1000000";
        limit["Period"] = 86400;
        env(limit, Ter(temDISABLED));

        // UnlistedAssets is refused on a create and on an update.
        auto create = firewall::set(bob.id(), carol.id(), backup.id());
        create["UnlistedAssets"] = 1;
        env(create, Ter(temDISABLED));

        auto update = firewall::set(alice.id(), keylet::firewall(alice.id()).key);
        update["UnlistedAssets"] = 1;
        env(update, Sig(sfCounterpartySignature, carol), signedFee(env), Ter(temDISABLED));

        // CounterpartySignature is refused on DelegateSet.
        env(delegate::set(alice, bob, {"Payment"}),
            Sig(sfCounterpartySignature, carol),
            signedFee(env),
            Ter(temDISABLED));
        env.close();

        BEAST_EXPECT(ownerCount(env, alice) == ownersBefore);
        BEAST_EXPECT(!env.le(keylet::firewall(bob.id())));
        BEAST_EXPECT(!env.le(keylet::delegate(alice.id(), bob.id())));
    }

public:
    void
    run() override
    {
        auto const all = jtx::testableAmendments();
        testDisabled(all);
        testFirewallSetUnlistedAssets(all);
    }
};

BEAST_DEFINE_TESTSUITE(FirewallLimits, app, xrpl);

}  // namespace xrpl::test

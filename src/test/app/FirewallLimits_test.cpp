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
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/jss.h>

namespace xrpl::test {

class FirewallLimits_test : public beast::unit_test::Suite
{
    // A counterparty signature adds one base fee to the minimum.
    static jtx::Fee
    signedFee(jtx::Env& env)
    {
        return jtx::Fee{STAmount{env.current()->fees().base * 2}};
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
    }
};

BEAST_DEFINE_TESTSUITE(FirewallLimits, app, xrpl);

}  // namespace xrpl::test

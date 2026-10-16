#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/acctdelete.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/noop.h>
#include <test/jtx/offer.h>
#include <test/jtx/owners.h>
#include <test/jtx/paths.h>
#include <test/jtx/pay.h>
#include <test/jtx/sig.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/utility.h>

#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/jss.h>

namespace xrpl::test {

class Firewall_test : public beast::unit_test::Suite
{
    // A counterparty signature adds one base fee to the minimum.
    static inline jtx::Fee const kSignedFee{jtx::drops(20)};

    static uint256
    firewallID(jtx::Account const& account)
    {
        return keylet::firewall(account.id()).key;
    }

    static bool
    hasFirewall(jtx::Env const& env, jtx::Account const& account)
    {
        return env.le(keylet::firewall(account.id())) != nullptr;
    }

    void
    testEnabled(FeatureBitset features)
    {
        testcase("enabled");
        using namespace jtx;

        // Without the amendment the transactions are disabled.
        {
            Env env{*this, features - featureFirewall};
            Account const alice{"alice"};
            Account const carol{"carol"};
            Account const backup{"backup"};
            env.fund(XRP(10000), alice, carol, backup);
            env.close();

            env(firewall::set(alice.id(), carol.id(), backup.id()), Ter(temDISABLED));
            env.close();
            BEAST_EXPECT(!hasFirewall(env, alice));
        }

        // With it, a firewall is created.
        {
            Env env{*this, features};
            Account const alice{"alice"};
            Account const carol{"carol"};
            Account const backup{"backup"};
            env.fund(XRP(10000), alice, carol, backup);
            env.close();

            auto const ownersBefore = ownerCount(env, alice);
            env(firewall::set(alice.id(), carol.id(), backup.id()));
            env.close();

            BEAST_EXPECT(hasFirewall(env, alice));
            // The firewall and the backup preauthorization.
            BEAST_EXPECT(ownerCount(env, alice) == ownersBefore + 2);

            auto const sle = env.le(keylet::firewall(alice.id()));
            BEAST_EXPECT(sle && sle->getAccountID(sfOwner) == alice.id());
            BEAST_EXPECT(sle && sle->getAccountID(sfCounterparty) == carol.id());
        }
    }

    void
    testCreatePreflight(FeatureBitset features)
    {
        testcase("create preflight");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, carol, backup);
        env.close();

        // The counterparty is required.
        {
            auto jv = firewall::set(alice.id(), carol.id(), backup.id());
            jv.removeMember(sfCounterparty.jsonName);
            env(jv, Ter(temMALFORMED));
        }

        // The backup is required.
        {
            auto jv = firewall::set(alice.id(), carol.id(), backup.id());
            jv.removeMember(sfBackup.jsonName);
            env(jv, Ter(temMALFORMED));
        }

        // Neither may be the account itself.
        env(firewall::set(alice.id(), alice.id(), backup.id()), Ter(temMALFORMED));
        env(firewall::set(alice.id(), carol.id(), alice.id()), Ter(temMALFORMED));

        // A creation carries no counterparty signature.
        env(firewall::set(alice.id(), carol.id(), backup.id()),
            Sig(sfCounterpartySignature, carol),
            kSignedFee,
            Ter(temMALFORMED));

        env.close();
        BEAST_EXPECT(!hasFirewall(env, alice));
    }

    void
    testCreatePreclaim(FeatureBitset features)
    {
        testcase("create preclaim");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        Account const absent{"absent"};
        env.fund(XRP(10000), alice, carol, backup);
        env.close();

        // The counterparty and the backup must exist.
        env(firewall::set(alice.id(), absent.id(), backup.id()), Ter(tecNO_DST));
        env(firewall::set(alice.id(), carol.id(), absent.id()), Ter(tecNO_DST));
        env.close();

        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();
        BEAST_EXPECT(hasFirewall(env, alice));

        // Only one firewall per account.
        env(firewall::set(alice.id(), carol.id(), backup.id()), Ter(tecDUPLICATE));
        env.close();
    }

    void
    testUpdate(FeatureBitset features)
    {
        testcase("update");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const dave{"dave"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, carol, dave, backup);
        env.close();

        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();
        auto const fwID = firewallID(alice);

        // An update needs the counterparty's signature.
        env(firewall::set(alice.id(), fwID), Ter(temBAD_SIGNER));

        // Signed by someone other than the recorded counterparty.
        env(firewall::set(alice.id(), fwID),
            Sig(sfCounterpartySignature, dave),
            kSignedFee,
            Ter(tefBAD_AUTH));
        env.close();

        // Signed by the counterparty, changing the counterparty.
        env(firewall::set(alice.id(), fwID),
            firewall::kCounterparty(dave),
            Sig(sfCounterpartySignature, carol),
            kSignedFee);
        env.close();

        auto const sle = env.le(keylet::firewall(alice.id()));
        BEAST_EXPECT(sle && sle->getAccountID(sfCounterparty) == dave.id());

        // The old counterparty can no longer authorize.
        env(firewall::set(alice.id(), fwID),
            Sig(sfCounterpartySignature, carol),
            kSignedFee,
            Ter(tefBAD_AUTH));
        env.close();
    }

    void
    testBlockedAndAllowed(FeatureBitset features)
    {
        testcase("blocked and allowed");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        Account const stranger{"stranger"};
        env.fund(XRP(10000), alice, carol, backup, stranger);
        env.close();

        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();

        // The backup was preauthorized when the firewall was created.
        env(pay(alice, backup, XRP(10)));
        env.close();

        // An unauthorized destination is blocked.
        env(pay(alice, stranger, XRP(10)), Ter(tefFIREWALL_BLOCK));

        // Paths are blocked even to a preauthorized destination, because they
        // can deliver somewhere the preauthorization check never sees. A
        // cross-currency send is used so the pathfinder actually sets sfPaths.
        Account const gw{"gw"};
        env.fund(XRP(10000), gw);
        env.close();
        env.trust(gw["USD"](1000), backup);
        env.close();
        env(offer(gw, XRP(100), gw["USD"](100)));
        env.close();

        env(pay(alice, backup, gw["USD"](10)), Paths(XRP), Ter(tefFIREWALL_BLOCK));

        // A blocked transaction type is rejected whatever its destination.
        env(offer(alice, XRP(10), alice["USD"](10)), Ter(tefFIREWALL_BLOCK));

        // A transaction the firewall allows still works.
        env(noop(alice));
        env.close();

        // An account without a firewall is unaffected.
        env(pay(stranger, alice, XRP(10)));
        env.close();
    }

    void
    testPreauth(FeatureBitset features)
    {
        testcase("preauth");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        Account const stranger{"stranger"};
        env.fund(XRP(10000), alice, carol, backup, stranger);
        env.close();

        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();
        auto const fwID = firewallID(alice);

        env(pay(alice, stranger, XRP(10)), Ter(tefFIREWALL_BLOCK));

        // Preauthorizing needs the counterparty's signature.
        env(firewall::authorize(alice.id(), fwID, stranger.id()), Ter(temMALFORMED));

        env(firewall::authorize(alice.id(), fwID, stranger.id()),
            Sig(sfCounterpartySignature, carol),
            kSignedFee);
        env.close();

        // Now the payment goes through.
        env(pay(alice, stranger, XRP(10)));
        env.close();

        // Authorizing the same destination twice fails.
        env(firewall::authorize(alice.id(), fwID, stranger.id()),
            Sig(sfCounterpartySignature, carol),
            kSignedFee,
            Ter(tecDUPLICATE));

        // An account may not preauthorize itself.
        env(firewall::authorize(alice.id(), fwID, alice.id()),
            Sig(sfCounterpartySignature, carol),
            kSignedFee,
            Ter(temCANNOT_PREAUTH_SELF));
        env.close();

        // Removing the preauthorization blocks it again.
        env(firewall::unauthorize(alice.id(), fwID, stranger.id()),
            Sig(sfCounterpartySignature, carol),
            kSignedFee);
        env.close();

        env(pay(alice, stranger, XRP(10)), Ter(tefFIREWALL_BLOCK));
        env.close();

        // Removing one that does not exist fails.
        env(firewall::unauthorize(alice.id(), fwID, stranger.id()),
            Sig(sfCounterpartySignature, carol),
            kSignedFee,
            Ter(tecNO_ENTRY));
        env.close();
    }

    void
    testMaxFee(FeatureBitset features)
    {
        testcase("max fee");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, carol, backup);
        env.close();

        auto jv = firewall::set(alice.id(), carol.id(), backup.id());
        jv[sfMaxFee.jsonName] = to_string(XRP(1).value());
        env(jv);
        env.close();

        // A fee above the cap is blocked, one at or below it is not.
        env(noop(alice), Fee(XRP(2)), Ter(tefFIREWALL_BLOCK));
        env(noop(alice), Fee(XRP(1)));
        env.close();
    }

    void
    testAccountDelete(FeatureBitset features)
    {
        testcase("account delete");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        env.fund(XRP(10000), alice, carol, backup);
        env.close();

        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();

        // The ledger sequence must advance far enough for AccountDelete.
        for (int i = 0; i < 256; ++i)
            env.close();

        // A firewall is an obligation, so the account cannot be deleted while
        // one is set.
        env(acctdelete(alice, backup),
            Fee(drops(env.current()->fees().increment)),
            Ter(tecHAS_OBLIGATIONS));
        env.close();
    }

    void
    testDelete(FeatureBitset features)
    {
        testcase("delete");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"};
        Account const carol{"carol"};
        Account const backup{"backup"};
        Account const stranger{"stranger"};
        env.fund(XRP(10000), alice, carol, backup, stranger);
        env.close();

        auto const ownersBefore = ownerCount(env, alice);
        env(firewall::set(alice.id(), carol.id(), backup.id()));
        env.close();
        auto const fwID = firewallID(alice);

        env(firewall::authorize(alice.id(), fwID, stranger.id()),
            Sig(sfCounterpartySignature, carol),
            kSignedFee);
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == ownersBefore + 3);

        // A delete needs the counterparty's signature.
        env(firewall::del(alice.id(), fwID), Ter(temMALFORMED));
        env(firewall::del(alice.id(), fwID),
            Sig(sfCounterpartySignature, stranger),
            kSignedFee,
            Ter(tefBAD_AUTH));
        env.close();

        env(firewall::del(alice.id(), fwID), Sig(sfCounterpartySignature, carol), kSignedFee);
        env.close();

        // The firewall and every preauthorization it owned are gone, and the
        // reserve comes back.
        BEAST_EXPECT(!hasFirewall(env, alice));
        BEAST_EXPECT(ownerCount(env, alice) == ownersBefore);

        // Value moves freely again.
        env(pay(alice, stranger, XRP(10)));
        env.close();
    }

public:
    void
    run() override
    {
        using namespace jtx;
        auto const all = jtx::testableAmendments();
        testEnabled(all);
        testCreatePreflight(all);
        testCreatePreclaim(all);
        testUpdate(all);
        testBlockedAndAllowed(all);
        testPreauth(all);
        testMaxFee(all);
        testAccountDelete(all);
        testDelete(all);
    }
};

BEAST_DEFINE_TESTSUITE(Firewall, app, xrpl);

}  // namespace xrpl::test

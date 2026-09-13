#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/acctdelete.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/owners.h>
#include <test/jtx/pay.h>
#include <test/jtx/regkey.h>
#include <test/jtx/sig.h>
#include <test/jtx/ter.h>
#include <test/jtx/utility.h>

#include <xrpl/beast/unit_test.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/jss.h>

namespace xrpl::test {

class Beneficiary_test : public beast::unit_test::Suite
{
    static json::Value
    set(jtx::Account const& account, jtx::Account const& beneficiary, std::uint32_t timeLock)
    {
        json::Value jv;
        jv[sfTransactionType] = jss::BeneficiarySet;
        jv[sfAccount] = account.human();
        jv[sfBeneficiary] = beneficiary.human();
        jv[sfTimeLock] = timeLock;
        return jv;
    }

    static json::Value
    clear(jtx::Account const& account)
    {
        json::Value jv;
        jv[sfTransactionType] = jss::BeneficiarySet;
        jv[sfAccount] = account.human();
        return jv;
    }

    static bool
    exists(jtx::Env const& env, jtx::Account const& account)
    {
        return env.le(keylet::beneficiary(account.id())) != nullptr;
    }

    static std::optional<std::uint32_t>
    stamp(jtx::Env const& env, jtx::Account const& account)
    {
        auto const sle = env.le(keylet::account(account.id()));
        if (!sle)
            return std::nullopt;
        return (*sle)[~sfLastInteraction];
    }

    void
    testEnabled(FeatureBitset features)
    {
        testcase("enabled");
        using namespace jtx;

        for (bool const withFeature : {false, true})
        {
            auto const amend = withFeature ? features : features - featureBeneficiary;
            Env env{*this, amend};
            Account const alice{"alice"}, bob{"bob"};
            env.fund(XRP(10000), alice, bob);
            env.close();

            auto const expected = withFeature ? Ter(tesSUCCESS) : Ter(temDISABLED);
            env(set(alice, bob, 3600), expected);
            env.close();

            if (!withFeature)
            {
                BEAST_EXPECT(!exists(env, alice));
                continue;
            }

            BEAST_EXPECT(exists(env, alice));
            BEAST_EXPECT(stamp(env, alice).has_value());
        }
    }

    void
    testSetMalformed(FeatureBitset features)
    {
        testcase("malformed set");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"}, bob{"bob"}, carol{"carol"};
        env.fund(XRP(10000), alice, bob);
        env.close();

        // An account cannot inherit from itself.
        env(set(alice, alice, 3600), Ter(temMALFORMED));

        // A time lock of zero is invocable in the ledger that sets it.
        env(set(alice, bob, 0), Ter(temMALFORMED));
        env(set(alice, bob, kMaxBeneficiaryTimeLock + 1), Ter(temMALFORMED));

        // The two fields describe one designation and travel together.
        {
            json::Value jv = clear(alice);
            jv[sfTimeLock] = 3600;
            env(jv, Ter(temMALFORMED));
        }
        {
            json::Value jv;
            jv[sfTransactionType] = jss::BeneficiarySet;
            jv[sfAccount] = alice.human();
            jv[sfBeneficiary] = bob.human();
            env(jv, Ter(temMALFORMED));
        }

        // The beneficiary has to be an account that exists.
        env(set(alice, carol, 3600), Ter(tecNO_TARGET));

        // Clearing when there is nothing to clear.
        env(clear(alice), Ter(tecNO_ENTRY));
        env.close();

        BEAST_EXPECT(!exists(env, alice));
        BEAST_EXPECT(!stamp(env, alice).has_value());

        // The ceiling itself is allowed.
        env(set(alice, bob, kMaxBeneficiaryTimeLock));
        env.close();
        BEAST_EXPECT(exists(env, alice));
    }

    void
    testSetUpdateClear(FeatureBitset features)
    {
        testcase("set, update and clear");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"}, bob{"bob"}, carol{"carol"};
        env.fund(XRP(10000), alice, bob, carol);
        env.close();

        BEAST_EXPECT(ownerCount(env, alice) == 0);

        env(set(alice, bob, 3600));
        env.close();
        BEAST_EXPECT(exists(env, alice));
        BEAST_EXPECT(ownerCount(env, alice) == 1);
        {
            auto const sle = env.le(keylet::beneficiary(alice.id()));
            BEAST_EXPECT((*sle)[sfAccount] == alice.id());
            BEAST_EXPECT((*sle)[sfBeneficiary] == bob.id());
            BEAST_EXPECT((*sle)[sfTimeLock] == 3600);
        }

        // Updating overwrites in place: still one entry, still one reserve.
        env(set(alice, carol, 7200));
        env.close();
        BEAST_EXPECT(ownerCount(env, alice) == 1);
        {
            auto const sle = env.le(keylet::beneficiary(alice.id()));
            BEAST_EXPECT((*sle)[sfBeneficiary] == carol.id());
            BEAST_EXPECT((*sle)[sfTimeLock] == 7200);
        }

        // Clearing leaves the account as it was before any designation.
        env(clear(alice));
        env.close();
        BEAST_EXPECT(!exists(env, alice));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(!stamp(env, alice).has_value());
    }

    // The beneficiary becomes a second regular key once the time lock has run.
    // Nothing on the account changes: no key is replaced, no entry deleted.
    void
    testBeneficiarySigns(FeatureBitset features)
    {
        testcase("the beneficiary signs once the time lock has run");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"}, bob{"bob"}, carol{"carol"};
        env.fund(XRP(10000), alice, bob, carol);
        env.close();

        env(set(alice, bob, 3600));
        env.close();

        // Before the time lock, the beneficiary is just another account.
        env(pay(alice, carol, XRP(1)), Sig(bob), Ter(tefBAD_AUTH));
        env.close();

        env.close(std::chrono::seconds(4000));

        // After it, the beneficiary signs for the account.
        auto const carolBefore = env.balance(carol);
        env(pay(alice, carol, XRP(1)), Sig(bob));
        env.close();
        BEAST_EXPECT(env.balance(carol) == carolBefore + XRP(1));

        // The account is untouched: no regular key was set and the designation
        // is still there.
        auto const sle = env.le(keylet::account(alice.id()));
        BEAST_EXPECT(!sle->isFieldPresent(sfRegularKey));
        BEAST_EXPECT(exists(env, alice));
        BEAST_EXPECT(ownerCount(env, alice) == 1);

        // An unrelated account still cannot sign.
        env(pay(alice, carol, XRP(1)), Sig(carol), Ter(tefBAD_AUTH));
        env.close();
    }

    // The beneficiary's own transactions must not reset the timer, or the first
    // one would shut the door behind it.
    void
    testBeneficiarySigningDoesNotResetTheTimer(FeatureBitset features)
    {
        testcase("a beneficiary-signed transaction does not reset the timer");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"}, bob{"bob"}, carol{"carol"};
        env.fund(XRP(10000), alice, bob, carol);
        env.close();

        env(set(alice, bob, 3600));
        env.close();
        auto const stampAtSet = stamp(env, alice);
        env.close(std::chrono::seconds(4000));

        env(pay(alice, carol, XRP(1)), Sig(bob));
        env.close();
        BEAST_EXPECT(stamp(env, alice) == stampAtSet);

        // So the beneficiary can keep signing.
        env(pay(alice, carol, XRP(1)), Sig(bob));
        env.close();
        BEAST_EXPECT(stamp(env, alice) == stampAtSet);
    }

    // The owner is never locked out: signing with their own key closes the
    // beneficiary's access again.
    void
    testOwnerReclaims(FeatureBitset features)
    {
        testcase("the owner reclaims by signing");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"}, bob{"bob"}, carol{"carol"};
        env.fund(XRP(10000), alice, bob, carol);
        env.close();

        env(set(alice, bob, 3600));
        env.close();
        env.close(std::chrono::seconds(4000));

        env(pay(alice, carol, XRP(1)), Sig(bob));
        env.close();

        // Alice comes back and uses her own key.
        env(pay(alice, carol, XRP(1)));
        env.close();

        // Bob is shut out again, and Alice never lost anything.
        env(pay(alice, carol, XRP(1)), Sig(bob), Ter(tefBAD_AUTH));
        env.close();
        BEAST_EXPECT(exists(env, alice));
    }

    // The case that drove this design: an owner who signs with a regular key
    // and goes quiet keeps their account.
    void
    testRegularKeyOwnerNotLockedOut(FeatureBitset features)
    {
        testcase("an owner signing with a regular key is not locked out");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"}, bob{"bob"}, carol{"carol"}, key{"key"};
        env.fund(XRP(10000), alice, bob, carol, key);
        env.close();

        env(regkey(alice, key));
        env.close();
        env(fset(alice, asfDisableMaster), Sig(alice));
        env.close();

        env(set(alice, bob, 3600), Sig(key));
        env.close();
        env.close(std::chrono::seconds(4000));

        // The beneficiary can sign now.
        env(pay(alice, carol, XRP(1)), Sig(bob));
        env.close();

        // And so can the owner, with the regular key they have always used.
        // The regular key was never overwritten.
        BEAST_EXPECT((*env.le(keylet::account(alice.id())))[~sfRegularKey] == key.id());
        env(pay(alice, carol, XRP(1)), Sig(key));
        env.close();

        env(pay(alice, carol, XRP(1)), Sig(bob), Ter(tefBAD_AUTH));
        env.close();
    }

    // The whole point of the mechanism: using the account holds it off.
    void
    testActivityResetsTheTimer(FeatureBitset features)
    {
        testcase("activity resets the timer");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"}, bob{"bob"};
        env.fund(XRP(10000), alice, bob);
        env.close();

        env(set(alice, bob, 3600));
        env.close();
        auto const first = stamp(env, alice);
        BEAST_EXPECT(first.has_value());

        env.close(std::chrono::seconds(3000));

        // An ordinary outgoing payment, nothing to do with this amendment.
        env(pay(alice, bob, XRP(1)));
        env.close();
        auto const second = stamp(env, alice);
        BEAST_EXPECT(second.has_value() && *second > *first);

        // The original deadline has now passed, and the beneficiary still
        // cannot sign, because the payment moved it.
        env.close(std::chrono::seconds(1000));
        env(pay(alice, bob, XRP(1)), Sig(bob), Ter(tefBAD_AUTH));
        env.close();

        env.close(std::chrono::seconds(4000));
        env(pay(alice, bob, XRP(1)), Sig(bob));
        env.close();
    }

    // Receiving is not activity: only the sender's own transactions count.
    void
    testIncomingIsNotActivity(FeatureBitset features)
    {
        testcase("an incoming payment is not activity");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"}, bob{"bob"};
        env.fund(XRP(10000), alice, bob);
        env.close();

        env(set(alice, bob, 3600));
        env.close();
        auto const before = stamp(env, alice);

        env.close(std::chrono::seconds(2000));
        env(pay(bob, alice, XRP(100)));
        env.close();
        BEAST_EXPECT(stamp(env, alice) == before);

        env.close(std::chrono::seconds(2000));
        env(pay(alice, bob, XRP(1)), Sig(bob));
        env.close();
    }

    // An account that never set a beneficiary is untouched by the amendment.
    void
    testUnrelatedAccountUnstamped(FeatureBitset features)
    {
        testcase("an account without a designation is never stamped");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"}, bob{"bob"};
        env.fund(XRP(10000), alice, bob);
        env.close();

        env(pay(alice, bob, XRP(1)));
        env.close();
        BEAST_EXPECT(!stamp(env, alice).has_value());
        BEAST_EXPECT(!stamp(env, bob).has_value());
    }

    void
    testAccountDeleteBlocked(FeatureBitset features)
    {
        testcase("a designation blocks account deletion");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"}, bob{"bob"}, sink{"sink"};
        env.fund(XRP(10000), alice, bob, sink);
        env.close();

        env(set(alice, bob, 3600));
        env.close();

        incLgrSeqForAccDel(env, alice);
        env(acctdelete(alice, sink),
            Fee(drops(env.current()->fees().increment)),
            Ter(tecHAS_OBLIGATIONS));
        env.close();

        // With the designation cleared, the same account deletes.
        env(clear(alice));
        env.close();
        env(acctdelete(alice, sink), Fee(drops(env.current()->fees().increment)));
        env.close();
        BEAST_EXPECT(!env.le(keylet::account(alice.id())));
    }

    void
    testRpc(FeatureBitset features)
    {
        testcase("account_objects and ledger_entry");
        using namespace jtx;

        Env env{*this, features};
        Account const alice{"alice"}, bob{"bob"};
        env.fund(XRP(10000), alice, bob);
        env.close();

        env(set(alice, bob, 3600));
        env.close();

        {
            json::Value params;
            params[jss::account] = alice.human();
            params[jss::type] = "beneficiary";
            auto const jv = env.rpc("json", "account_objects", to_string(params))[jss::result];
            BEAST_EXPECT(jv[jss::account_objects].size() == 1);
            auto const& object = jv[jss::account_objects][0u];
            BEAST_EXPECT(object["LedgerEntryType"].asString() == "Beneficiary");
            BEAST_EXPECT(object["Beneficiary"].asString() == bob.human());
        }

        // The beneficiary does not own the entry, so it is not in their
        // directory.
        {
            json::Value params;
            params[jss::account] = bob.human();
            params[jss::type] = "beneficiary";
            auto const jv = env.rpc("json", "account_objects", to_string(params))[jss::result];
            BEAST_EXPECT(jv[jss::account_objects].size() == 0);
        }

        {
            json::Value params;
            params[jss::beneficiary] = alice.human();
            auto const jv = env.rpc("json", "ledger_entry", to_string(params))[jss::result];
            BEAST_EXPECT(
                jv[jss::index].asString() == to_string(keylet::beneficiary(alice.id()).key));
        }
    }

public:
    void
    run() override
    {
        using namespace jtx;
        auto const all = jtx::testableAmendments();
        testEnabled(all);
        testSetMalformed(all);
        testSetUpdateClear(all);
        testBeneficiarySigns(all);
        testBeneficiarySigningDoesNotResetTheTimer(all);
        testOwnerReclaims(all);
        testRegularKeyOwnerNotLockedOut(all);
        testActivityResetsTheTimer(all);
        testIncomingIsNotActivity(all);
        testUnrelatedAccountUnstamped(all);
        testAccountDeleteBlocked(all);
        testRpc(all);
    }
};

BEAST_DEFINE_TESTSUITE(Beneficiary, app, xrpl);

}  // namespace xrpl::test

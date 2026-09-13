#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/acctdelete.h>
#include <test/jtx/amount.h>
#include <test/jtx/delegate.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/multisign.h>
#include <test/jtx/pay.h>
#include <test/jtx/rate.h>
#include <test/jtx/regkey.h>
#include <test/jtx/seq.h>
#include <test/jtx/sig.h>
#include <test/jtx/subscription.h>
#include <test/jtx/tag.h>
#include <test/jtx/ter.h>
#include <test/jtx/ticket.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/ledger/Dir.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/tx/applySteps.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <utility>

namespace xrpl::test {
struct Subscription_test : public beast::unit_test::Suite
{
    static uint256
    getSubscriptionIndex(AccountID const& account, AccountID const& dest, std::uint32_t uSequence)
    {
        return keylet::subscription(account, dest, uSequence).key;
    }

    static bool
    inOwnerDir(
        ReadView const& view,
        jtx::Account const& acct,
        std::shared_ptr<SLE const> const& token)
    {
        Dir const ownerDir(view, keylet::ownerDir(acct.id()));
        return std::find(ownerDir.begin(), ownerDir.end(), token) != ownerDir.end();
    }

    static std::size_t
    ownerDirCount(ReadView const& view, jtx::Account const& acct)
    {
        Dir const ownerDir(view, keylet::ownerDir(acct.id()));
        return std::distance(ownerDir.begin(), ownerDir.end());
    };

    static std::pair<uint256, std::shared_ptr<SLE const>>
    subKeyAndSle(ReadView const& view, uint256 const& subId)
    {
        auto const sle = view.read(keylet::subscription(subId));
        if (!sle)
            return {};
        return {sle->key(), sle};
    }

    bool
    subscriptionExists(ReadView const& view, uint256 const& subId)
    {
        auto const slep = view.read({ltSUBSCRIPTION, subId});
        return bool(slep);
    }

    jtx::PrettyAmount
    issuerBalance(jtx::Env& env, jtx::Account const& account, Issue const& issue)
    {
        json::Value params;
        params[jss::account] = account.human();
        auto jrr = env.rpc("json", "gateway_balances", to_string(params));
        auto const result = jrr[jss::result];
        auto const obligations = result[jss::obligations][to_string(issue.currency)];
        if (obligations.isNull())
            return {STAmount(issue, 0), account.name()};
        STAmount const amount = amountFromString(issue, obligations.asString());
        return {amount, account.name()};
    }

    std::uint32_t
    getNextPaymentTime(ReadView const& view, uint256 const& subId)
    {
        auto const [_, sleSub] = subKeyAndSle(view, subId);
        return sleSub->getFieldU32(sfNextClaimTime);
    }

    void
    validateSubscription(
        jtx::Env& env,
        uint256 const& subId,
        STAmount const& amount,
        STAmount const& balance,
        std::uint32_t const& frequency,
        std::uint32_t const& nextClaimTime)
    {
        auto const [id, sle] = subKeyAndSle(*env.current(), subId);
        BEAST_EXPECT(sle);
        BEAST_EXPECT(sle->getFieldAmount(sfAmount) == amount);
        BEAST_EXPECT(sle->getFieldAmount(sfBalance) == balance);
        BEAST_EXPECT(sle->getFieldU32(sfFrequency) == frequency);
        BEAST_EXPECT(sle->getFieldU32(sfNextClaimTime) == nextClaimTime);
    }

    void
    testEnabled(FeatureBitset features)
    {
        testcase("enabled");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        // setup env
        auto const alice = Account("alice");
        auto const bob = Account("bob");

        for (bool const withSubscription : {true, false})
        {
            auto const amend = withSubscription ? features : features - featureSubscription;
            Env env{*this, amend};

            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const txResult = withSubscription ? Ter(tesSUCCESS) : Ter(temDISABLED);
            auto const ownerDir = withSubscription ? 1 : 0;

            auto const aliceSeq = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, aliceSeq);

            // SET - (Create)
            auto const frequency = 100s;
            env(subscription::create(alice, bob, XRP(10), frequency), txResult);
            env.close();

            BEAST_EXPECT(
                withSubscription ? subscriptionExists(*env.current(), subId)
                                 : !subscriptionExists(*env.current(), subId));
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == ownerDir);
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == ownerDir);

            // CLAIM
            env(subscription::claim(bob, subId, XRP(1)), txResult);
            env.close();

            BEAST_EXPECT(
                withSubscription ? subscriptionExists(*env.current(), subId)
                                 : !subscriptionExists(*env.current(), subId));
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == ownerDir);
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == ownerDir);

            // CANCEL
            env(subscription::cancel(alice, subId), txResult);
            env.close();

            BEAST_EXPECT(!subscriptionExists(*env.current(), subId));
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 0);
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 0);
        }
    }

    void
    testSetPreflightInvalid(FeatureBitset features)
    {
        testcase("set preflight invalid");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        // setup env
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        Env env{*this, features};
        env.fund(XRP(1000), alice, bob, gw);
        env.close();
        env(trust(alice, USD(10000)));
        env(trust(bob, USD(10000)));
        env.close();
        env(pay(gw, alice, USD(1000)));
        env(pay(gw, bob, USD(1000)));
        env.close();

        /*
        CREATE
        */

        // temINVALID_FLAG
        {
            env(subscription::create(alice, bob, XRP(10), 100s),
                Txflags(0x00020000),
                Ter(temINVALID_FLAG));
            env.close();
        }

        // temBAD_FEE: Exercises invalid preflight1
        {
            env(subscription::create(alice, bob, XRP(10), 100s), Fee(XRP(-1)), Ter(temBAD_FEE));
            env.close();
        }

        // temMALFORMED: no sfDestination
        {
            json::Value txn;
            txn[jss::TransactionType] = jss::SubscriptionSet;
            txn[jss::Account] = alice.human();
            txn[sfAmount.jsonName] = XRP(10).value().getJson(JsonOptions::Values::None);
            NetClock::duration const frequency = 100s;
            txn[sfFrequency.jsonName] = frequency.count();
            env(txn, Ter(temMALFORMED));
            env.close();
        }

        // temMALFORMED: no sfAmount
        {
            json::Value txn;
            txn[jss::TransactionType] = jss::SubscriptionSet;
            txn[jss::Account] = alice.human();
            txn[sfDestination.jsonName] = bob.human();
            NetClock::duration const frequency = 100s;
            txn[sfFrequency.jsonName] = frequency.count();
            env(txn, Ter(temMALFORMED));
            env.close();
        }

        // temMALFORMED: no sfFrequency
        {
            json::Value txn;
            txn[jss::TransactionType] = jss::SubscriptionSet;
            txn[jss::Account] = alice.human();
            txn[sfDestination.jsonName] = bob.human();
            txn[sfAmount.jsonName] = XRP(10).value().getJson(JsonOptions::Values::None);
            env(txn, Ter(temMALFORMED));
            env.close();
        }

        // temDST_IS_SRC
        {
            env(subscription::create(alice, alice, XRP(10), 100s), Ter(temDST_IS_SRC));
            env.close();
        }

        /*
        UPDATE
        */

        // temMALFORMED: sfDestination present with sfSubscriptionID
        {
            auto const aliceSeq = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, aliceSeq);
            json::Value txn = subscription::update(alice, subId, XRP(10));
            txn[sfDestination.jsonName] = bob.human();
            env(txn, Ter(temMALFORMED));
            env.close();
        }

        // temMALFORMED: sfStartTime present with sfSubscriptionID
        {
            auto const aliceSeq = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, aliceSeq);
            json::Value txn = subscription::update(alice, subId, XRP(10));
            auto const startTime = env.now() + 0s;
            txn[sfStartTime.jsonName] = to_string(startTime.time_since_epoch().count());
            env(txn, Ter(temMALFORMED));
            env.close();
        }

        /*
        BOTH CREATE AND UPDATE
        */

        //----------------------------------------------------------------------
        // XRP

        // temBAD_AMOUNT: negative XRP
        {
            env(subscription::create(alice, bob, XRP(-10), 100s), Ter(temBAD_AMOUNT));
            env.close();
        }

        // temBAD_AMOUNT: zero XRP
        {
            env(subscription::create(alice, bob, XRP(0), 100s), Ter(temBAD_AMOUNT));
            env.close();
        }
    }

    void
    testSetPreclaimInvalid(FeatureBitset features)
    {
        testcase("set preclaim invalid");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        // setup env
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const dne = Account("dne");

        Env env{*this, features};

        env.fund(XRP(1000), alice, bob);
        env.close();

        env.memoize(dne);

        /*
        CREATE
        */

        // tecNO_DST
        {
            env(subscription::create(alice, dne, XRP(10), 100s), Ter(tecNO_DST));
            env.close();
        }

        // tecNO_PERMISSION: start time in the past
        {
            auto const start = env.now() - 10s;
            env(subscription::create(alice, bob, XRP(10), 100s),
                subscription::StartTime(start),
                Ter(tecNO_PERMISSION));
            env.close();
        }

        // tecEXPIRED: expiration in the past
        {
            auto const expire = env.now() - 10s;
            env(subscription::create(alice, bob, XRP(10), 100s, expire), Ter(tecEXPIRED));
            env.close();
        }

        // tecEXPIRED: expiration before start time
        {
            auto const start = env.now() + 100s;
            auto const expire = env.now() + 50s;
            env(subscription::create(alice, bob, XRP(10), 100s, expire),
                subscription::StartTime(start),
                Ter(tecEXPIRED));
            env.close();
        }

        // tecDST_TAG_NEEDED
        {
            env(fset(bob, asfRequireDest));
            env.close();

            env(subscription::create(alice, bob, XRP(10), 100s), Ter(tecDST_TAG_NEEDED));
            env.close();

            // clear flag for other tests
            env(fclear(bob, asfRequireDest));
            env.close();
        }

        /*
        UPDATE
        */

        // tecNO_ENTRY: subscription doesn't exist
        {
            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::update(alice, subId, XRP(100)), Ter(tecNO_ENTRY));
            env.close();
        }

        // tecNO_PERMISSION: non-owner tries to update
        {
            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::create(alice, bob, XRP(100), 100s));
            env.close();

            env(subscription::update(bob, subId, XRP(100)), Ter(tecNO_PERMISSION));
            env.close();
        }

        // tecEXPIRED: update with past expiration
        {
            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::create(alice, bob, XRP(100), 100s));
            env.close();

            auto const expire = env.now() - 10s;
            env(subscription::update(alice, subId, XRP(100), expire), Ter(tecEXPIRED));
            env.close();
        }
    }

    void
    testSetDoApplyInvalid(FeatureBitset features)
    {
        testcase("set doApply invalid");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        // setup env
        auto const alice = Account("alice");
        auto const bob = Account("bob");

        Env env{*this, features};

        /*
        CREATE
        */

        // tecINSUFFICIENT_RESERVE
        {
            auto const reserve = env.current()->fees().accountReserve(0, 1);
            auto const incReserve = env.current()->fees().increment;

            env.fund(reserve + incReserve - XRP(1), alice);
            env.fund(XRP(1000), bob);
            env.close();

            env(subscription::create(alice, bob, XRP(10), 100s), Ter(tecINSUFFICIENT_RESERVE));
            env.close();
        }
    }

    void
    testCancelPreflightInvalid(FeatureBitset features)
    {
        testcase("cancel preflight invalid");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        // setup env
        auto const alice = Account("alice");
        auto const bob = Account("bob");

        Env env{*this, features};

        env.fund(XRP(1000), alice, bob);
        env.close();

        auto const aliceSeq = env.seq(alice);
        auto const subId = getSubscriptionIndex(alice, bob, aliceSeq);

        // temINVALID_FLAG
        {
            env(subscription::cancel(alice, subId), Txflags(tfSetfAuth), Ter(temINVALID_FLAG));
            env.close();
        }
    }

    void
    testCancelPreclaimInvalid(FeatureBitset features)
    {
        testcase("cancel preclaim invalid");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        // setup env
        auto const alice = Account("alice");
        auto const bob = Account("bob");

        Env env{*this, features};

        env.fund(XRP(1000), alice, bob);
        env.close();

        auto const aliceSeq = env.seq(alice);
        auto const subId = getSubscriptionIndex(alice, bob, aliceSeq);

        // tecNO_ENTRY
        {
            env(subscription::cancel(alice, subId), Ter(tecNO_ENTRY));
            env.close();
        }
        BEAST_EXPECT(1 == 1);
    }

    void
    testClaimPreflightInvalid(FeatureBitset features)
    {
        testcase("claim preflight invalid");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        // setup env
        auto const alice = Account("alice");
        auto const bob = Account("bob");

        Env env{*this, features};

        env.fund(XRP(1000), alice, bob);
        env.close();

        auto const aliceSeq = env.seq(alice);
        auto const subId = getSubscriptionIndex(alice, bob, aliceSeq);

        // temINVALID_FLAG
        {
            env(subscription::claim(bob, subId, XRP(10)),
                Txflags(tfSetfAuth),
                Ter(temINVALID_FLAG));
            env.close();
        }
    }

    void
    testClaimPreclaimInvalid(FeatureBitset features)
    {
        testcase("claim preclaim invalid");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        // setup env
        auto const alice = Account("alice");
        auto const bob = Account("bob");

        Env env{*this, features};

        env.fund(XRP(1000), alice, bob);
        env.close();

        // tecNO_ENTRY: subscription doesn't exist
        {
            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::claim(bob, subId, XRP(10)), Ter(tecNO_ENTRY));
            env.close();
        }

        // tecNO_PERMISSION: wrong destination
        {
            auto const carol = Account("carol");
            env.fund(XRP(1000), carol);
            env.close();

            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::create(alice, bob, XRP(10), 100s));
            env.close();

            env(subscription::claim(carol, subId, XRP(1)), Ter(tecNO_PERMISSION));
            env.close();
        }

        // tecWRONG_ASSET: wrong currency/asset
        {
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(1000), gw);
            env.close();
            env.trust(USD(10000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(1000)));
            env.close();

            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::create(alice, bob, XRP(10), 100s));
            env.close();

            // Try to claim with wrong currency
            env(subscription::claim(bob, subId, USD(1)), Ter(tecWRONG_ASSET));
            env.close();
        }

        // tecLIMIT_EXCEEDED: claim more than subscription amount
        {
            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::create(alice, bob, XRP(10), 100s));
            env.close();

            env(subscription::claim(bob, subId, XRP(11)), Ter(tecLIMIT_EXCEEDED));
            env.close();
        }

        // tecUNFUNDED: insufficient subscription balance
        {
            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::create(alice, bob, XRP(10), 100s));
            env.close();

            env(subscription::claim(bob, subId, XRP(1)));
            env.close();

            env(subscription::claim(bob, subId, XRP(11)), Ter(tecLIMIT_EXCEEDED));
            env.close();
        }

        // tecTOO_SOON: subscription hasn't reached next payment time
        {
            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            auto const startTime = env.now() + 1000s;
            env(subscription::create(alice, bob, XRP(10), 100s),
                subscription::StartTime(startTime));
            env.close();

            env(subscription::claim(bob, subId, XRP(10)), Ter(tecTOO_SOON));
            env.close();
        }
    }

    void
    testClaimDoApplyInvalid(FeatureBitset features)
    {
        testcase("claim doApply invalid");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        // setup env
        auto const alice = Account("alice");
        auto const bob = Account("bob");

        Env env{*this, features};

        env.fund(XRP(1000), alice, bob);
        env.close();

        // tecNO_PERMISSION: account claims own subscription
        {
            auto const aliceSeq = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, aliceSeq);

            env(subscription::create(alice, bob, XRP(10), 100s));
            env.close();

            env(subscription::claim(alice, subId, XRP(1)), Ter(tecNO_PERMISSION));
            env.close();
        }

        // tecINSUFFICIENT_FUNDS: XRP
        {
            auto const aliceSeq = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, aliceSeq);

            env(subscription::create(alice, bob, XRP(1000), 100s));
            env.close();

            env(subscription::claim(bob, subId, XRP(1000)), Ter(tecINSUFFICIENT_FUNDS));
            env.close();
        }
    }

    void
    testSet(FeatureBitset features)
    {
        testcase("set");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        // setup env
        auto const alice = Account("alice");
        auto const bob = Account("bob");

        Env env{*this, features};

        env.fund(XRP(1000), alice, bob);
        env.close();

        // No StartTime & No Expiration
        {
            auto const aliceSeq = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, aliceSeq);

            auto const startTime = env.now();
            auto const frequency = 100s;
            env(subscription::create(alice, bob, XRP(10), frequency));
            env.close();

            auto const [key, subSle] = subKeyAndSle(*env.current(), subId);
            BEAST_EXPECT(subSle->getFieldAmount(sfAmount) == XRP(10));
            BEAST_EXPECT(subSle->getFieldU32(sfFrequency) == frequency.count());
            BEAST_EXPECT(
                subSle->getFieldU32(sfNextClaimTime) == startTime.time_since_epoch().count());
            BEAST_EXPECT(!subSle->isFieldPresent(sfExpiration));
        }

        // StartTime & Expiration
        {
            auto const aliceSeq = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, aliceSeq);

            auto const startTime = env.now() + 100s;
            auto const expiration = env.now() + 300s;
            auto const frequency = 100s;
            env(subscription::create(alice, bob, XRP(10), frequency, expiration),
                subscription::StartTime(startTime));
            env.close();

            auto const [key, subSle] = subKeyAndSle(*env.current(), subId);
            BEAST_EXPECT(subSle->getFieldAmount(sfAmount) == XRP(10));
            BEAST_EXPECT(subSle->getFieldU32(sfFrequency) == frequency.count());
            BEAST_EXPECT(
                subSle->getFieldU32(sfNextClaimTime) == startTime.time_since_epoch().count());
            BEAST_EXPECT(
                subSle->getFieldU32(sfExpiration) == expiration.time_since_epoch().count());
        }
    }

    void
    testUpdate(FeatureBitset features)
    {
        testcase("update");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        // setup env
        auto const alice = Account("alice");
        auto const bob = Account("bob");

        Env env{*this, features};

        env.fund(XRP(1000), alice, bob);
        env.close();

        // Update Amount
        {
            auto const aliceSeq = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, aliceSeq);

            env(subscription::create(alice, bob, XRP(10), 100s));
            env.close();

            env(subscription::update(alice, subId, XRP(11)));
            env.close();

            auto const [key, subSle] = subKeyAndSle(*env.current(), subId);
            BEAST_EXPECT(subSle->getFieldAmount(sfAmount) == XRP(11));
        }

        // Update Expiration
        {
            auto const aliceSeq = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, aliceSeq);

            env(subscription::create(alice, bob, XRP(10), 100s));
            env.close();

            auto const expire = env.now() + 10s;
            env(subscription::update(alice, subId, XRP(10), expire));
            env.close();

            auto const [key, subSle] = subKeyAndSle(*env.current(), subId);
            BEAST_EXPECT(subSle->getFieldAmount(sfAmount) == XRP(10));
            BEAST_EXPECT(subSle->getFieldU32(sfExpiration) == expire.time_since_epoch().count());
        }
    }

    void
    testCancel(FeatureBitset features)
    {
        testcase("cancel");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        // Cancel Account
        {
            // setup env
            auto const alice = Account("alice");
            auto const bob = Account("bob");

            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;

            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            env(subscription::create(alice, bob, XRP(10), 100s));
            env.close();

            env(subscription::cancel(alice, subId));
            env.close();

            BEAST_EXPECT(env.balance(alice) == preAlice - (baseFee * 2));
            BEAST_EXPECT(env.balance(bob) == preBob);
            BEAST_EXPECT(!subscriptionExists(*env.current(), subId));
        }

        // Cancel Destination
        {
            // setup env
            auto const alice = Account("alice");
            auto const bob = Account("bob");

            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;

            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            env(subscription::create(alice, bob, XRP(10), 100s));
            env.close();

            env(subscription::cancel(bob, subId));
            env.close();

            BEAST_EXPECT(env.balance(alice) == preAlice - baseFee);
            BEAST_EXPECT(env.balance(bob) == preBob - baseFee);
            BEAST_EXPECT(!subscriptionExists(*env.current(), subId));
        }
    }

    void
    testClaim(FeatureBitset features)
    {
        testcase("claim");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        // First Claim Partial & Second Claim Full
        {
            auto const alice = Account("alice");
            auto const bob = Account("bob");

            Env env{*this, features};
            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const aliceSeq = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, aliceSeq);

            auto const frequency = 100s;
            auto const startTime = env.now().time_since_epoch().count();
            env(subscription::create(alice, bob, XRP(10), frequency));
            env.close();

            validateSubscription(env, subId, XRP(10), XRP(10), frequency.count(), startTime);

            auto preAlice = env.balance(alice);
            auto preBob = env.balance(bob);

            // First Partial claim
            env(subscription::claim(bob, subId, XRP(5)));
            env.close();

            validateSubscription(env, subId, XRP(10), XRP(5), frequency.count(), startTime);
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(5));
            BEAST_EXPECT(env.balance(bob) == preBob - env.current()->fees().base + XRP(5));

            preAlice = env.balance(alice);
            preBob = env.balance(bob);

            // Claim too soon, do not have sufficient funds
            env(subscription::claim(bob, subId, XRP(10)), Ter(tecINSUFFICIENT_FUNDS));
            env.close();

            validateSubscription(env, subId, XRP(10), XRP(5), frequency.count(), startTime);
            BEAST_EXPECT(
                env.now().time_since_epoch().count() <
                getNextPaymentTime(*env.current(), subId) + frequency.count());

            // Advance time
            env.close(60s);
            BEAST_EXPECT(
                env.now().time_since_epoch().count() ==
                getNextPaymentTime(*env.current(), subId) + frequency.count());

            // Can claim full amount
            env(subscription::claim(bob, subId, XRP(10)));
            env.close();

            validateSubscription(
                env,
                subId,
                XRP(10),
                XRP(10),
                frequency.count(),
                startTime + (frequency.count() * 2));
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(10));
            BEAST_EXPECT(env.balance(bob) == preBob - (env.current()->fees().base * 2) + XRP(10));

            // Cannot claim again yet
            env(subscription::claim(bob, subId, XRP(10)), Ter(tecTOO_SOON));
            env.close();
        }

        // First Claim Full & Second Claim Full
        {
            auto const alice = Account("alice");
            auto const bob = Account("bob");

            Env env{*this, features};
            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const aliceSeq = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, aliceSeq);

            auto const frequency = 100s;
            auto const startTime = env.now().time_since_epoch().count();
            env(subscription::create(alice, bob, XRP(10), frequency));
            env.close();

            validateSubscription(env, subId, XRP(10), XRP(10), frequency.count(), startTime);

            auto preAlice = env.balance(alice);
            auto preBob = env.balance(bob);

            // First Partial claim
            env(subscription::claim(bob, subId, XRP(10)));
            env.close();

            validateSubscription(
                env, subId, XRP(10), XRP(10), frequency.count(), startTime + frequency.count());
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(10));
            BEAST_EXPECT(env.balance(bob) == preBob - env.current()->fees().base + XRP(10));

            preAlice = env.balance(alice);
            preBob = env.balance(bob);

            // Cannot claim full amount yet
            env(subscription::claim(bob, subId, XRP(10)), Ter(tecTOO_SOON));
            env.close();

            validateSubscription(
                env, subId, XRP(10), XRP(10), frequency.count(), startTime + frequency.count());
            BEAST_EXPECT(
                env.now().time_since_epoch().count() < getNextPaymentTime(*env.current(), subId));

            // Advance time
            env.close(60s);
            BEAST_EXPECT(
                env.now().time_since_epoch().count() == getNextPaymentTime(*env.current(), subId));

            // Can claim full amount
            env(subscription::claim(bob, subId, XRP(10)));
            env.close();

            validateSubscription(
                env,
                subId,
                XRP(10),
                XRP(10),
                frequency.count(),
                startTime + (frequency.count() * 2));
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(10));
            BEAST_EXPECT(env.balance(bob) == preBob - (env.current()->fees().base * 2) + XRP(10));

            // Cannot claim again yet
            env(subscription::claim(bob, subId, XRP(10)), Ter(tecTOO_SOON));
            env.close();
        }

        // Test Arrears
        {
            auto const alice = Account("alice");
            auto const bob = Account("bob");

            Env env{*this, features};
            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const aliceSeq = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, aliceSeq);

            auto const frequency = 100s;
            auto const startTime = env.now().time_since_epoch().count();
            env(subscription::create(alice, bob, XRP(10), frequency));
            env.close();

            validateSubscription(env, subId, XRP(10), XRP(10), frequency.count(), startTime);

            auto preAlice = env.balance(alice);
            auto preBob = env.balance(bob);

            // Advance time 3x
            env.close(frequency);
            env.close(frequency);
            env.close(frequency);
            BEAST_EXPECT(
                env.now().time_since_epoch().count() >
                getNextPaymentTime(*env.current(), subId) + frequency.count() * 3);

            for (int i = 0; i < 4; ++i)
            {
                // Can claim full amount
                env(subscription::claim(bob, subId, XRP(10)));
                env.close();
                validateSubscription(
                    env,
                    subId,
                    XRP(10),
                    XRP(10),
                    frequency.count(),
                    startTime + (frequency.count() * (i + 1)));
            }
            env(subscription::claim(bob, subId, XRP(10)), Ter(tecTOO_SOON));
            env.close();

            validateSubscription(
                env,
                subId,
                XRP(10),
                XRP(10),
                frequency.count(),
                startTime + (frequency.count() * 4));
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(40));
            BEAST_EXPECT(env.balance(bob) == preBob - (env.current()->fees().base * 5) + XRP(40));
        }
    }

    void
    testDstTag(FeatureBitset features)
    {
        testcase("dst tag");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        // setup env
        auto const alice = Account("alice");
        auto const bob = Account("bob");

        Env env{*this, features};
        env.fund(XRP(1000), alice, bob);
        env.close();
        env(fset(bob, asfRequireDest));
        env.close();

        {
            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::create(alice, bob, XRP(10), 100s), Ter(tecDST_TAG_NEEDED));
            env.close();

            BEAST_EXPECT(!subscriptionExists(*env.current(), subId));
        }

        {
            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::create(alice, bob, XRP(10), 100s), Dtag(1));
            env.close();

            BEAST_EXPECT(subscriptionExists(*env.current(), subId));

            auto const [key, subSle] = subKeyAndSle(*env.current(), subId);
            BEAST_EXPECT(subSle->isFieldPresent(sfDestinationTag));
            BEAST_EXPECT(subSle->getFieldU32(sfDestinationTag) == 1);
        }
    }

    void
    testMetaAndOwnership(FeatureBitset features)
    {
        testcase("meta and ownership");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");

        Env env{*this, features};
        env.fund(XRP(1000), alice, bob, carol);
        env.close();

        // Create subscription
        {
            auto const aliceSeq = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, aliceSeq);

            env(subscription::create(alice, bob, XRP(10), 100s));
            env.close();

            auto const sub = env.le(keylet::subscription(subId));
            BEAST_EXPECT(sub);

            // Check owner directories
            Dir aliceDir(*env.current(), keylet::ownerDir(alice.id()));
            BEAST_EXPECT(std::distance(aliceDir.begin(), aliceDir.end()) == 1);
            BEAST_EXPECT(std::find(aliceDir.begin(), aliceDir.end(), sub) != aliceDir.end());

            Dir bobDir(*env.current(), keylet::ownerDir(bob.id()));
            BEAST_EXPECT(std::distance(bobDir.begin(), bobDir.end()) == 1);
            BEAST_EXPECT(std::find(bobDir.begin(), bobDir.end(), sub) != bobDir.end());

            // Cancel subscription
            env(subscription::cancel(alice, subId));
            env.close();

            BEAST_EXPECT(!env.le(keylet::subscription(subId)));

            Dir aliceDir2(*env.current(), keylet::ownerDir(alice.id()));
            BEAST_EXPECT(std::distance(aliceDir2.begin(), aliceDir2.end()) == 0);

            Dir bobDir2(*env.current(), keylet::ownerDir(bob.id()));
            BEAST_EXPECT(std::distance(bobDir2.begin(), bobDir2.end()) == 0);
        }

        // Multiple subscriptions
        {
            auto const seq1 = env.seq(alice);
            auto const subId1 = getSubscriptionIndex(alice, bob, seq1);
            env(subscription::create(alice, bob, XRP(10), 100s));
            env.close();

            auto const seq2 = env.seq(alice);
            auto const subId2 = getSubscriptionIndex(alice, carol, seq2);
            env(subscription::create(alice, carol, XRP(20), 200s));
            env.close();

            auto const seq3 = env.seq(bob);
            auto const subId3 = getSubscriptionIndex(bob, carol, seq3);
            env(subscription::create(bob, carol, XRP(30), 300s));
            env.close();

            // Check owner counts
            Dir aliceDir(*env.current(), keylet::ownerDir(alice.id()));
            BEAST_EXPECT(std::distance(aliceDir.begin(), aliceDir.end()) == 2);

            Dir bobDir(*env.current(), keylet::ownerDir(bob.id()));
            BEAST_EXPECT(std::distance(bobDir.begin(), bobDir.end()) == 2);

            Dir carolDir(*env.current(), keylet::ownerDir(carol.id()));
            BEAST_EXPECT(std::distance(carolDir.begin(), carolDir.end()) == 2);

            // Clean up
            env(subscription::cancel(alice, subId1));
            env(subscription::cancel(alice, subId2));
            env(subscription::cancel(bob, subId3));
            env.close();
        }
    }

    void
    testAccountDelete(FeatureBitset features)
    {
        testcase("account delete");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto rmAccount =
            [this](
                Env& env, Account const& toRm, Account const& dst, TER expectedTer = tesSUCCESS) {
                // only allow an account to be deleted if the account's sequence
                // number is at least 256 less than the current ledger sequence
                for (auto minRmSeq = env.seq(toRm) + 257; env.current()->seq() < minRmSeq;
                     env.close())
                {
                }

                env(acctdelete(toRm, dst),
                    Fee(drops(env.current()->fees().increment)),
                    Ter(expectedTer));
                env.close();
                this->BEAST_EXPECT(
                    isTesSuccess(expectedTer) == !env.closed()->exists(keylet::account(toRm.id())));
            };

        // setup env
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");

        Env env{*this, features};
        env.fund(XRP(1000), alice, bob, carol);
        env.close();

        env(subscription::create(alice, bob, XRP(10), 100s));
        env.close();

        rmAccount(env, alice, carol, tecHAS_OBLIGATIONS);
        rmAccount(env, bob, carol, tecHAS_OBLIGATIONS);
        BEAST_EXPECT(env.closed()->exists(keylet::account(alice.id())));
        BEAST_EXPECT(env.closed()->exists(keylet::account(bob.id())));
    }

    void
    testUsingTickets(FeatureBitset features)
    {
        testcase("using tickets");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        // Create / Claim / Cancel (Account)
        {
            // setup env
            auto const alice = Account("alice");
            auto const bob = Account("bob");

            Env env{*this, features};
            env.fund(XRP(1000), alice, bob);
            env.close();

            std::uint32_t aliceTicketSeq{env.seq(alice) + 1};
            env(ticket::create(alice, 10));
            std::uint32_t const aliceSeq{env.seq(alice)};

            std::uint32_t bobTicketSeq{env.seq(bob) + 1};
            env(ticket::create(bob, 10));
            std::uint32_t const bobSeq{env.seq(bob)};

            auto const subId = getSubscriptionIndex(alice, bob, aliceTicketSeq);
            env(subscription::create(alice, bob, XRP(10), 100s), ticket::Use(aliceTicketSeq++));
            env.close();

            env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
            BEAST_EXPECT(env.seq(alice) == aliceSeq);

            env(subscription::claim(bob, subId, XRP(10)), ticket::Use(bobTicketSeq++));
            env.close();

            env.require(tickets(bob, env.seq(bob) - bobTicketSeq));
            BEAST_EXPECT(env.seq(bob) == bobSeq);

            env(subscription::cancel(alice, subId), ticket::Use(aliceTicketSeq++));
            env.close();

            env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
            BEAST_EXPECT(env.seq(alice) == aliceSeq);
        }

        // Create / Claim / Cancel (Destination)
        {
            // setup env
            auto const alice = Account("alice");
            auto const bob = Account("bob");

            Env env{*this, features};
            env.fund(XRP(1000), alice, bob);
            env.close();

            std::uint32_t aliceTicketSeq{env.seq(alice) + 1};
            env(ticket::create(alice, 10));
            std::uint32_t const aliceSeq{env.seq(alice)};

            std::uint32_t bobTicketSeq{env.seq(bob) + 1};
            env(ticket::create(bob, 10));
            std::uint32_t const bobSeq{env.seq(bob)};

            auto const subId = getSubscriptionIndex(alice, bob, aliceTicketSeq);
            env(subscription::create(alice, bob, XRP(10), 100s), ticket::Use(aliceTicketSeq++));
            env.close();

            env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
            BEAST_EXPECT(env.seq(alice) == aliceSeq);

            env(subscription::claim(bob, subId, XRP(10)), ticket::Use(bobTicketSeq++));
            env.close();

            env.require(tickets(bob, env.seq(bob) - bobTicketSeq));
            BEAST_EXPECT(env.seq(bob) == bobSeq);

            env(subscription::cancel(bob, subId), ticket::Use(bobTicketSeq++));
            env.close();

            env.require(tickets(bob, env.seq(bob) - bobTicketSeq));
            BEAST_EXPECT(env.seq(bob) == bobSeq);
        }
    }

    void
    testExpiredSubscription(FeatureBitset features)
    {
        testcase("expired subscription");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        Env env{*this, features};
        env.fund(XRP(1000), alice, bob);
        env.close();

        auto const aliceSeq = env.seq(alice);
        auto const subId = getSubscriptionIndex(alice, bob, aliceSeq);

        auto const expire = env.now() + 200s;
        env(subscription::create(alice, bob, XRP(10), 100s, expire));
        env.close();

        // First payment before expiration
        env(subscription::claim(bob, subId, XRP(10)));
        env.close();

        // Advance time past expiration
        env.close(200s);

        // Claims after expiration fail; the object remains on the ledger
        env(subscription::claim(bob, subId, XRP(10)), Ter(tecEXPIRED));
        env.close();

        BEAST_EXPECT(subscriptionExists(*env.current(), subId));

        // Anyone may cancel an expired subscription; the owner reserve is
        // released and both directory entries are removed
        auto const carol = Account("carol");
        env.fund(XRP(1000), carol);
        env.close();

        auto const preOwnerCount = ownerCount(env, alice);
        env(subscription::cancel(carol, subId));
        env.close();

        BEAST_EXPECT(!subscriptionExists(*env.current(), subId));
        BEAST_EXPECT(ownerCount(env, alice) == preOwnerCount - 1);
        BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 0);
        BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 0);

        // Further claims should fail
        env(subscription::claim(bob, subId, XRP(10)), Ter(tecNO_ENTRY));
        env.close();
    }

    void
    testTimingBoundaries(FeatureBitset features)
    {
        testcase("timing boundaries");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        // StartTime in the future: a claim one ledger before NextClaimTime
        // fails with tecTOO_SOON; a claim in the ledger whose parent close
        // time is exactly NextClaimTime succeeds.
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            auto const frequency = 100s;
            auto const start = env.now() + 100s;
            env(subscription::create(alice, bob, XRP(10), frequency),
                subscription::StartTime(start));
            env.close();

            // Well before the start time
            BEAST_EXPECT(env.now() < start);
            env(subscription::claim(bob, subId, XRP(10)), Ter(tecTOO_SOON));
            env.close();

            // One ledger before the boundary
            for (; env.now() < start - 10s; env.close())
            {
            }
            BEAST_EXPECT(env.now() == start - 10s);
            env(subscription::claim(bob, subId, XRP(10)), Ter(tecTOO_SOON));
            env.close();

            // Exactly at the boundary: parentCloseTime == NextClaimTime
            BEAST_EXPECT(env.now() == start);
            env(subscription::claim(bob, subId, XRP(10)));
            env.close();

            validateSubscription(
                env,
                subId,
                XRP(10),
                XRP(10),
                frequency.count(),
                (start + frequency).time_since_epoch().count());
        }

        // A claim in the ledger whose parent close time is exactly Expiration
        // fails with tecEXPIRED; one ledger earlier it still succeeds.
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            auto const expire = env.now() + 200s;
            env(subscription::create(alice, bob, XRP(10), 100s, expire));
            env.close();

            // One ledger before expiration the claim succeeds
            for (; env.now() < expire - 10s; env.close())
            {
            }
            BEAST_EXPECT(env.now() == expire - 10s);
            env(subscription::claim(bob, subId, XRP(10)));
            env.close();

            // parentCloseTime == Expiration: expiry uses >=, so the claim is
            // rejected exactly at the boundary and the object remains
            BEAST_EXPECT(env.now() == expire);
            env(subscription::claim(bob, subId, XRP(10)), Ter(tecEXPIRED));
            env.close();
            BEAST_EXPECT(subscriptionExists(*env.current(), subId));
        }

        // Create with Expiration == current close time is allowed:
        // SubscriptionSet rejects only an expiration strictly less than
        // parentCloseTime, so the boundary value creates an already-expired
        // subscription.
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            auto const expire = env.now();
            env(subscription::create(alice, bob, XRP(10), 100s, expire));
            env.close();

            BEAST_EXPECT(subscriptionExists(*env.current(), subId));

            // ... and it can never be claimed
            env(subscription::claim(bob, subId, XRP(10)), Ter(tecEXPIRED));
            env.close();
        }

        // StartTime in the past is rejected
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob);
            env.close();

            env(subscription::create(alice, bob, XRP(10), 100s),
                subscription::StartTime(env.now() - 10s),
                Ter(tecNO_PERMISSION));
            env.close();
        }
    }

    void
    testConsequences(FeatureBitset features)
    {
        testcase("consequences");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        Env env{*this, features};
        auto const baseFee = env.current()->fees().base;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.memoize(alice);
        env.memoize(bob);

        uint256 const subId = getSubscriptionIndex(alice, bob, 1);

        // None of the subscription transactors define makeTxConsequences, so
        // all three report the default consequences: fee only, no potential
        // spend.
        {
            auto const jtx =
                env.jt(subscription::create(alice, bob, XRP(1000), 100s), Seq(1), Fee(baseFee));
            auto const pf =
                preflight(env.app(), env.current()->rules(), *jtx.stx, TapNone, env.journal);
            BEAST_EXPECT(isTesSuccess(pf.ter));
            BEAST_EXPECT(!pf.consequences.isBlocker());
            BEAST_EXPECT(pf.consequences.fee() == drops(baseFee));
            BEAST_EXPECT(pf.consequences.potentialSpend() == XRP(0));
        }

        {
            auto const jtx =
                env.jt(subscription::claim(bob, subId, XRP(1000)), Seq(1), Fee(baseFee));
            auto const pf =
                preflight(env.app(), env.current()->rules(), *jtx.stx, TapNone, env.journal);
            BEAST_EXPECT(isTesSuccess(pf.ter));
            BEAST_EXPECT(!pf.consequences.isBlocker());
            BEAST_EXPECT(pf.consequences.fee() == drops(baseFee));
            BEAST_EXPECT(pf.consequences.potentialSpend() == XRP(0));
        }

        {
            auto const jtx = env.jt(subscription::cancel(alice, subId), Seq(1), Fee(baseFee));
            auto const pf =
                preflight(env.app(), env.current()->rules(), *jtx.stx, TapNone, env.journal);
            BEAST_EXPECT(isTesSuccess(pf.ter));
            BEAST_EXPECT(!pf.consequences.isBlocker());
            BEAST_EXPECT(pf.consequences.fee() == drops(baseFee));
            BEAST_EXPECT(pf.consequences.potentialSpend() == XRP(0));
        }
    }

    void
    testMultipleSubscriptionsSamePair(FeatureBitset features)
    {
        testcase("multiple subscriptions same pair");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        Env env{*this, features};
        env.fund(XRP(1000), alice, bob);
        env.close();

        auto const start1 = env.now().time_since_epoch().count();
        auto const sub1 = getSubscriptionIndex(alice, bob, env.seq(alice));
        env(subscription::create(alice, bob, XRP(10), 100s));
        env.close();

        auto const start2 = env.now().time_since_epoch().count();
        auto const sub2 = getSubscriptionIndex(alice, bob, env.seq(alice));
        env(subscription::create(alice, bob, XRP(20), 200s));
        env.close();

        auto const start3 = env.now().time_since_epoch().count();
        auto const sub3 = getSubscriptionIndex(alice, bob, env.seq(alice));
        env(subscription::create(alice, bob, XRP(30), 300s));
        env.close();

        BEAST_EXPECT(sub1 != sub2 && sub2 != sub3 && sub1 != sub3);
        BEAST_EXPECT(subscriptionExists(*env.current(), sub1));
        BEAST_EXPECT(subscriptionExists(*env.current(), sub2));
        BEAST_EXPECT(subscriptionExists(*env.current(), sub3));

        // Only the owner carries the reserve; the destination just holds
        // directory entries
        BEAST_EXPECT(ownerCount(env, alice) == 3);
        BEAST_EXPECT(ownerCount(env, bob) == 0);
        BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 3);
        BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 3);

        // Independent claims: claiming one leaves the others untouched
        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);
        env(subscription::claim(bob, sub2, XRP(20)));
        env.close();

        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(20));
        BEAST_EXPECT(env.balance(bob) == preBob - env.current()->fees().base + XRP(20));
        validateSubscription(env, sub1, XRP(10), XRP(10), 100, start1);
        validateSubscription(env, sub2, XRP(20), XRP(20), 200, start2 + 200);
        validateSubscription(env, sub3, XRP(30), XRP(30), 300, start3);

        // Independent cancels
        env(subscription::cancel(alice, sub1));
        env.close();
        BEAST_EXPECT(!subscriptionExists(*env.current(), sub1));
        BEAST_EXPECT(subscriptionExists(*env.current(), sub2));
        BEAST_EXPECT(subscriptionExists(*env.current(), sub3));
        BEAST_EXPECT(ownerCount(env, alice) == 2);

        env(subscription::cancel(bob, sub3));
        env.close();
        BEAST_EXPECT(subscriptionExists(*env.current(), sub2));
        BEAST_EXPECT(!subscriptionExists(*env.current(), sub3));
        BEAST_EXPECT(ownerCount(env, alice) == 1);

        env(subscription::cancel(alice, sub2));
        env.close();
        BEAST_EXPECT(!subscriptionExists(*env.current(), sub2));
        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 0);
        BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 0);
    }

    void
    testRegularKey(FeatureBitset features)
    {
        testcase("regular key");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const alie = Account("alie");
        auto const bobby = Account("bobby");

        Env env{*this, features};
        env.fund(XRP(1000), alice, bob);
        env.close();

        env(regkey(alice, alie));
        env(regkey(bob, bobby));
        env(fset(alice, asfDisableMaster), Sig(alice));
        env.close();

        auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
        env(subscription::create(alice, bob, XRP(10), 100s), Sig(alie));
        env.close();
        BEAST_EXPECT(subscriptionExists(*env.current(), subId));

        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);
        env(subscription::claim(bob, subId, XRP(10)), Sig(bobby));
        env.close();
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(10));
        BEAST_EXPECT(env.balance(bob) == preBob - env.current()->fees().base + XRP(10));

        env(subscription::cancel(alice, subId), Sig(alie));
        env.close();
        BEAST_EXPECT(!subscriptionExists(*env.current(), subId));
    }

    void
    testMultisign(FeatureBitset features)
    {
        testcase("multisign");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const daria = Account("daria");

        Env env{*this, features};
        auto const baseFee = env.current()->fees().base;
        env.fund(XRP(1000), alice, bob, carol, daria);
        env.close();

        env(signers(alice, 1, {{carol, 1}}));
        env(signers(bob, 1, {{daria, 1}}));
        env.close();

        auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
        env(subscription::create(alice, bob, XRP(10), 100s), Msig(carol), Fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(subscriptionExists(*env.current(), subId));

        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);
        env(subscription::claim(bob, subId, XRP(10)), Msig(daria), Fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(10));
        BEAST_EXPECT(env.balance(bob) == preBob - (baseFee * 2) + XRP(10));

        env(subscription::cancel(alice, subId), Msig(carol), Fee(2 * baseFee));
        env.close();
        BEAST_EXPECT(!subscriptionExists(*env.current(), subId));
    }

    void
    testDelegation(FeatureBitset features)
    {
        testcase("delegation");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const dave = Account("dave");

        Env env{*this, features};
        env.fund(XRP(1000), alice, bob, carol, dave);
        env.close();

        // All three subscription transactions are delegable
        env(delegate::set(alice, dave, {"SubscriptionSet", "SubscriptionCancel"}));
        env(delegate::set(bob, dave, {"SubscriptionClaim"}));
        env.close();

        {
            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::create(alice, bob, XRP(10), 100s), delegate::As(dave));
            env.close();
            BEAST_EXPECT(subscriptionExists(*env.current(), subId));

            env(subscription::claim(bob, subId, XRP(1)), delegate::As(dave));
            env.close();

            env(subscription::cancel(alice, subId), delegate::As(dave));
            env.close();
            BEAST_EXPECT(!subscriptionExists(*env.current(), subId));
        }

        // A delegate without the subscription permissions is rejected
        env(delegate::set(alice, carol, {"Payment"}));
        env(delegate::set(bob, carol, {"Payment"}));
        env.close();

        // A missing tx-type permission is reported with the retry code
        // terNO_DELEGATE_PERMISSION (no fee, no sequence consumed), matching
        // every other delegable transaction
        {
            env(subscription::create(alice, bob, XRP(10), 100s),
                delegate::As(carol),
                Ter(terNO_DELEGATE_PERMISSION));
            env.close();

            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::create(alice, bob, XRP(10), 100s));
            env.close();

            env(subscription::claim(bob, subId, XRP(1)),
                delegate::As(carol),
                Ter(terNO_DELEGATE_PERMISSION));
            env.close();

            env(subscription::cancel(alice, subId),
                delegate::As(carol),
                Ter(terNO_DELEGATE_PERMISSION));
            env.close();

            BEAST_EXPECT(subscriptionExists(*env.current(), subId));
            env(subscription::cancel(alice, subId));
            env.close();
        }
    }

    void
    testAccountObjectsRPC(FeatureBitset features)
    {
        testcase("account_objects RPC");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        Env env{*this, features};
        env.fund(XRP(1000), alice, bob);
        env.close();

        auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
        env(subscription::create(alice, bob, XRP(10), 100s));
        env.close();

        // The "subscription" type filter returns the object for the owner
        // and for the destination
        auto checkAccountObjects = [&](Account const& acct) {
            json::Value params;
            params[jss::account] = acct.human();
            params[jss::type] = jss::subscription;
            auto const resp = env.rpc("json", "account_objects", to_string(params));
            auto const& objects = resp[jss::result][jss::account_objects];
            if (!BEAST_EXPECT(objects.isArray() && objects.size() == 1))
                return;
            BEAST_EXPECT(objects[0u][sfLedgerEntryType.jsonName] == jss::Subscription);
            BEAST_EXPECT(objects[0u][jss::index] == to_string(subId));
            BEAST_EXPECT(objects[0u][sfAccount.jsonName] == alice.human());
            BEAST_EXPECT(objects[0u][sfDestination.jsonName] == bob.human());
        };

        checkAccountObjects(alice);
        checkAccountObjects(bob);
    }

    void
    testLedgerEntryRPC(FeatureBitset features)
    {
        testcase("ledger_entry RPC");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        Env env{*this, features};
        env.fund(XRP(1000), alice, bob);
        env.close();

        auto const createSeq = env.seq(alice);
        auto const subId = getSubscriptionIndex(alice, bob, createSeq);
        env(subscription::create(alice, bob, XRP(10), 100s));
        env.close();

        // By hex index
        {
            json::Value params;
            params[jss::subscription] = to_string(subId);
            auto const jrr = env.rpc("json", "ledger_entry", to_string(params))[jss::result];
            BEAST_EXPECT(jrr[jss::index] == to_string(subId));
            BEAST_EXPECT(jrr[jss::node][sfLedgerEntryType.jsonName] == jss::Subscription);
            BEAST_EXPECT(jrr[jss::node][sfAccount.jsonName] == alice.human());
            BEAST_EXPECT(jrr[jss::node][sfDestination.jsonName] == bob.human());
            BEAST_EXPECT(jrr[jss::node][sfSequence.jsonName].asUInt() == createSeq);
        }

        // By {account, destination, seq} object
        {
            json::Value params;
            params[jss::subscription][jss::account] = alice.human();
            params[jss::subscription][jss::destination] = bob.human();
            params[jss::subscription][jss::seq] = createSeq;
            auto const jrr = env.rpc("json", "ledger_entry", to_string(params))[jss::result];
            BEAST_EXPECT(jrr[jss::index] == to_string(subId));
            BEAST_EXPECT(jrr[jss::node][sfLedgerEntryType.jsonName] == jss::Subscription);
        }

        auto checkError = [&](json::Value const& params, std::string const& err) {
            auto const jrr = env.rpc("json", "ledger_entry", to_string(params))[jss::result];
            BEAST_EXPECTS(jrr[jss::error] == err, jrr.toStyledString());
        };

        // Missing account: a missing field always reports malformedRequest;
        // the malformedAccount code is used for present-but-invalid values
        {
            json::Value params;
            params[jss::subscription][jss::destination] = bob.human();
            params[jss::subscription][jss::seq] = createSeq;
            checkError(params, "malformedRequest");
        }

        // Bad account
        {
            json::Value params;
            params[jss::subscription][jss::account] = "not_an_account";
            params[jss::subscription][jss::destination] = bob.human();
            params[jss::subscription][jss::seq] = createSeq;
            checkError(params, "malformedAccount");
        }

        // Bad destination
        {
            json::Value params;
            params[jss::subscription][jss::account] = alice.human();
            params[jss::subscription][jss::destination] = "not_an_account";
            params[jss::subscription][jss::seq] = createSeq;
            checkError(params, "malformedDestination");
        }

        // Missing seq
        {
            json::Value params;
            params[jss::subscription][jss::account] = alice.human();
            params[jss::subscription][jss::destination] = bob.human();
            checkError(params, "malformedRequest");
        }
    }

    void
    testDepositAuthDestination(FeatureBitset features)
    {
        testcase("deposit auth destination");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        Env env{*this, features};
        env.fund(XRP(1000), alice, bob);
        env.close();

        auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
        env(subscription::create(alice, bob, XRP(10), 100s));
        env.close();

        // The destination signs the claim itself, so its own DepositAuth
        // flag does not block the delivery
        env(fset(bob, asfDepositAuth));
        env.close();

        auto preAlice = env.balance(alice);
        auto preBob = env.balance(bob);
        env(subscription::claim(bob, subId, XRP(10)));
        env.close();
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(10));
        BEAST_EXPECT(env.balance(bob) == preBob - env.current()->fees().base + XRP(10));

        // An owner with DepositAuth set can still be claimed from
        env(fset(alice, asfDepositAuth));
        env.close(100s);

        preAlice = env.balance(alice);
        preBob = env.balance(bob);
        env(subscription::claim(bob, subId, XRP(10)));
        env.close();
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(10));
        BEAST_EXPECT(env.balance(bob) == preBob - env.current()->fees().base + XRP(10));
    }

    void
    testExploitThirdPartyCancel(FeatureBitset features)
    {
        testcase("exploit: third party cancel");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");

        Env env{*this, features};
        env.fund(XRP(1000), alice, bob, carol);
        env.close();

        // A third party cannot cancel an unexpired subscription; the owner
        // can
        {
            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::create(alice, bob, XRP(10), 100s));
            env.close();

            env(subscription::cancel(carol, subId), Ter(tecNO_PERMISSION));
            env.close();
            BEAST_EXPECT(subscriptionExists(*env.current(), subId));

            env(subscription::cancel(alice, subId));
            env.close();
            BEAST_EXPECT(!subscriptionExists(*env.current(), subId));
        }

        // ... and so can the destination
        {
            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::create(alice, bob, XRP(10), 100s));
            env.close();

            env(subscription::cancel(carol, subId), Ter(tecNO_PERMISSION));
            env.close();
            BEAST_EXPECT(subscriptionExists(*env.current(), subId));

            env(subscription::cancel(bob, subId));
            env.close();
            BEAST_EXPECT(!subscriptionExists(*env.current(), subId));
        }
    }

    void
    testExploitExpiredDrain(FeatureBitset features)
    {
        testcase("exploit: expired drain");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");

        Env env{*this, features};
        env.fund(XRP(1000), alice, bob, carol);
        env.close();

        auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
        auto const expire = env.now() + 400s;
        env(subscription::create(alice, bob, XRP(10), 100s, expire));
        env.close();

        // Let three full periods accrue unclaimed, then let the subscription
        // expire
        for (; env.now() < expire; env.close())
        {
        }
        BEAST_EXPECT(env.now() == expire);

        // The accrued arrears cannot be drained once expired
        auto const preAlice = env.balance(alice);
        auto const preBob = env.balance(bob);
        env(subscription::claim(bob, subId, XRP(10)), Ter(tecEXPIRED));
        env.close();
        BEAST_EXPECT(subscriptionExists(*env.current(), subId));
        BEAST_EXPECT(env.balance(alice) == preAlice);
        BEAST_EXPECT(env.balance(bob) == preBob - env.current()->fees().base);

        // Any third party may reap the expired object; the owner reserve is
        // released and both directory entries are removed
        auto const preAliceOwners = ownerCount(env, alice);
        env(subscription::cancel(carol, subId));
        env.close();

        BEAST_EXPECT(!subscriptionExists(*env.current(), subId));
        BEAST_EXPECT(ownerCount(env, alice) == preAliceOwners - 1);
        BEAST_EXPECT(ownerCount(env, bob) == 0);
        BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 0);
        BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 0);

        // Nothing further to claim
        env(subscription::claim(bob, subId, XRP(10)), Ter(tecNO_ENTRY));
        env.close();
    }

    void
    testExploitAssetSwitchUpdate(FeatureBitset features)
    {
        testcase("exploit: asset switch update");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const gw2 = Account{"gateway2"};
        auto const USD = gw["USD"];
        auto const EUR = gw["EUR"];
        auto const USD2 = gw2["USD"];

        // IOU and XRP subscriptions
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, gw);
            env.close();
            env.trust(USD(10000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(1000)));
            env.close();

            auto const subUSD = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::create(alice, bob, USD(10), 1000s));
            env.close();

            // Different currency
            env(subscription::update(alice, subUSD, EUR(10)), Ter(tecWRONG_ASSET));
            env.close();
            // Different issuer, same currency code
            env(subscription::update(alice, subUSD, USD2(10)), Ter(tecWRONG_ASSET));
            env.close();
            // IOU -> XRP
            env(subscription::update(alice, subUSD, XRP(10)), Ter(tecWRONG_ASSET));
            env.close();

            auto const subXRP = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::create(alice, bob, XRP(10), 1000s));
            env.close();

            // XRP -> IOU
            env(subscription::update(alice, subXRP, USD(10)), Ter(tecWRONG_ASSET));
            env.close();

            // Same-asset update with a new value succeeds; the stored
            // Balance is NOT clamped to the new Amount, but any claim is
            // still capped at the new Amount
            env(subscription::claim(bob, subUSD, USD(4)));
            env.close();

            env(subscription::update(alice, subUSD, USD(5)));
            env.close();

            auto const [key, subSle] = subKeyAndSle(*env.current(), subUSD);
            if (BEAST_EXPECT(subSle))
            {
                BEAST_EXPECT(subSle->getFieldAmount(sfAmount) == USD(5));
                // Balance still holds the pre-update remainder of the period
                BEAST_EXPECT(subSle->getFieldAmount(sfBalance) == USD(6));
            }

            env(subscription::claim(bob, subUSD, USD(6)), Ter(tecLIMIT_EXCEEDED));
            env.close();

            env(subscription::claim(bob, subUSD, USD(5)));
            env.close();
            auto const [key2, subSle2] = subKeyAndSle(*env.current(), subUSD);
            if (BEAST_EXPECT(subSle2))
                BEAST_EXPECT(subSle2->getFieldAmount(sfBalance) == USD(1));
        }

        // MPT subscription cannot be switched to an IOU
        {
            Env env{*this, features};
            auto const gwM = Account("gw");
            env.fund(XRP(5000), bob);
            env.close();

            MPTTester mptGw(env, gwM, {.holders = {alice}});
            mptGw.create({.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const MPT = mptGw["MPT"];
            env(pay(gwM, alice, MPT(10000)));
            env.close();

            auto const subMPT = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::create(alice, bob, MPT(100), 1000s));
            env.close();

            env(subscription::update(alice, subMPT, USD(10)), Ter(tecWRONG_ASSET));
            env.close();
        }
    }

    void
    testExploitClaimOverdraw(FeatureBitset features)
    {
        testcase("exploit: claim overdraw");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        Env env{*this, features};
        env.fund(XRP(1000), alice, bob);
        env.close();

        // Use a frequency far larger than the test's ledger time so the next
        // period cannot start during the test
        auto const frequency = 10000s;

        // Fully drain the period, then try to claim the same period again:
        // the full claim advanced NextClaimTime a full period and no time
        // has passed, so any further claim is too soon
        {
            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::create(alice, bob, XRP(10), frequency));
            env.close();

            env(subscription::claim(bob, subId, XRP(10)));
            env.close();

            env(subscription::claim(bob, subId, XRP(10)), Ter(tecTOO_SOON));
            env.close();
            env(subscription::claim(bob, subId, XRP(1)), Ter(tecTOO_SOON));
            env.close();
        }

        // Partial claim, then a claim exceeding the remainder of the period
        {
            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::create(alice, bob, XRP(10), frequency));
            env.close();

            env(subscription::claim(bob, subId, XRP(4)));
            env.close();

            env(subscription::claim(bob, subId, XRP(7)), Ter(tecINSUFFICIENT_FUNDS));
            env.close();

            // Claim above the per-period Amount
            env(subscription::claim(bob, subId, XRP(11)), Ter(tecLIMIT_EXCEEDED));
            env.close();
        }
    }

    void
    testExploitBoundaryStraddle(FeatureBitset features)
    {
        testcase("exploit: boundary straddle");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        Env env{*this, features};
        env.fund(XRP(1000), alice, bob);
        env.close();

        auto const start = env.now();
        auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
        env(subscription::create(alice, bob, XRP(10), 100s));
        env.close();

        // Claim the full amount in the very last ledger of the first period
        for (; env.now() < start + 90s; env.close())
        {
        }
        BEAST_EXPECT(env.now() == start + 90s);
        auto const preAlice = env.balance(alice);
        env(subscription::claim(bob, subId, XRP(10)));
        env.close();

        // Documented tumbling-window property: the full claim advanced
        // NextClaimTime to the period boundary, which the very next ledger
        // reaches, so two full-Amount claims succeed back-to-back
        BEAST_EXPECT(env.now() == start + 100s);
        env(subscription::claim(bob, subId, XRP(10)));
        env.close();

        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(20));
        validateSubscription(
            env, subId, XRP(10), XRP(10), 100, (start + 200s).time_since_epoch().count());
    }

    void
    testExploitArrearsExactness(FeatureBitset features)
    {
        testcase("exploit: arrears exactness");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        Env env{*this, features};
        env.fund(XRP(1000), alice, bob);
        env.close();

        auto const start = env.now();
        auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
        env(subscription::create(alice, bob, XRP(10), 100s));
        env.close();

        // Advance into the third period without claiming: two periods fully
        // missed plus the in-progress period make exactly three claimable
        // full claims
        for (; env.now() < start + 250s; env.close())
        {
        }
        BEAST_EXPECT(env.now() == start + 250s);

        auto const preAlice = env.balance(alice);
        for (int i = 0; i < 3; ++i)
        {
            env(subscription::claim(bob, subId, XRP(10)));
            env.close();
        }
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(30));

        // The fourth claim needs the next period boundary
        env(subscription::claim(bob, subId, XRP(10)), Ter(tecTOO_SOON));
        env.close();
        validateSubscription(
            env, subId, XRP(10), XRP(10), 100, (start + 300s).time_since_epoch().count());
    }

    void
    testExploitOwnerClaim(FeatureBitset features)
    {
        testcase("exploit: owner claim");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");

        Env env{*this, features};
        env.fund(XRP(1000), alice, bob, carol);
        env.close();

        auto const start = env.now().time_since_epoch().count();
        auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
        env(subscription::create(alice, bob, XRP(10), 100s));
        env.close();

        // The owner cannot claim its own subscription
        env(subscription::claim(alice, subId, XRP(1)), Ter(tecNO_PERMISSION));
        env.close();

        // Neither can an unrelated account
        env(subscription::claim(carol, subId, XRP(1)), Ter(tecNO_PERMISSION));
        env.close();

        // The subscription is untouched
        validateSubscription(env, subId, XRP(10), XRP(10), 100, start);
    }

    void
    testUpdateRequireAuth(FeatureBitset features)
    {
        testcase("update requireauth");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        auto const aliceUSD = alice["USD"];
        auto const bobUSD = bob["USD"];

        Env env{*this, features};
        env.fund(XRP(1000), alice, bob, gw);
        env(fset(gw, asfRequireAuth));
        env.close();

        env(trust(gw, aliceUSD(10000)), Txflags(tfSetfAuth));
        env(trust(alice, USD(10000)));
        env(trust(gw, bobUSD(10000)), Txflags(tfSetfAuth));
        env(trust(bob, USD(10000)));
        env.close();
        env(pay(gw, alice, USD(1000)));
        env.close();

        auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
        env(subscription::create(alice, bob, USD(100), 100s));
        env.close();

        // Regression: the update path takes the destination from the
        // subscription object, so the RequireAuth checks pass for an
        // authorized pair
        env(subscription::update(alice, subId, USD(200)));
        env.close();

        auto const [key, subSle] = subKeyAndSle(*env.current(), subId);
        if (BEAST_EXPECT(subSle))
            BEAST_EXPECT(subSle->getFieldAmount(sfAmount) == USD(200));

        env(subscription::claim(bob, subId, USD(100)));
        env.close();
        BEAST_EXPECT(env.balance(bob, USD) == USD(100));
    }

    void
    testSequenceField(FeatureBitset features)
    {
        testcase("sequence field");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        Env env{*this, features};
        env.fund(XRP(1000), alice, bob);
        env.close();

        auto lookupBySeq = [&](std::uint32_t seq) {
            json::Value params;
            params[jss::subscription][jss::account] = alice.human();
            params[jss::subscription][jss::destination] = bob.human();
            params[jss::subscription][jss::seq] = seq;
            return env.rpc("json", "ledger_entry", to_string(params))[jss::result];
        };

        // Sequence-created subscription records the consumed sequence
        {
            auto const createSeq = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, createSeq);
            env(subscription::create(alice, bob, XRP(10), 100s));
            env.close();

            auto const [key, subSle] = subKeyAndSle(*env.current(), subId);
            if (BEAST_EXPECT(subSle))
                BEAST_EXPECT(subSle->getFieldU32(sfSequence) == createSeq);

            auto const jrr = lookupBySeq(createSeq);
            BEAST_EXPECT(jrr[jss::index] == to_string(subId));
            BEAST_EXPECT(jrr[jss::node][sfSequence.jsonName].asUInt() == createSeq);
        }

        // Ticket-created subscription records the consumed ticket sequence
        {
            std::uint32_t const ticketSeq{env.seq(alice) + 1};
            env(ticket::create(alice, 1));
            env.close();

            auto const subId = getSubscriptionIndex(alice, bob, ticketSeq);
            env(subscription::create(alice, bob, XRP(10), 100s), ticket::Use(ticketSeq));
            env.close();

            auto const [key, subSle] = subKeyAndSle(*env.current(), subId);
            if (BEAST_EXPECT(subSle))
                BEAST_EXPECT(subSle->getFieldU32(sfSequence) == ticketSeq);

            auto const jrr = lookupBySeq(ticketSeq);
            BEAST_EXPECT(jrr[jss::index] == to_string(subId));
            BEAST_EXPECT(jrr[jss::node][sfSequence.jsonName].asUInt() == ticketSeq);
        }
    }

    void
    testReserveEdge(FeatureBitset features)
    {
        testcase("reserve edge");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        // Create at the owner-reserve boundary
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
            auto const reserve = env.current()->fees().accountReserve(1, 1);

            env.fund(XRP(1000), bob);
            // One drop below: after the fee alice cannot cover the reserve
            // for the new owner entry
            env.fund(reserve + baseFee - drops(1), alice);
            env.close();

            env(subscription::create(alice, bob, XRP(1), 100s), Ter(tecINSUFFICIENT_RESERVE));
            env.close();

            // Top alice back up to exactly reserve + fee: creation succeeds
            // with a post-fee balance exactly at the reserve
            env(pay(env.master, alice, drops(baseFee.drops() + 1)));
            env.close();

            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::create(alice, bob, XRP(1), 100s));
            env.close();
            BEAST_EXPECT(subscriptionExists(*env.current(), subId));
            BEAST_EXPECT(env.balance(alice) == drops(reserve.drops()));
        }

        // IOU claim where the destination cannot cover the reserve for the
        // auto-created trust line
        {
            Env env{*this, features};
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            auto const carol = Account("carol");
            auto const reserve = env.current()->fees().accountReserve(0, 1);
            auto const incReserve = env.current()->fees().increment;

            env.fund(XRP(1000), alice, gw);
            env.fund(reserve + incReserve - drops(1), carol);
            env.close();
            env.trust(USD(10000), alice);
            env.close();
            env(pay(gw, alice, USD(1000)));
            env.close();

            auto const subId = getSubscriptionIndex(alice, carol, env.seq(alice));
            env(subscription::create(alice, carol, USD(10), 100s));
            env.close();

            env(subscription::claim(carol, subId, USD(10)), Ter(tecNO_LINE_INSUF_RESERVE));
            env.close();
        }

        // MPT claim where the destination cannot cover the reserve for the
        // new MPToken
        {
            Env env{*this, features};
            auto const gw = Account("gw");
            auto const reserve = env.current()->fees().accountReserve(0, 1);
            auto const incReserve = env.current()->fees().increment;

            env.fund(reserve + incReserve - drops(1), bob);
            env.close();

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create({.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10000)));
            env.close();

            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::create(alice, bob, MPT(10), 100s));
            env.close();

            env(subscription::claim(bob, subId, MPT(10)), Ter(tecINSUFFICIENT_RESERVE));
            env.close();
        }
    }

    void
    testUnmeteredMode(FeatureBitset features)
    {
        testcase("unmetered mode");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        Env env{*this, features};
        env.fund(XRP(1000), alice, bob);
        env.close();

        // Frequency == 0 is valid and denotes an unmetered subscription:
        // Balance == Amount, NextClaimTime == create/close time, no period.
        {
            auto const startTime = env.now().time_since_epoch().count();
            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::create(alice, bob, XRP(10), 0s));
            env.close();

            validateSubscription(env, subId, XRP(10), XRP(10), 0, startTime);
        }

        // A post-dated StartTime still gates the first claim with tecTOO_SOON.
        {
            auto const start = env.now() + 200s;
            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::create(alice, bob, XRP(10), 0s), subscription::StartTime(start));
            env.close();

            validateSubscription(env, subId, XRP(10), XRP(10), 0, start.time_since_epoch().count());

            env(subscription::claim(bob, subId, XRP(10)), Ter(tecTOO_SOON));
            env.close();

            for (; env.now() < start; env.close())
            {
            }
            env(subscription::claim(bob, subId, XRP(10)));
            env.close();

            // Balance and NextClaimTime are untouched by the claim.
            validateSubscription(env, subId, XRP(10), XRP(10), 0, start.time_since_epoch().count());
        }

        // Expiration still gates an unmetered subscription with tecEXPIRED.
        {
            auto const expire = env.now() + 100s;
            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::create(alice, bob, XRP(10), 0s, expire));
            env.close();

            env.close(100s);
            env(subscription::claim(bob, subId, XRP(10)), Ter(tecEXPIRED));
            env.close();
            BEAST_EXPECT(subscriptionExists(*env.current(), subId));
        }
    }

    void
    testUnmeteredClaims(FeatureBitset features)
    {
        testcase("unmetered claims");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        Env env{*this, features};
        env.fund(XRP(1000), alice, bob);
        env.close();

        auto const startTime = env.now().time_since_epoch().count();
        auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
        env(subscription::create(alice, bob, XRP(10), 0s));
        env.close();

        // A claim exceeding Amount is rejected.
        env(subscription::claim(bob, subId, XRP(11)), Ter(tecLIMIT_EXCEEDED));
        env.close();

        // Unlimited claims, each capped at Amount. Partial claims do NOT
        // reduce a running balance: the next claim is still capped at the full
        // Amount, and Balance/NextClaimTime never change.
        auto const baseFee = env.current()->fees().base;

        for (auto const& amt : {XRP(3), XRP(10), XRP(1), XRP(10)})
        {
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);
            env(subscription::claim(bob, subId, amt));
            env.close();
            BEAST_EXPECT(env.balance(alice) == preAlice - amt);
            BEAST_EXPECT(env.balance(bob) == preBob - baseFee + amt);
            // Balance stays == Amount across every partial and full claim.
            validateSubscription(env, subId, XRP(10), XRP(10), 0, startTime);
        }
    }

    void
    testSingleUse(FeatureBitset features)
    {
        testcase("single use");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        // Metered single-use: the first (full) claim deletes the object with
        // full cleanup.
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::create(alice, bob, XRP(10), 100s),
                Txflags(tfSingleUse | tfFullyCanonicalSig));
            env.close();

            // lsfSingleUse is recorded on the object.
            auto const [key, subSle] = subKeyAndSle(*env.current(), subId);
            if (!BEAST_EXPECT(subSle))
                return;
            BEAST_EXPECT(subSle->getFieldU32(sfFlags) & lsfSingleUse);

            auto const preAliceOwners = ownerCount(env, alice);
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            env(subscription::claim(bob, subId, XRP(10)));
            env.close();

            // Gone from the ledger, both directories emptied, reserve released.
            BEAST_EXPECT(!subscriptionExists(*env.current(), subId));
            BEAST_EXPECT(ownerCount(env, alice) == preAliceOwners - 1);
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 0);
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 0);
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(10));
            BEAST_EXPECT(env.balance(bob) == preBob - env.current()->fees().base + XRP(10));

            // Second claim fails: the object is gone.
            env(subscription::claim(bob, subId, XRP(10)), Ter(tecNO_ENTRY));
            env.close();
        }

        // Unmetered one-shot: single-use composed with Frequency == 0. The
        // first claim (here partial) deletes the object.
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::create(alice, bob, XRP(10), 0s),
                Txflags(tfSingleUse | tfFullyCanonicalSig));
            env.close();

            auto const preAliceOwners = ownerCount(env, alice);
            env(subscription::claim(bob, subId, XRP(4)));
            env.close();

            BEAST_EXPECT(!subscriptionExists(*env.current(), subId));
            BEAST_EXPECT(ownerCount(env, alice) == preAliceOwners - 1);
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 0);

            env(subscription::claim(bob, subId, XRP(4)), Ter(tecNO_ENTRY));
            env.close();
        }
    }

    void
    testSingleUseMetered(FeatureBitset features)
    {
        testcase("single use metered");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        Env env{*this, features};
        env.fund(XRP(1000), alice, bob);
        env.close();

        // A single-use metered subscription is deleted on the first claim even
        // when that claim is partial and the period is not exhausted.
        auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
        env(subscription::create(alice, bob, XRP(10), 100s),
            Txflags(tfSingleUse | tfFullyCanonicalSig));
        env.close();

        auto const preAliceOwners = ownerCount(env, alice);
        auto const preAlice = env.balance(alice);
        env(subscription::claim(bob, subId, XRP(5)));
        env.close();

        BEAST_EXPECT(!subscriptionExists(*env.current(), subId));
        BEAST_EXPECT(ownerCount(env, alice) == preAliceOwners - 1);
        BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 0);
        BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 0);
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(5));

        env(subscription::claim(bob, subId, XRP(5)), Ter(tecNO_ENTRY));
        env.close();
    }

    void
    testSingleUseImmutable(FeatureBitset features)
    {
        testcase("single use immutable");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        Env env{*this, features};
        env.fund(XRP(1000), alice, bob);
        env.close();

        // A subscription created without lsfSingleUse cannot gain it via update.
        auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
        env(subscription::create(alice, bob, XRP(10), 100s));
        env.close();

        json::Value txn = subscription::update(alice, subId, XRP(10));
        txn[jss::Flags] = tfSingleUse | tfFullyCanonicalSig;
        env(txn, Ter(temINVALID_FLAG));
        env.close();

        auto const [key, subSle] = subKeyAndSle(*env.current(), subId);
        if (BEAST_EXPECT(subSle))
            BEAST_EXPECT(!(subSle->getFieldU32(sfFlags) & lsfSingleUse));
    }

    void
    testUpdateFrequency(FeatureBitset features)
    {
        testcase("update frequency");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        Env env{*this, features};
        env.fund(XRP(1000), alice, bob);
        env.close();

        auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
        env(subscription::create(alice, bob, XRP(10), 100s));
        env.close();

        // Claim the full period so NextClaimTime is advanced.
        env(subscription::claim(bob, subId, XRP(10)));
        env.close();
        env(subscription::claim(bob, subId, XRP(10)), Ter(tecTOO_SOON));
        env.close();

        // Updating Frequency changes it and resets a clean period:
        // NextClaimTime = current close time, Balance = Amount.
        env(subscription::update(alice, subId, XRP(10), std::nullopt, 200s));
        env.close();

        auto const resetTime = env.now().time_since_epoch().count();
        {
            auto const [key, subSle] = subKeyAndSle(*env.current(), subId);
            if (!BEAST_EXPECT(subSle))
                return;
            BEAST_EXPECT(subSle->getFieldU32(sfFrequency) == 200);
            BEAST_EXPECT(subSle->getFieldAmount(sfBalance) == XRP(10));
            // NextClaimTime was reset to the update's close time.
            BEAST_EXPECT(subSle->getFieldU32(sfNextClaimTime) < resetTime);
        }

        // A fresh full claim is immediately available after the reset.
        env(subscription::claim(bob, subId, XRP(10)));
        env.close();
        // ... and the new (longer) period now gates the next one.
        env(subscription::claim(bob, subId, XRP(10)), Ter(tecTOO_SOON));
        env.close();
    }

    void
    testUpdateFrequencyTransitions(FeatureBitset features)
    {
        testcase("update frequency transitions");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        // metered -> unmetered (N -> 0)
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::create(alice, bob, XRP(10), 100s));
            env.close();

            env(subscription::claim(bob, subId, XRP(10)));
            env.close();
            // Metered: a second immediate claim is too soon.
            env(subscription::claim(bob, subId, XRP(10)), Ter(tecTOO_SOON));
            env.close();

            env(subscription::update(alice, subId, XRP(10), std::nullopt, 0s));
            env.close();
            auto const resetTime = env.now().time_since_epoch().count();

            // Now unmetered: repeated claims succeed, each capped at Amount,
            // and Balance/NextClaimTime never change.
            for (int i = 0; i < 3; ++i)
            {
                env(subscription::claim(bob, subId, XRP(10)));
                env.close();
                auto const [key, subSle] = subKeyAndSle(*env.current(), subId);
                if (!BEAST_EXPECT(subSle))
                    return;
                BEAST_EXPECT(subSle->getFieldU32(sfFrequency) == 0);
                BEAST_EXPECT(subSle->getFieldAmount(sfBalance) == XRP(10));
                BEAST_EXPECT(subSle->getFieldU32(sfNextClaimTime) < resetTime);
            }
        }

        // unmetered -> metered (0 -> N)
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::create(alice, bob, XRP(10), 0s));
            env.close();

            // Unmetered: two back-to-back claims both succeed.
            env(subscription::claim(bob, subId, XRP(10)));
            env.close();
            env(subscription::claim(bob, subId, XRP(10)));
            env.close();

            env(subscription::update(alice, subId, XRP(10), std::nullopt, 100s));
            env.close();

            {
                auto const [key, subSle] = subKeyAndSle(*env.current(), subId);
                if (!BEAST_EXPECT(subSle))
                    return;
                BEAST_EXPECT(subSle->getFieldU32(sfFrequency) == 100);
                BEAST_EXPECT(subSle->getFieldAmount(sfBalance) == XRP(10));
            }

            // Now metered: one full claim, then the period gates the next.
            env(subscription::claim(bob, subId, XRP(10)));
            env.close();
            env(subscription::claim(bob, subId, XRP(10)), Ter(tecTOO_SOON));
            env.close();
        }

        // metered -> metered (N -> M)
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::create(alice, bob, XRP(10), 100s));
            env.close();

            env(subscription::claim(bob, subId, XRP(10)));
            env.close();
            env(subscription::claim(bob, subId, XRP(10)), Ter(tecTOO_SOON));
            env.close();

            env(subscription::update(alice, subId, XRP(10), std::nullopt, 300s));
            env.close();

            {
                auto const [key, subSle] = subKeyAndSle(*env.current(), subId);
                if (!BEAST_EXPECT(subSle))
                    return;
                BEAST_EXPECT(subSle->getFieldU32(sfFrequency) == 300);
                BEAST_EXPECT(subSle->getFieldAmount(sfBalance) == XRP(10));
            }

            // Fresh full claim immediately available after the reset.
            env(subscription::claim(bob, subId, XRP(10)));
            env.close();
            env(subscription::claim(bob, subId, XRP(10)), Ter(tecTOO_SOON));
            env.close();
        }
    }

    void
    testUpdateRemoveExpiration(FeatureBitset features)
    {
        testcase("update remove expiration");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        Env env{*this, features};
        env.fund(XRP(1000), alice, bob);
        env.close();

        auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
        auto const expire = env.now() + 150s;
        env(subscription::create(alice, bob, XRP(10), 100s, expire));
        env.close();

        {
            auto const [key, subSle] = subKeyAndSle(*env.current(), subId);
            if (BEAST_EXPECT(subSle))
                BEAST_EXPECT(
                    subSle->getFieldU32(sfExpiration) == expire.time_since_epoch().count());
        }

        // Update with Expiration > now changes it.
        auto const expire2 = env.now() + 250s;
        env(subscription::update(alice, subId, XRP(10), expire2));
        env.close();
        {
            auto const [key, subSle] = subKeyAndSle(*env.current(), subId);
            if (BEAST_EXPECT(subSle))
                BEAST_EXPECT(
                    subSle->getFieldU32(sfExpiration) == expire2.time_since_epoch().count());
        }

        // Update with a past (nonzero) Expiration is rejected.
        env(subscription::update(alice, subId, XRP(10), env.now() - 10s), Ter(tecEXPIRED));
        env.close();

        // Update with Expiration == 0 removes the field entirely.
        env(subscription::update(alice, subId, XRP(10), NetClock::time_point{}));
        env.close();
        {
            auto const [key, subSle] = subKeyAndSle(*env.current(), subId);
            if (BEAST_EXPECT(subSle))
                BEAST_EXPECT(!subSle->isFieldPresent(sfExpiration));
        }

        // With expiration removed, a claim succeeds past the original expiry.
        env.close(300s);
        auto const preAlice = env.balance(alice);
        env(subscription::claim(bob, subId, XRP(10)));
        env.close();
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(10));
    }

    void
    testDelegatedPull(FeatureBitset features)
    {
        testcase("delegated pull");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        // The puller/receiver split is delivered through XLS-75 delegation,
        // NOT a dedicated field on the subscription object. The merchant
        // (Destination) delegates SubscriptionClaim to a processor account;
        // the processor claims on the merchant's behalf and funds move
        // payer -> merchant. The processor is never named on the object.
        auto const payer = Account("payer");
        auto const merchant = Account("merchant");
        auto const processor = Account("processor");
        auto const stranger = Account("stranger");

        Env env{*this, features};
        env.fund(XRP(1000), payer, merchant, processor, stranger);
        env.close();

        auto const subId = getSubscriptionIndex(payer, merchant, env.seq(payer));
        env(subscription::create(payer, merchant, XRP(10), 100s));
        env.close();

        // The merchant delegates SubscriptionClaim to the processor.
        env(delegate::set(merchant, processor, {"SubscriptionClaim"}));
        env.close();

        auto const baseFee = env.current()->fees().base;
        auto const predPayer = env.balance(payer);
        auto const preMerchant = env.balance(merchant);
        auto const preProcessor = env.balance(processor);

        // The processor claims on the merchant's behalf: funds move
        // payer -> merchant. Under XLS-75 delegation the delegate (processor)
        // pays the transaction fee, so the merchant receives the full claim.
        env(subscription::claim(merchant, subId, XRP(10)), delegate::As(processor));
        env.close();
        BEAST_EXPECT(env.balance(payer) == predPayer - XRP(10));
        BEAST_EXPECT(env.balance(merchant) == preMerchant + XRP(10));
        BEAST_EXPECT(env.balance(processor) == preProcessor - baseFee);

        // The processor is not recorded on the object.
        {
            auto const [key, subSle] = subKeyAndSle(*env.current(), subId);
            if (BEAST_EXPECT(subSle))
            {
                BEAST_EXPECT(subSle->getAccountID(sfAccount) == payer.id());
                BEAST_EXPECT(subSle->getAccountID(sfDestination) == merchant.id());
            }
        }

        env.close(100s);

        // Revoking the delegation (removing the SubscriptionClaim permission)
        // makes the processor's next claim fail with the delegate retry code.
        env(delegate::set(merchant, processor, {"Payment"}));
        env.close();

        env(subscription::claim(merchant, subId, XRP(10)),
            delegate::As(processor),
            Ter(terNO_DELEGATE_PERMISSION));
        env.close();

        // A stranger submitting a claim directly (not as a delegate) is not
        // the destination: tecNO_PERMISSION.
        env(subscription::claim(stranger, subId, XRP(10)), Ter(tecNO_PERMISSION));
        env.close();

        // An account that was never delegated the permission fails with the
        // delegate retry code.
        env(subscription::claim(merchant, subId, XRP(10)),
            delegate::As(stranger),
            Ter(terNO_DELEGATE_PERMISSION));
        env.close();
    }

    void
    testExploitSingleUseReplay(FeatureBitset features)
    {
        testcase("exploit: single use replay");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        Env env{*this, features};
        env.fund(XRP(1000), alice, bob);
        env.close();

        auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
        env(subscription::create(alice, bob, XRP(10), 100s),
            Txflags(tfSingleUse | tfFullyCanonicalSig));
        env.close();

        auto const preAlice = env.balance(alice);
        env(subscription::claim(bob, subId, XRP(10)));
        env.close();
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(10));

        // Replaying the same claim cannot double-spend: the object is gone.
        env(subscription::claim(bob, subId, XRP(10)), Ter(tecNO_ENTRY));
        env.close();
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(10));
    }

    void
    testExploitUnmeteredPartialDrain(FeatureBitset features)
    {
        testcase("exploit: unmetered partial drain");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        Env env{*this, features};
        env.fund(XRP(1000), alice, bob);
        env.close();

        auto const startTime = env.now().time_since_epoch().count();
        auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
        env(subscription::create(alice, bob, XRP(10), 0s));
        env.close();

        // A partial claim does not shrink the per-claim cap: the following
        // claim is still capped at the full Amount, not the remainder. There
        // is no running balance to drain below the cap.
        env(subscription::claim(bob, subId, XRP(3)));
        env.close();
        validateSubscription(env, subId, XRP(10), XRP(10), 0, startTime);

        // Claiming above Amount is still rejected.
        env(subscription::claim(bob, subId, XRP(11)), Ter(tecLIMIT_EXCEEDED));
        env.close();

        // A full-Amount claim right after the partial one succeeds (the cap
        // did not drop to the remainder of 7).
        env(subscription::claim(bob, subId, XRP(10)));
        env.close();
        validateSubscription(env, subId, XRP(10), XRP(10), 0, startTime);
    }

    void
    testExploitFrequencyUpdateArrearsReset(FeatureBitset features)
    {
        testcase("exploit: frequency update arrears reset");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        Env env{*this, features};
        env.fund(XRP(1000), alice, bob);
        env.close();

        auto const start = env.now();
        auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
        env(subscription::create(alice, bob, XRP(10), 100s));
        env.close();

        // Let three periods accrue unclaimed. Without an update these arrears
        // would permit three back-to-back full claims.
        for (; env.now() < start + 300s; env.close())
        {
        }

        // A Frequency update resets a clean period: the accrued arrears are
        // forfeited, not carried over.
        env(subscription::update(alice, subId, XRP(10), std::nullopt, 100s));
        env.close();

        auto const preAlice = env.balance(alice);

        // Exactly one full claim is available immediately after the reset.
        env(subscription::claim(bob, subId, XRP(10)));
        env.close();
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(10));

        // The arrears did not survive the reset: no second claim yet.
        env(subscription::claim(bob, subId, XRP(10)), Ter(tecTOO_SOON));
        env.close();
        BEAST_EXPECT(env.balance(alice) == preAlice - XRP(10));
    }

    void
    testIOUEnablement(FeatureBitset features)
    {
        testcase("IOU Enablement");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        // Test with and without Subscription feature
        for (bool const withSubscription : {true, false})
        {
            auto const amend = withSubscription ? features : features - featureSubscription;
            Env env{*this, amend};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];

            env.fund(XRP(5000), alice, bob, gw);
            env.close();
            env.trust(USD(10000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env.close();

            auto const createResult = withSubscription ? Ter(tesSUCCESS) : Ter(temDISABLED);

            auto const seq1 = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, seq1);
            env(subscription::create(alice, bob, USD(100), 100s), createResult);
            env.close();

            if (withSubscription)
            {
                BEAST_EXPECT(subscriptionExists(*env.current(), subId));
                env(subscription::claim(bob, subId, USD(100)));
                env.close();
                env(subscription::cancel(alice, subId));
                env.close();
            }
        }
    }

    void
    testIOUSetPreflightInvalid(FeatureBitset features)
    {
        testcase("IOU Set Preflight Invalid");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        env.fund(XRP(1000), alice, bob, gw);
        env.close();

        // temBAD_AMOUNT: negative IOU
        {
            env(subscription::create(alice, bob, USD(-1), 100s), Ter(temBAD_AMOUNT));
            env.close();
        }

        // temBAD_AMOUNT: zero IOU
        {
            env(subscription::create(alice, bob, USD(0), 100s), Ter(temBAD_AMOUNT));
            env.close();
        }

        // temBAD_CURRENCY
        {
            IOU const BAD{gw, badCurrency()};
            env(subscription::create(alice, bob, BAD(10), 100s), Ter(temBAD_CURRENCY));
            env.close();
        }
    }

    void
    testIOUSetPreclaimInvalid(FeatureBitset features)
    {
        testcase("IOU Set Preclaim Invalid");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        env.fund(XRP(1000), alice, bob, gw);
        env.close();

        // tecNO_ISSUER: issuer doesn't exist
        {
            auto const dneGw = Account{"dneGateway"};
            auto const DNE = dneGw["USD"];
            env.memoize(dneGw);

            env(subscription::create(alice, bob, DNE(10), 100s), Ter(tecNO_ISSUER));
            env.close();
        }

        // tecNO_LINE: account doesn't have trustline to issuer
        {
            env(subscription::create(alice, bob, USD(10), 100s), Ter(tecNO_LINE));
            env.close();
        }

        // Setup for remaining tests
        env(fset(gw, asfRequireAuth));
        env.close();
        env.trust(USD(10000), alice, bob);
        env.close();

        // tecNO_AUTH: requireAuth set, account not authorized
        {
            env(subscription::create(alice, bob, USD(10), 100s), Ter(tecNO_AUTH));
            env.close();
        }

        // tecNO_AUTH: requireAuth set, destination not authorized
        {
            auto const aliceUSD = alice["USD"];
            env(trust(gw, aliceUSD(10'000)), Txflags(tfSetfAuth));
            env(subscription::create(alice, bob, USD(10), 100s), Ter(tecNO_AUTH));
            env.close();

            env(fclear(gw, asfRequireAuth));
            env.close();
        }

        env(fclear(gw, asfRequireAuth));
        env.close();
        env(pay(gw, alice, USD(5000)));
        env(pay(gw, bob, USD(5000)));
        env.close();

        // tecFROZEN: account is frozen
        {
            env(trust(gw, USD(10000), alice, tfSetFreeze));
            env.close();

            env(subscription::create(alice, bob, USD(10), 100s), Ter(tecFROZEN));
            env.close();

            env(trust(gw, USD(10000), alice, tfClearFreeze));
            env.close();
        }

        // tecFROZEN: destination is frozen
        {
            env(trust(gw, USD(10000), bob, tfSetFreeze));
            env.close();

            env(subscription::create(alice, bob, USD(10), 100s), Ter(tecFROZEN));
            env.close();

            env(trust(gw, USD(10000), bob, tfClearFreeze));
            env.close();
        }

        // tecINSUFFICIENT_FUNDS: balance is zero
        {
            env(pay(alice, gw, USD(5000)));
            env.close();

            env(subscription::create(alice, bob, USD(10), 100s), Ter(tecINSUFFICIENT_FUNDS));
            env.close();

            env(pay(gw, alice, USD(5000)));
            env.close();
        }

        // tecINSUFFICIENT_FUNDS: balance less than amount
        {
            env(subscription::create(alice, bob, USD(6000), 100s), Ter(tecINSUFFICIENT_FUNDS));
            env.close();
        }
    }

    void
    testIOUClaimPreclaimInvalid(FeatureBitset features)
    {
        testcase("IOU Claim Preclaim Invalid");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        // TODO: Will need to retest all of the functionality here.

        // tecNO_AUTH: dest not authorized after subscription created
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            auto const aliceUSD = alice["USD"];
            auto const bobUSD = bob["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env(fset(gw, asfRequireAuth));
            env.close();
            env(trust(gw, aliceUSD(10'000)), Txflags(tfSetfAuth));
            env(trust(gw, bobUSD(10'000)), Txflags(tfSetfAuth));
            env.trust(USD(10'000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(10'000)));
            env(pay(gw, bob, USD(10'000)));
            env.close();

            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::create(alice, bob, USD(100), 100s));
            env.close();

            // Unauthorize dest
            env(pay(bob, gw, USD(10'000)));
            env(trust(gw, bobUSD(0)), Txflags(tfSetfAuth));
            env(trust(bob, USD(0)));
            env.close();

            env.trust(USD(10'000), bob);
            env.close();

            env(subscription::claim(bob, subId, USD(100)), Ter(tecNO_AUTH));
            env.close();
        }
    }

    void
    testIOUClaimDoApplyInvalid(FeatureBitset features)
    {
        testcase("IOU Claim DoApply Invalid");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        env.fund(XRP(1000), alice, bob, gw);
        env.close();
        env.trust(USD(10000), alice, bob);
        env.close();
        env(pay(gw, alice, USD(5000)));
        env(pay(gw, bob, USD(5000)));
        env.close();

        // tecNO_LINE_INSUF_RESERVE: insufficient reserve to create trustline
        {
            auto const reserve = env.current()->fees().accountReserve(0, 1);
            auto const incReserve = env.current()->fees().increment;

            env.fund(reserve + (incReserve - 1), carol);
            env.close();

            auto const subId = getSubscriptionIndex(alice, carol, env.seq(alice));
            env(subscription::create(alice, carol, USD(10), 100s));
            env.close();

            env(subscription::claim(carol, subId, USD(10)), Ter(tecNO_LINE_INSUF_RESERVE));
            env.close();
        }
    }

    void
    testIOUBalances(FeatureBitset features)
    {
        testcase("IOU Balances");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        env.fund(XRP(5000), alice, bob, gw);
        env.close();
        env.trust(USD(10000), alice, bob);
        env.close();
        env(pay(gw, alice, USD(5000)));
        env(pay(gw, bob, USD(5000)));
        env.close();

        auto const outstandingUSD = USD(10000);

        // Create & Claim Subscription
        {
            auto const preAliceUSD = env.balance(alice, USD);
            auto const preBobUSD = env.balance(bob, USD);

            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::create(alice, bob, USD(1000), 100s));
            env.close();

            BEAST_EXPECT(env.balance(alice, USD) == preAliceUSD);
            BEAST_EXPECT(env.balance(bob, USD) == preBobUSD);
            BEAST_EXPECT(issuerBalance(env, gw, USD) == outstandingUSD);

            env(subscription::claim(bob, subId, USD(1000)));
            env.close();

            BEAST_EXPECT(env.balance(alice, USD) == preAliceUSD - USD(1000));
            BEAST_EXPECT(env.balance(bob, USD) == preBobUSD + USD(1000));
            BEAST_EXPECT(issuerBalance(env, gw, USD) == outstandingUSD);

            // Second claim
            env.close(100s);
            env(subscription::claim(bob, subId, USD(1000)));
            env.close();

            BEAST_EXPECT(env.balance(alice, USD) == preAliceUSD - USD(2000));
            BEAST_EXPECT(env.balance(bob, USD) == preBobUSD + USD(2000));
        }
    }

    void
    testIOUMetaAndOwnership(FeatureBitset features)
    {
        testcase("IOU Meta and Ownership");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        env.fund(XRP(5000), alice, bob, carol, gw);
        env.close();
        env.trust(USD(10000), alice, bob, carol);
        env.close();
        env(pay(gw, alice, USD(5000)));
        env(pay(gw, bob, USD(5000)));
        env(pay(gw, carol, USD(5000)));
        env.close();

        // Create subscriptions and check ownership
        {
            auto const seq1 = env.seq(alice);
            auto const subId1 = getSubscriptionIndex(alice, bob, seq1);
            env(subscription::create(alice, bob, USD(100), 100s));
            env.close();

            auto const sub1 = env.le(keylet::subscription(subId1));
            BEAST_EXPECT(sub1);

            Dir aod(*env.current(), keylet::ownerDir(alice.id()));
            BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 2);  // trustline + subscription
            BEAST_EXPECT(std::find(aod.begin(), aod.end(), sub1) != aod.end());

            Dir bod(*env.current(), keylet::ownerDir(bob.id()));
            BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 2);  // trustline + subscription
            BEAST_EXPECT(std::find(bod.begin(), bod.end(), sub1) != bod.end());

            env(subscription::cancel(alice, subId1));
            env.close();

            BEAST_EXPECT(!env.le(keylet::subscription(subId1)));
        }
    }

    void
    testIOURippleState(FeatureBitset features)
    {
        testcase("IOU RippleState");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        struct TestAccountData
        {
            jtx::Account src;
            jtx::Account dst;
            jtx::Account gw;
            bool hasTrustline;
        };

        std::array<TestAccountData, 4> tests = {{
            {Account("alice2"), Account("bob0"), Account{"gw0"}, false},
            {Account("carol0"), Account("dan1"), Account{"gw1"}, false},
            {Account("alice2"), Account("bob0"), Account{"gw0"}, true},
            {Account("carol0"), Account("dan1"), Account{"gw1"}, true},
        }};

        for (auto const& t : tests)
        {
            Env env{*this, features};
            auto const USD = t.gw["USD"];

            env.fund(XRP(5000), t.src, t.dst, t.gw);
            env.close();

            if (t.hasTrustline)
                env.trust(USD(100000), t.src, t.dst);
            else
                env.trust(USD(100000), t.src);
            env.close();

            env(pay(t.gw, t.src, USD(10000)));
            if (t.hasTrustline)
                env(pay(t.gw, t.dst, USD(10000)));
            env.close();

            auto const seq1 = env.seq(t.src);
            auto const subId = getSubscriptionIndex(t.src, t.dst, seq1);
            auto const delta = USD(1000);

            env(subscription::create(t.src, t.dst, delta, 100s));
            env.close();

            auto const preSrc = env.balance(t.src, USD);
            auto const preDst = env.balance(t.dst, USD);

            env(subscription::claim(t.dst, subId, delta));
            env.close();

            BEAST_EXPECT(env.balance(t.src, USD) == preSrc - delta);
            BEAST_EXPECT(env.balance(t.dst, USD) == preDst + delta);
        }
    }

    void
    testIOUGateway(FeatureBitset features)
    {
        testcase("IOU Gateway");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        // Issuer as source
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];

            env.fund(XRP(5000), alice, gw);
            env.close();
            env.trust(USD(100000), alice);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env.close();

            auto const seq1 = env.seq(alice);
            auto const subId = getSubscriptionIndex(gw, alice, seq1);
            auto const preSrc = env.balance(alice, USD);

            env(subscription::create(gw, alice, USD(1000), 100s));
            env.close();

            env(subscription::claim(alice, subId, USD(1000)));
            env.close();

            BEAST_EXPECT(env.balance(alice, USD) == preSrc + USD(1000));
            BEAST_EXPECT(env.balance(gw, USD) == USD(0));
        }

        // Issuer as destination
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];

            env.fund(XRP(5000), alice, gw);
            env.close();
            env.trust(USD(100000), alice);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env.close();

            auto const seq1 = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, gw, seq1);
            auto const preSrc = env.balance(alice, USD);

            env(subscription::create(alice, gw, USD(1000), 100s));
            env.close();

            env(subscription::claim(gw, subId, USD(1000)));
            env.close();

            BEAST_EXPECT(env.balance(alice, USD) == preSrc - USD(1000));
            BEAST_EXPECT(env.balance(gw, USD) == USD(0));
        }
    }

    void
    testIOUTransferRate(FeatureBitset features)
    {
        testcase("IOU Transfer Rate");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        env.fund(XRP(10000), alice, bob, gw);
        env(rate(gw, 1.25));
        env.close();
        env.trust(USD(100000), alice, bob);
        env.close();
        env(pay(gw, alice, USD(10000)));
        env(pay(gw, bob, USD(10000)));
        env.close();

        // Create subscription with transfer rate
        {
            auto const preAlice = env.balance(alice, USD);
            auto const preBob = env.balance(bob, USD);
            auto const seq1 = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, seq1);

            env(subscription::create(alice, bob, USD(125), 100s));
            env.close();

            // Rate changes after subscription creation
            env(rate(gw, 1.00));
            env.close();

            // Claim with new rate (should apply new rate for subscriptions)
            env(subscription::claim(bob, subId, USD(125)));
            env.close();

            BEAST_EXPECT(env.balance(alice, USD) == preAlice - USD(125));
            BEAST_EXPECT(env.balance(bob, USD) == preBob + USD(125));
        }
    }

    void
    testIOULimitAmount(FeatureBitset features)
    {
        testcase("IOU Limit Amount");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        // Create subscription and verify limit isn't changed
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];

            env.fund(XRP(1000), alice, bob, gw);
            env.close();
            env.trust(USD(10000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(1000)));
            env(pay(gw, bob, USD(1000)));
            env.close();

            auto const seq1 = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, seq1);
            auto const preBobLimit = env.limit(bob, USD);

            env(subscription::create(alice, bob, USD(125), 100s));
            env.close();

            env(subscription::claim(bob, subId, USD(125)));
            env.close();

            auto const postBobLimit = env.limit(bob, USD);
            BEAST_EXPECT(postBobLimit == preBobLimit);
        }

        // Create subscription and verify initial 0 limit
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];

            env.fund(XRP(1000), alice, bob, gw);
            env.close();
            env.trust(USD(10000), alice);
            env.close();
            env(pay(gw, alice, USD(1000)));
            env.close();

            auto const seq1 = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, seq1);
            auto const preBobLimit = env.limit(bob, USD);

            env(subscription::create(alice, bob, USD(125), 100s));
            env.close();

            env(subscription::claim(bob, subId, USD(125)));
            env.close();

            auto const postBobLimit = env.limit(bob, USD);
            BEAST_EXPECT(postBobLimit == preBobLimit);
        }
    }

    void
    testIOURequireAuth(FeatureBitset features)
    {
        testcase("IOU Require Auth");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        auto const aliceUSD = alice["USD"];
        auto const bobUSD = bob["USD"];

        env.fund(XRP(1000), alice, bob, gw);
        env(fset(gw, asfRequireAuth));
        env.close();

        env(trust(gw, aliceUSD(10000)), Txflags(tfSetfAuth));
        env(trust(alice, USD(10000)));
        env(trust(bob, USD(10000)));
        env.close();
        env(pay(gw, alice, USD(1000)));
        env.close();

        // Cannot create subscription without dest auth
        {
            env(subscription::create(alice, bob, USD(125), 100s), Ter(tecNO_AUTH));
            env.close();
        }

        // Set auth on bob and retry
        {
            env(trust(gw, bobUSD(10000)), Txflags(tfSetfAuth));
            env(trust(bob, USD(10000)));
            env.close();
            env(pay(gw, bob, USD(1000)));
            env.close();

            auto const seq1 = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, seq1);

            env(subscription::create(alice, bob, USD(125), 100s));
            env.close();

            env(subscription::claim(bob, subId, USD(125)));
            env.close();

            env(subscription::cancel(alice, subId));
            env.close();
        }
    }

    void
    testIOUFreeze(FeatureBitset features)
    {
        testcase("IOU Freeze");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        // Global Freeze
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];

            env.fund(XRP(10000), alice, bob, gw);
            env.close();
            env.trust(USD(100000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();

            env(fset(gw, asfGlobalFreeze));
            env.close();

            // Cannot create subscription with frozen assets
            env(subscription::create(alice, bob, USD(125), 100s), Ter(tecFROZEN));
            env.close();

            env(fclear(gw, asfGlobalFreeze));
            env.close();

            // Can create after unfreezing
            auto const seq1 = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, seq1);

            env(subscription::create(alice, bob, USD(125), 100s));
            env.close();

            // Freeze again
            env(fset(gw, asfGlobalFreeze));
            env.close();

            // Cannot claim with frozen assets
            env(subscription::claim(bob, subId, USD(125)), Ter(tecFROZEN));
            env.close();

            env(fclear(gw, asfGlobalFreeze));
            env(subscription::cancel(alice, subId));
            env.close();
        }

        // Individual Freeze
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];

            env.fund(XRP(10000), alice, bob, gw);
            env.close();
            env.trust(USD(100000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();

            // Freeze alice trustline
            env(trust(gw, USD(10000), alice, tfSetFreeze));
            env.close();

            // Cannot create subscription with frozen account
            env(subscription::create(alice, bob, USD(125), 100s), Ter(tecFROZEN));
            env.close();

            env(trust(gw, USD(10000), alice, tfClearFreeze));
            env.close();

            // Create subscription
            auto const seq1 = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, seq1);

            env(subscription::create(alice, bob, USD(125), 100s));
            env.close();

            // Freeze bob trustline
            env(trust(gw, USD(10000), bob, tfSetFreeze));
            env.close();

            // Cannot claim with frozen destination
            env(subscription::claim(bob, subId, USD(125)), Ter(tecFROZEN));
            env.close();

            env(trust(gw, USD(10000), bob, tfClearFreeze));
            env(subscription::cancel(alice, subId));
            env.close();
        }
    }

    void
    testIOUPrecisionLoss(FeatureBitset features)
    {
        testcase("IOU Precision Loss");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(USD(100000000000000000), alice, bob);
        env.close();
        env(pay(gw, alice, USD(10000000000000000)));
        env(pay(gw, bob, USD(1)));
        env.close();

        // Cannot create subscription with precision loss amount
        {
            // Large-mantissa amendments make this amount representable
            bool const largeMantissa =
                features[featureSingleAssetVault] || features[featureLendingProtocol];

            env(subscription::create(alice, bob, USD(1), 100s),
                Ter(largeMantissa ? (TER)tesSUCCESS : (TER)tecPRECISION_LOSS));
            env.close();

            // This amount works
            auto const seq1 = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, seq1);

            env(subscription::create(alice, bob, USD(1000), 100s));
            env.close();

            env(subscription::claim(bob, subId, USD(1000)));
            env.close();
        }
    }

    void
    testMPTEnablement(FeatureBitset features)
    {
        testcase("MPT Enablement");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        for (bool const withSubscription : {true, false})
        {
            auto const amend = withSubscription ? features : features - featureSubscription;
            Env env{*this, amend};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            env.fund(XRP(5000), bob);

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create({.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10000)));
            env.close();

            auto const createResult = withSubscription ? Ter(tesSUCCESS) : Ter(temDISABLED);

            auto const seq1 = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, seq1);
            env(subscription::create(alice, bob, MPT(1000), 100s), createResult);
            env.close();

            if (withSubscription)
            {
                env(subscription::claim(bob, subId, MPT(1000)));
                env.close();
                env(subscription::cancel(alice, subId));
                env.close();
            }
        }
    }

    void
    testMPTSetPreflightInvalid(FeatureBitset features)
    {
        testcase("MPT Set Preflight Invalid");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");

        MPTTester mptGw(env, gw, {.holders = {alice, bob}});
        mptGw.create({.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
        mptGw.authorize({.account = alice});
        mptGw.authorize({.account = bob});
        auto const MPT = mptGw["MPT"];

        // temBAD_AMOUNT: negative MPT
        {
            env(subscription::create(alice, bob, MPT(-1), 100s), Ter(temBAD_AMOUNT));
            env.close();
        }

        // temBAD_AMOUNT: zero MPT
        {
            env(subscription::create(alice, bob, MPT(0), 100s), Ter(temBAD_AMOUNT));
            env.close();
        }

        // temBAD_AMOUNT: exceeds max MPT amount
        // DA: Not Testable
    }

    void
    testMPTSetPreclaimInvalid(FeatureBitset features)
    {
        testcase("MPT Set Preclaim Invalid");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        // tecOBJECT_NOT_FOUND: mpt does not exist
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const mpt = jtx::MPT(alice.name(), makeMptID(env.seq(alice), alice));
            json::Value jv = subscription::create(alice, bob, mpt(10), 100s);
            jv[jss::Amount][jss::mpt_issuance_id] =
                "00000004A407AF5856CCF3C42619DAA925813FC955C72983";
            env(jv, Ter(tecOBJECT_NOT_FOUND));
            env.close();
        }

        // tecOBJECT_NOT_FOUND: account does not have the mpt
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            auto const MPT = mptGw["MPT"];

            env(subscription::create(alice, bob, MPT(4), 100s), Ter(tecOBJECT_NOT_FOUND));
            env.close();
        }

        // tecNO_AUTH: requireAuth set: account not authorized
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer | tfMPTRequireAuth});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = gw, .holder = alice});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env.close();

            // unauthorize account
            mptGw.authorize({.account = gw, .holder = alice, .flags = tfMPTUnauthorize});

            env(subscription::create(alice, bob, MPT(5), 100s), Ter(tecNO_AUTH));
            env.close();
        }

        // tecNO_AUTH: requireAuth set: dest not authorized
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer | tfMPTRequireAuth});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = gw, .holder = alice});
            mptGw.authorize({.account = bob});
            mptGw.authorize({.account = gw, .holder = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env(pay(gw, bob, MPT(10'000)));
            env.close();

            // unauthorize dest
            mptGw.authorize({.account = gw, .holder = bob, .flags = tfMPTUnauthorize});

            env(subscription::create(alice, bob, MPT(6), 100s), Ter(tecNO_AUTH));
            env.close();
        }

        // tecLOCKED: issuer has locked the account
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer | tfMPTCanLock});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env(pay(gw, bob, MPT(10'000)));
            env.close();

            // lock account
            mptGw.set({.account = gw, .holder = alice, .flags = tfMPTLock});

            env(subscription::create(alice, bob, MPT(7), 100s), Ter(tecLOCKED));
            env.close();
        }

        // tecLOCKED: issuer has locked the dest
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer | tfMPTCanLock});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env(pay(gw, bob, MPT(10'000)));
            env.close();

            // lock dest
            mptGw.set({.account = gw, .holder = bob, .flags = tfMPTLock});

            env(subscription::create(alice, bob, MPT(8), 100s), Ter(tecLOCKED));
            env.close();
        }

        // tecNO_AUTH: mpt cannot be transferred
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create({.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanEscrow});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env(pay(gw, bob, MPT(10'000)));
            env.close();

            env(subscription::create(alice, bob, MPT(9), 100s), Ter(tecNO_AUTH));
            env.close();
        }

        // tecINSUFFICIENT_FUNDS: spendable amount is zero
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, bob, MPT(10)));
            env.close();

            env(subscription::create(alice, bob, MPT(10), 100s), Ter(tecINSUFFICIENT_FUNDS));
            env.close();
        }

        // tecINSUFFICIENT_FUNDS: spendable amount is less than the amount
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10)));
            env(pay(gw, bob, MPT(10)));
            env.close();

            env(subscription::create(alice, bob, MPT(11), 100s), Ter(tecINSUFFICIENT_FUNDS));
            env.close();
        }
    }

    void
    testMPTClaimPreclaimInvalid(FeatureBitset features)
    {
        testcase("MPT Claim Preclaim Invalid");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        // tecNO_AUTH: dest not authorized after subscription created
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTRequireAuth});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = gw, .holder = alice});
            mptGw.authorize({.account = bob});
            mptGw.authorize({.account = gw, .holder = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10000)));
            env(pay(gw, bob, MPT(10000)));
            env.close();

            auto const seq1 = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, seq1);
            env(subscription::create(alice, bob, MPT(10), 100s));
            env.close();

            // Unauthorize dest
            mptGw.authorize({.account = gw, .holder = bob, .flags = tfMPTUnauthorize});

            env(subscription::claim(bob, subId, MPT(10)), Ter(tecNO_AUTH));
            env.close();
        }

        // tecLOCKED: dest is locked
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanLock});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10000)));
            env(pay(gw, bob, MPT(10000)));
            env.close();

            auto const seq1 = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, seq1);
            env(subscription::create(alice, bob, MPT(8), 100s));
            env.close();

            // Lock dest
            mptGw.set({.account = gw, .holder = bob, .flags = tfMPTLock});

            env(subscription::claim(bob, subId, MPT(8)), Ter(tecLOCKED));
            env.close();
        }
    }

    void
    testMPTClaimDoApply(FeatureBitset features)
    {
        testcase("MPT Claim DoApply");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        // tecINSUFFICIENT_RESERVE: insufficient reserve to create MPToken
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            auto const reserve = env.current()->fees().accountReserve(0, 1);
            auto const incReserve = env.current()->fees().increment;

            env.fund(reserve + (incReserve - 1), bob);
            env.close();

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create({.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10000)));
            env.close();

            auto const seq1 = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, seq1);
            env(subscription::create(alice, bob, MPT(10), 100s));
            env.close();

            env(subscription::claim(bob, subId, MPT(10)), Ter(tecINSUFFICIENT_RESERVE));
            env.close();
        }

        // tesSUCCESS: bob submits; finish MPT created
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            env.fund(XRP(10'000), bob);
            env.close();

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env.close();

            auto const seq1 = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, seq1);
            env(subscription::create(alice, bob, MPT(10), 100s));
            env.close();

            env(subscription::claim(bob, subId, MPT(10)), Ter(tesSUCCESS));
            env.close();
        }

        // tecNO_PERMISSION: MPToken not created for destination with
        // requireAuth
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            auto const gw = Account("gw");
            env.fund(XRP(10'000), bob, carol);
            env.close();

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env.close();

            auto const seq1 = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, seq1);
            env(subscription::create(alice, bob, MPT(10), 100s));
            env.close();

            env(subscription::claim(carol, subId, MPT(10)), Ter(tecNO_PERMISSION));
            env.close();
        }
    }

    void
    testMPTBalances(FeatureBitset features)
    {
        testcase("MPT Balances");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        env.fund(XRP(5000), bob);

        MPTTester mptGw(env, gw, {.holders = {alice}});
        mptGw.create({.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
        mptGw.authorize({.account = alice});
        auto const MPT = mptGw["MPT"];
        env(pay(gw, alice, MPT(10000)));
        env.close();

        auto outstandingMPT = env.balance(gw, MPT);

        // Create & Claim Subscription
        {
            auto const preAliceMPT = env.balance(alice, MPT);
            auto const preBobMPT = env.balance(bob, MPT);

            auto const seq1 = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, seq1);
            env(subscription::create(alice, bob, MPT(1000), 100s));
            env.close();

            BEAST_EXPECT(env.balance(alice, MPT) == preAliceMPT);
            BEAST_EXPECT(env.balance(bob, MPT) == preBobMPT);
            BEAST_EXPECT(env.balance(gw, MPT) == outstandingMPT);

            env(subscription::claim(bob, subId, MPT(1000)));
            env.close();

            BEAST_EXPECT(env.balance(alice, MPT) == preAliceMPT - MPT(1000));
            BEAST_EXPECT(env.balance(bob, MPT) == preBobMPT + MPT(1000));
            BEAST_EXPECT(env.balance(gw, MPT) == outstandingMPT);

            // Second claim
            env.close(100s);
            env(subscription::claim(bob, subId, MPT(1000)));
            env.close();

            BEAST_EXPECT(env.balance(alice, MPT) == preAliceMPT - MPT(2000));
            BEAST_EXPECT(env.balance(bob, MPT) == preBobMPT + MPT(2000));
        }
    }

    void
    testMPTMetaAndOwnership(FeatureBitset features)
    {
        testcase("MPT Meta and Ownership");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");

        MPTTester mptGw(env, gw, {.holders = {alice, bob}});
        mptGw.create({.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
        mptGw.authorize({.account = alice});
        mptGw.authorize({.account = bob});
        auto const MPT = mptGw["MPT"];
        env(pay(gw, alice, MPT(10000)));
        env(pay(gw, bob, MPT(10000)));
        env.close();

        // Create subscription and check ownership
        {
            auto const seq1 = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, seq1);
            env(subscription::create(alice, bob, MPT(100), 100s));
            env.close();

            auto const sub = env.le(keylet::subscription(subId));
            BEAST_EXPECT(sub);

            Dir aod(*env.current(), keylet::ownerDir(alice.id()));
            BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 2);  // mptoken + subscription
            BEAST_EXPECT(std::find(aod.begin(), aod.end(), sub) != aod.end());

            Dir bod(*env.current(), keylet::ownerDir(bob.id()));
            BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 2);  // mptoken + subscription
            BEAST_EXPECT(std::find(bod.begin(), bod.end(), sub) != bod.end());

            env(subscription::cancel(alice, subId));
            env.close();

            BEAST_EXPECT(!env.le(keylet::subscription(subId)));
        }
    }

    void
    testMPTGateway(FeatureBitset features)
    {
        testcase("MPT Gateway");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        // Issuer as source
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create({.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10000)));
            env.close();

            auto const seq1 = env.seq(gw);
            auto const subId = getSubscriptionIndex(gw, alice, seq1);
            auto const preAliceMPT = env.balance(alice, MPT);
            auto const preOutstanding = env.balance(gw, MPT);

            env(subscription::create(gw, alice, MPT(1000), 100s));
            env.close();

            env(subscription::claim(alice, subId, MPT(1000)));
            env.close();

            BEAST_EXPECT(env.balance(alice, MPT) == preAliceMPT + MPT(1000));
            BEAST_EXPECT(env.balance(gw, MPT) == preOutstanding - MPT(1000));
        }

        // Issuer as destination
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create({.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10000)));
            env.close();

            auto const seq1 = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, gw, seq1);
            auto const preAliceMPT = env.balance(alice, MPT);
            auto const preOutstanding = env.balance(gw, MPT);

            env(subscription::create(alice, gw, MPT(1000), 100s));
            env.close();

            env(subscription::claim(gw, subId, MPT(1000)));
            env.close();

            BEAST_EXPECT(env.balance(alice, MPT) == preAliceMPT - MPT(1000));
            BEAST_EXPECT(env.balance(gw, MPT) == preOutstanding + MPT(1000));
        }
    }

    void
    testMPTTransferRate(FeatureBitset features)
    {
        testcase("MPT Transfer Rate");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");

        MPTTester mptGw(env, gw, {.holders = {alice, bob}});
        mptGw.create(
            {.transferFee = 25000,  // 2.5%
             .ownerCount = 1,
             .holderCount = 0,
             .flags = tfMPTCanTransfer});
        mptGw.authorize({.account = alice});
        mptGw.authorize({.account = bob});
        auto const MPT = mptGw["MPT"];
        env(pay(gw, alice, MPT(10000)));
        env(pay(gw, bob, MPT(10000)));
        env.close();

        // Create subscription with transfer fee
        {
            auto const preAlice = env.balance(alice, MPT);
            auto const preBob = env.balance(bob, MPT);
            auto const seq1 = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, seq1);

            env(subscription::create(alice, bob, MPT(125), 100s));
            env.close();

            env(subscription::claim(bob, subId, MPT(125)));
            env.close();

            BEAST_EXPECT(env.balance(alice, MPT) == preAlice - MPT(156));
            // Bob receives 125
            BEAST_EXPECT(env.balance(bob, MPT) == preBob + MPT(125));

            env(subscription::cancel(alice, subId));
            env.close();
        }
    }

    void
    testMPTRequireAuth(FeatureBitset features)
    {
        testcase("MPT Require Auth");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");

        MPTTester mptGw(env, gw, {.holders = {alice, bob}});
        mptGw.create(
            {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTRequireAuth});
        mptGw.authorize({.account = alice});
        mptGw.authorize({.account = gw, .holder = alice});
        mptGw.authorize({.account = bob});
        mptGw.authorize({.account = gw, .holder = bob});
        auto const MPT = mptGw["MPT"];
        env(pay(gw, alice, MPT(10000)));
        env.close();

        // Create subscription with both authorized
        {
            auto const seq1 = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, seq1);

            env(subscription::create(alice, bob, MPT(100), 100s));
            env.close();

            env(subscription::claim(bob, subId, MPT(100)));
            env.close();

            env(subscription::cancel(alice, subId));
            env.close();
        }
    }

    void
    testMPTLock(FeatureBitset features)
    {
        testcase("MPT Lock");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");

        MPTTester mptGw(env, gw, {.holders = {alice, bob}});
        mptGw.create({.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer | tfMPTCanLock});
        mptGw.authorize({.account = alice});
        mptGw.authorize({.account = bob});
        auto const MPT = mptGw["MPT"];
        env(pay(gw, alice, MPT(10000)));
        env(pay(gw, bob, MPT(10000)));
        env.close();

        // Create subscription
        {
            auto const seq1 = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, seq1);
            env(subscription::create(alice, bob, MPT(100), 100s));
            env.close();

            // Lock both accounts
            mptGw.set({.account = gw, .holder = alice, .flags = tfMPTLock});
            mptGw.set({.account = gw, .holder = bob, .flags = tfMPTLock});

            // Cannot claim when locked
            env(subscription::claim(bob, subId, MPT(100)), Ter(tecLOCKED));
            env.close();

            // Unlock and cleanup
            mptGw.set({.account = gw, .holder = alice, .flags = tfMPTUnlock});
            mptGw.set({.account = gw, .holder = bob, .flags = tfMPTUnlock});
            env(subscription::cancel(alice, subId));
            env.close();
        }
    }

    void
    testMPTCanTransfer(FeatureBitset features)
    {
        if (!features[featureMPTokensV1])
            return;

        testcase("MPT Can Transfer");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");

        MPTTester mptGw(env, gw, {.holders = {alice, bob}});
        mptGw.create({.ownerCount = 1, .holderCount = 0, .flags = 0});  // No tfMPTCanTransfer
        mptGw.authorize({.account = alice});
        mptGw.authorize({.account = bob});
        auto const MPT = mptGw["MPT"];
        env(pay(gw, alice, MPT(10000)));
        env(pay(gw, bob, MPT(10000)));
        env.close();

        // Cannot create subscription to non-issuer without transfer
        {
            env(subscription::create(alice, bob, MPT(100), 100s), Ter(tecNO_AUTH));
            env.close();
        }

        // Can create subscription to issuer
        {
            auto const seq1 = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, gw, seq1);
            env(subscription::create(alice, gw, MPT(100), 100s));
            env.close();

            env(subscription::claim(gw, subId, MPT(100)));
            env.close();

            env(subscription::cancel(alice, subId));
            env.close();
        }
    }

    void
    testIOUWithFeats(FeatureBitset features)
    {
        testIOUEnablement(features);
        testIOUSetPreflightInvalid(features);
        testIOUSetPreclaimInvalid(features);
        // testIOUClaimPreclaimInvalid(features); // TODO: Extra Duplication
        testIOUClaimDoApplyInvalid(features);
        testIOUBalances(features);
        testIOUMetaAndOwnership(features);
        testIOURippleState(features);
        testIOUGateway(features);
        testIOUTransferRate(features);
        testIOULimitAmount(features);
        testIOURequireAuth(features);
        testIOUFreeze(features);
        testIOUPrecisionLoss(features);
    }

    void
    testMPTWithFeats(FeatureBitset features)
    {
        testMPTEnablement(features);
        testMPTSetPreflightInvalid(features);
        testMPTSetPreclaimInvalid(features);
        // testMPTClaimPreclaimInvalid(features); // TODO: Extra Duplication
        testMPTClaimDoApply(features);
        testMPTBalances(features);
        testMPTMetaAndOwnership(features);
        testMPTGateway(features);
        testMPTTransferRate(features);
        testMPTRequireAuth(features);
        testMPTLock(features);
        testMPTCanTransfer(features);
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testEnabled(features);
        testSetPreflightInvalid(features);
        testSetPreclaimInvalid(features);
        testSetDoApplyInvalid(features);
        testCancelPreflightInvalid(features);
        testCancelPreclaimInvalid(features);
        testClaimPreflightInvalid(features);
        testClaimPreclaimInvalid(features);
        testClaimDoApplyInvalid(features);
        testSet(features);
        testUpdate(features);
        testCancel(features);
        testClaim(features);
        testDstTag(features);
        testMetaAndOwnership(features);
        testAccountDelete(features);
        testUsingTickets(features);
        testExpiredSubscription(features);
        testTimingBoundaries(features);
        testConsequences(features);
        testMultipleSubscriptionsSamePair(features);
        testRegularKey(features);
        testMultisign(features);
        testDelegation(features);
        testAccountObjectsRPC(features);
        testLedgerEntryRPC(features);
        testDepositAuthDestination(features);
        testExploitThirdPartyCancel(features);
        testExploitExpiredDrain(features);
        testExploitAssetSwitchUpdate(features);
        testExploitClaimOverdraw(features);
        testExploitBoundaryStraddle(features);
        testExploitArrearsExactness(features);
        testExploitOwnerClaim(features);
        testUpdateRequireAuth(features);
        testSequenceField(features);
        testReserveEdge(features);

        // Unmetered mode, single-use, flexible updates, delegated pull
        testUnmeteredMode(features);
        testUnmeteredClaims(features);
        testSingleUse(features);
        testSingleUseMetered(features);
        testSingleUseImmutable(features);
        testUpdateFrequency(features);
        testUpdateFrequencyTransitions(features);
        testUpdateRemoveExpiration(features);
        testDelegatedPull(features);
        testExploitSingleUseReplay(features);
        testExploitUnmeteredPartialDrain(features);
        testExploitFrequencyUpdateArrearsReset(features);

        // IOU-specific tests
        testIOUWithFeats(features);

        // MPT-specific tests
        testMPTWithFeats(features);

        // TODO: Can a MPT/Token/Issuance be destroyed while a subscription
        // exists?
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        auto const sa = testableAmendments() | featureSubscription;
        testWithFeats(sa);
    }
};

BEAST_DEFINE_TESTSUITE(Subscription, app, xrpl);
}  // namespace xrpl::test

//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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
#include <test/jtx/subscription.h>

#include <xrpld/ledger/Dir.h>

#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {
struct Subscription_test : public beast::unit_test::suite
{
    static uint256
    getSubscriptionIndex(
        AccountID const& account,
        AccountID const& dest,
        std::uint32_t uSequence)
    {
        return keylet::subscription(account, dest, uSequence).key;
    }

    static bool
    inOwnerDir(
        ReadView const& view,
        jtx::Account const& acct,
        std::shared_ptr<SLE const> const& token)
    {
        ripple::Dir const ownerDir(view, keylet::ownerDir(acct.id()));
        return std::find(ownerDir.begin(), ownerDir.end(), token) !=
            ownerDir.end();
    }

    static std::size_t
    ownerDirCount(ReadView const& view, jtx::Account const& acct)
    {
        ripple::Dir const ownerDir(view, keylet::ownerDir(acct.id()));
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
    issuerBalance(
        jtx::Env& env,
        jtx::Account const& account,
        Issue const& issue)
    {
        Json::Value params;
        params[jss::account] = account.human();
        auto jrr = env.rpc("json", "gateway_balances", to_string(params));
        auto const result = jrr[jss::result];
        auto const obligations =
            result[jss::obligations][to_string(issue.currency)];
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
            auto const amend =
                withSubscription ? features : features - featureSubscription;
            Env env{*this, amend};

            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const txResult =
                withSubscription ? ter(tesSUCCESS) : ter(temDISABLED);
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
                txflags(tfSetfAuth),
                ter(temINVALID_FLAG));
            env.close();
        }

        // temBAD_FEE: Exercises invalid preflight1
        {
            env(subscription::create(alice, bob, XRP(10), 100s),
                fee(XRP(-1)),
                ter(temBAD_FEE));
            env.close();
        }

        // temMALFORMED: no sfDestination
        {
            Json::Value txn;
            txn[jss::TransactionType] = jss::SubscriptionSet;
            txn[jss::Account] = alice.human();
            txn[sfAmount.jsonName] = XRP(10).value().getJson(JsonOptions::none);
            NetClock::duration const frequency = 100s;
            txn[sfFrequency.jsonName] = frequency.count();
            env(txn, ter(temMALFORMED));
            env.close();
        }

        // temMALFORMED: no sfAmount
        {
            Json::Value txn;
            txn[jss::TransactionType] = jss::SubscriptionSet;
            txn[jss::Account] = alice.human();
            txn[sfDestination.jsonName] = bob.human();
            NetClock::duration const frequency = 100s;
            txn[sfFrequency.jsonName] = frequency.count();
            env(txn, ter(temMALFORMED));
            env.close();
        }

        // temMALFORMED: no sfFrequency
        {
            Json::Value txn;
            txn[jss::TransactionType] = jss::SubscriptionSet;
            txn[jss::Account] = alice.human();
            txn[sfDestination.jsonName] = bob.human();
            txn[sfAmount.jsonName] = XRP(10).value().getJson(JsonOptions::none);
            env(txn, ter(temMALFORMED));
            env.close();
        }

        // temDST_IS_SRC
        {
            env(subscription::create(alice, alice, XRP(10), 100s),
                ter(temDST_IS_SRC));
            env.close();
        }

        /*
        UPDATE
        */

        // temMALFORMED: sfDestination present with sfSubscriptionID
        {
            auto const aliceSeq = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, aliceSeq);
            Json::Value txn = subscription::update(alice, subId, XRP(10));
            txn[sfDestination.jsonName] = bob.human();
            env(txn, ter(temMALFORMED));
            env.close();
        }

        // temMALFORMED: sfFrequency present with sfSubscriptionID
        {
            auto const aliceSeq = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, aliceSeq);
            Json::Value txn = subscription::update(alice, subId, XRP(10));
            auto const frequency = 100s;
            txn[sfFrequency.jsonName] = to_string(frequency.count());
            env(txn, ter(temMALFORMED));
            env.close();
        }

        // temMALFORMED: sfStartTime present with sfSubscriptionID
        {
            auto const aliceSeq = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, aliceSeq);
            Json::Value txn = subscription::update(alice, subId, XRP(10));
            auto const startTime = env.now() + 0s;
            txn[sfStartTime.jsonName] =
                to_string(startTime.time_since_epoch().count());
            env(txn, ter(temMALFORMED));
            env.close();
        }

        /*
        BOTH CREATE AND UPDATE
        */

        //----------------------------------------------------------------------
        // XRP

        // temBAD_AMOUNT: negative XRP
        {
            env(subscription::create(alice, bob, XRP(-10), 100s),
                ter(temBAD_AMOUNT));
            env.close();
        }

        // temBAD_AMOUNT: zero XRP
        {
            env(subscription::create(alice, bob, XRP(0), 100s),
                ter(temBAD_AMOUNT));
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
            env(subscription::create(alice, dne, XRP(10), 100s),
                ter(tecNO_DST));
            env.close();
        }

        // temMALFORMED: frequency <= 0
        {
            env(subscription::create(alice, bob, XRP(10), 0s),
                ter(temMALFORMED));
            env.close();
        }

        // temMALFORMED: start time in the past
        {
            auto const start = env.now() - 10s;
            env(subscription::create(alice, bob, XRP(10), 100s),
                subscription::start_time(start),
                ter(temMALFORMED));
            env.close();
        }

        // temBAD_EXPIRATION: expiration in the past
        {
            auto const expire = env.now() - 10s;
            env(subscription::create(alice, bob, XRP(10), 100s, expire),
                ter(temBAD_EXPIRATION));
            env.close();
        }

        // temBAD_EXPIRATION: expiration before start time
        {
            auto const start = env.now() + 100s;
            auto const expire = env.now() + 50s;
            env(subscription::create(alice, bob, XRP(10), 100s, expire),
                subscription::start_time(start),
                ter(temBAD_EXPIRATION));
            env.close();
        }

        // tecDST_TAG_NEEDED
        {
            env(fset(bob, asfRequireDest));
            env.close();

            env(subscription::create(alice, bob, XRP(10), 100s),
                ter(tecDST_TAG_NEEDED));
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
            env(subscription::update(alice, subId, XRP(100)), ter(tecNO_ENTRY));
            env.close();
        }

        // tecNO_PERMISSION: non-owner tries to update
        {
            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::create(alice, bob, XRP(100), 100s));
            env.close();

            env(subscription::update(bob, subId, XRP(100)),
                ter(tecNO_PERMISSION));
            env.close();
        }

        // temBAD_EXPIRATION: update with past expiration
        {
            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::create(alice, bob, XRP(100), 100s));
            env.close();

            auto const expire = env.now() - 10s;
            env(subscription::update(alice, subId, XRP(100), expire),
                ter(temBAD_EXPIRATION));
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
            auto const reserve = env.current()->fees().accountReserve(0);
            auto const incReserve = env.current()->fees().increment;

            env.fund(reserve + incReserve - XRP(1), alice);
            env.fund(XRP(1000), bob);
            env.close();

            env(subscription::create(alice, bob, XRP(10), 100s),
                ter(tecINSUFFICIENT_RESERVE));
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
            env(subscription::cancel(alice, subId),
                txflags(tfSetfAuth),
                ter(temINVALID_FLAG));
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
            env(subscription::cancel(alice, subId), ter(tecNO_ENTRY));
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
                txflags(tfSetfAuth),
                ter(temINVALID_FLAG));
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
            env(subscription::claim(bob, subId, XRP(10)), ter(tecNO_ENTRY));
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

            env(subscription::claim(carol, subId, XRP(1)),
                ter(tecNO_PERMISSION));
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
            env(subscription::claim(bob, subId, USD(1)), ter(tecWRONG_ASSET));
            env.close();
        }

        // temBAD_AMOUNT: claim more than subscription amount
        {
            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::create(alice, bob, XRP(10), 100s));
            env.close();

            env(subscription::claim(bob, subId, XRP(11)), ter(temBAD_AMOUNT));
            env.close();
        }

        // tecUNFUNDED: insufficient subscription balance
        {
            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::create(alice, bob, XRP(10), 100s));
            env.close();

            env(subscription::claim(bob, subId, XRP(1)));
            env.close();

            env(subscription::claim(bob, subId, XRP(11)), ter(temBAD_AMOUNT));
            env.close();
        }

        // tecTOO_SOON: subscription hasn't reached next payment time
        {
            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            auto const startTime = env.now() + 1000s;
            env(subscription::create(alice, bob, XRP(10), 100s),
                subscription::start_time(startTime));
            env.close();

            env(subscription::claim(bob, subId, XRP(10)), ter(tecTOO_SOON));
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

            env(subscription::claim(alice, subId, XRP(1)),
                ter(tecNO_PERMISSION));
            env.close();
        }

        // tecINSUFFICIENT_FUNDS: XRP
        {
            auto const aliceSeq = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, aliceSeq);

            env(subscription::create(alice, bob, XRP(1000), 100s));
            env.close();

            env(subscription::claim(bob, subId, XRP(1000)),
                ter(tecINSUFFICIENT_FUNDS));
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
                subSle->getFieldU32(sfNextClaimTime) ==
                startTime.time_since_epoch().count());
            BEAST_EXPECT(!subSle->isFieldPresent(sfExpiration));
        }

        // StartTime & Expiration
        {
            auto const aliceSeq = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, aliceSeq);

            auto const startTime = env.now() + 100s;
            auto const expiration = env.now() + 300s;
            auto const frequency = 100s;
            env(subscription::create(
                    alice, bob, XRP(10), frequency, expiration),
                subscription::start_time(startTime));
            env.close();

            auto const [key, subSle] = subKeyAndSle(*env.current(), subId);
            BEAST_EXPECT(subSle->getFieldAmount(sfAmount) == XRP(10));
            BEAST_EXPECT(subSle->getFieldU32(sfFrequency) == frequency.count());
            BEAST_EXPECT(
                subSle->getFieldU32(sfNextClaimTime) ==
                startTime.time_since_epoch().count());
            BEAST_EXPECT(
                subSle->getFieldU32(sfExpiration) ==
                expiration.time_since_epoch().count());
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
            BEAST_EXPECT(
                subSle->getFieldU32(sfExpiration) ==
                expire.time_since_epoch().count());
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

            validateSubscription(
                env, subId, XRP(10), XRP(10), frequency.count(), startTime);

            auto preAlice = env.balance(alice);
            auto preBob = env.balance(bob);

            // First Partial claim
            env(subscription::claim(bob, subId, XRP(5)));
            env.close();

            validateSubscription(
                env, subId, XRP(10), XRP(5), frequency.count(), startTime);
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(5));
            BEAST_EXPECT(
                env.balance(bob) ==
                preBob - env.current()->fees().base + XRP(5));

            preAlice = env.balance(alice);
            preBob = env.balance(bob);

            // Claim too soon, do not have sufficient funds
            env(subscription::claim(bob, subId, XRP(10)),
                ter(tecINSUFFICIENT_FUNDS));
            env.close();

            validateSubscription(
                env, subId, XRP(10), XRP(5), frequency.count(), startTime);
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
            BEAST_EXPECT(
                env.balance(bob) ==
                preBob - (env.current()->fees().base * 2) + XRP(10));

            // Cannot claim again yet
            env(subscription::claim(bob, subId, XRP(10)), ter(tecTOO_SOON));
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

            validateSubscription(
                env, subId, XRP(10), XRP(10), frequency.count(), startTime);

            auto preAlice = env.balance(alice);
            auto preBob = env.balance(bob);

            // First Partial claim
            env(subscription::claim(bob, subId, XRP(10)));
            env.close();

            validateSubscription(
                env,
                subId,
                XRP(10),
                XRP(10),
                frequency.count(),
                startTime + frequency.count());
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(10));
            BEAST_EXPECT(
                env.balance(bob) ==
                preBob - env.current()->fees().base + XRP(10));

            preAlice = env.balance(alice);
            preBob = env.balance(bob);

            // Cannot claim full amount yet
            env(subscription::claim(bob, subId, XRP(10)), ter(tecTOO_SOON));
            env.close();

            validateSubscription(
                env,
                subId,
                XRP(10),
                XRP(10),
                frequency.count(),
                startTime + frequency.count());
            BEAST_EXPECT(
                env.now().time_since_epoch().count() <
                getNextPaymentTime(*env.current(), subId));

            // Advance time
            env.close(60s);
            BEAST_EXPECT(
                env.now().time_since_epoch().count() ==
                getNextPaymentTime(*env.current(), subId));

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
            BEAST_EXPECT(
                env.balance(bob) ==
                preBob - (env.current()->fees().base * 2) + XRP(10));

            // Cannot claim again yet
            env(subscription::claim(bob, subId, XRP(10)), ter(tecTOO_SOON));
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

            validateSubscription(
                env, subId, XRP(10), XRP(10), frequency.count(), startTime);

            auto preAlice = env.balance(alice);
            auto preBob = env.balance(bob);

            // Advance time 3x
            env.close(frequency);
            env.close(frequency);
            env.close(frequency);
            BEAST_EXPECT(
                env.now().time_since_epoch().count() >
                getNextPaymentTime(*env.current(), subId) +
                    frequency.count() * 3);

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
            env(subscription::claim(bob, subId, XRP(10)), ter(tecTOO_SOON));
            env.close();

            validateSubscription(
                env,
                subId,
                XRP(10),
                XRP(10),
                frequency.count(),
                startTime + (frequency.count() * 4));
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(40));
            BEAST_EXPECT(
                env.balance(bob) ==
                preBob - (env.current()->fees().base * 5) + XRP(40));
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
            env(subscription::create(alice, bob, XRP(10), 100s),
                ter(tecDST_TAG_NEEDED));
            env.close();

            BEAST_EXPECT(!subscriptionExists(*env.current(), subId));
        }

        {
            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::create(alice, bob, XRP(10), 100s), dtag(1));
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
            ripple::Dir aliceDir(*env.current(), keylet::ownerDir(alice.id()));
            BEAST_EXPECT(std::distance(aliceDir.begin(), aliceDir.end()) == 1);
            BEAST_EXPECT(
                std::find(aliceDir.begin(), aliceDir.end(), sub) !=
                aliceDir.end());

            ripple::Dir bobDir(*env.current(), keylet::ownerDir(bob.id()));
            BEAST_EXPECT(std::distance(bobDir.begin(), bobDir.end()) == 1);
            BEAST_EXPECT(
                std::find(bobDir.begin(), bobDir.end(), sub) != bobDir.end());

            // Cancel subscription
            env(subscription::cancel(alice, subId));
            env.close();

            BEAST_EXPECT(!env.le(keylet::subscription(subId)));

            ripple::Dir aliceDir2(*env.current(), keylet::ownerDir(alice.id()));
            BEAST_EXPECT(
                std::distance(aliceDir2.begin(), aliceDir2.end()) == 0);

            ripple::Dir bobDir2(*env.current(), keylet::ownerDir(bob.id()));
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
            ripple::Dir aliceDir(*env.current(), keylet::ownerDir(alice.id()));
            BEAST_EXPECT(std::distance(aliceDir.begin(), aliceDir.end()) == 2);

            ripple::Dir bobDir(*env.current(), keylet::ownerDir(bob.id()));
            BEAST_EXPECT(std::distance(bobDir.begin(), bobDir.end()) == 2);

            ripple::Dir carolDir(*env.current(), keylet::ownerDir(carol.id()));
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

        auto rmAccount = [this](
                             Env& env,
                             Account const& toRm,
                             Account const& dst,
                             TER expectedTer = tesSUCCESS) {
            // only allow an account to be deleted if the account's sequence
            // number is at least 256 less than the current ledger sequence
            for (auto minRmSeq = env.seq(toRm) + 257;
                 env.current()->seq() < minRmSeq;
                 env.close())
            {
            }

            env(acctdelete(toRm, dst),
                fee(drops(env.current()->fees().increment)),
                ter(expectedTer));
            env.close();
            this->BEAST_EXPECT(
                isTesSuccess(expectedTer) ==
                !env.closed()->exists(keylet::account(toRm.id())));
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
            env(subscription::create(alice, bob, XRP(10), 100s),
                ticket::use(aliceTicketSeq++));
            env.close();

            env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
            BEAST_EXPECT(env.seq(alice) == aliceSeq);

            env(subscription::claim(bob, subId, XRP(10)),
                ticket::use(bobTicketSeq++));
            env.close();

            env.require(tickets(bob, env.seq(bob) - bobTicketSeq));
            BEAST_EXPECT(env.seq(bob) == bobSeq);

            env(subscription::cancel(alice, subId),
                ticket::use(aliceTicketSeq++));
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
            env(subscription::create(alice, bob, XRP(10), 100s),
                ticket::use(aliceTicketSeq++));
            env.close();

            env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
            BEAST_EXPECT(env.seq(alice) == aliceSeq);

            env(subscription::claim(bob, subId, XRP(10)),
                ticket::use(bobTicketSeq++));
            env.close();

            env.require(tickets(bob, env.seq(bob) - bobTicketSeq));
            BEAST_EXPECT(env.seq(bob) == bobSeq);

            env(subscription::cancel(bob, subId), ticket::use(bobTicketSeq++));
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

        // Second payment at expiration should succeed and delete
        // subscription
        env(subscription::claim(bob, subId, XRP(10)));
        env.close();

        BEAST_EXPECT(!subscriptionExists(*env.current(), subId));

        // Further claims should fail
        env(subscription::claim(bob, subId, XRP(10)), ter(tecNO_ENTRY));
        env.close();
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
            auto const amend =
                withSubscription ? features : features - featureSubscription;
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

            auto const createResult =
                withSubscription ? ter(tesSUCCESS) : ter(temDISABLED);

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
            env(subscription::create(alice, bob, USD(-1), 100s),
                ter(temBAD_AMOUNT));
            env.close();
        }

        // temBAD_AMOUNT: zero IOU
        {
            env(subscription::create(alice, bob, USD(0), 100s),
                ter(temBAD_AMOUNT));
            env.close();
        }

        // temBAD_CURRENCY
        {
            IOU const BAD{gw, badCurrency()};
            env(subscription::create(alice, bob, BAD(10), 100s),
                ter(temBAD_CURRENCY));
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

            env(subscription::create(alice, bob, DNE(10), 100s),
                ter(tecNO_ISSUER));
            env.close();
        }

        // tecNO_LINE: account doesn't have trustline to issuer
        {
            env(subscription::create(alice, bob, USD(10), 100s),
                ter(tecNO_LINE));
            env.close();
        }

        // Setup for remaining tests
        env(fset(gw, asfRequireAuth));
        env.close();
        env.trust(USD(10000), alice, bob);
        env.close();

        // tecNO_AUTH: requireAuth set, account not authorized
        {
            env(subscription::create(alice, bob, USD(10), 100s),
                ter(tecNO_AUTH));
            env.close();
        }

        // tecNO_AUTH: requireAuth set, destination not authorized
        {
            auto const aliceUSD = alice["USD"];
            env(trust(gw, aliceUSD(10'000)), txflags(tfSetfAuth));
            env(subscription::create(alice, bob, USD(10), 100s),
                ter(tecNO_AUTH));
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

            env(subscription::create(alice, bob, USD(10), 100s),
                ter(tecFROZEN));
            env.close();

            env(trust(gw, USD(10000), alice, tfClearFreeze));
            env.close();
        }

        // tecFROZEN: destination is frozen
        {
            env(trust(gw, USD(10000), bob, tfSetFreeze));
            env.close();

            env(subscription::create(alice, bob, USD(10), 100s),
                ter(tecFROZEN));
            env.close();

            env(trust(gw, USD(10000), bob, tfClearFreeze));
            env.close();
        }

        // tecINSUFFICIENT_FUNDS: balance is zero
        {
            env(pay(alice, gw, USD(5000)));
            env.close();

            env(subscription::create(alice, bob, USD(10), 100s),
                ter(tecINSUFFICIENT_FUNDS));
            env.close();

            env(pay(gw, alice, USD(5000)));
            env.close();
        }

        // tecINSUFFICIENT_FUNDS: balance less than amount
        {
            env(subscription::create(alice, bob, USD(6000), 100s),
                ter(tecINSUFFICIENT_FUNDS));
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
            env(trust(gw, aliceUSD(10'000)), txflags(tfSetfAuth));
            env(trust(gw, bobUSD(10'000)), txflags(tfSetfAuth));
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
            env(trust(gw, bobUSD(0)), txflags(tfSetfAuth));
            env(trust(bob, USD(0)));
            env.close();

            env.trust(USD(10'000), bob);
            env.close();

            env(subscription::claim(bob, subId, USD(100)), ter(tecNO_AUTH));
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
            auto const reserve = env.current()->fees().accountReserve(0);
            auto const incReserve = env.current()->fees().increment;

            env.fund(reserve + (incReserve - 1), carol);
            env.close();

            auto const subId =
                getSubscriptionIndex(alice, carol, env.seq(alice));
            env(subscription::create(alice, carol, USD(10), 100s));
            env.close();

            env(subscription::claim(carol, subId, USD(10)),
                ter(tecNO_LINE_INSUF_RESERVE));
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

            ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
            BEAST_EXPECT(
                std::distance(aod.begin(), aod.end()) ==
                2);  // trustline + subscription
            BEAST_EXPECT(std::find(aod.begin(), aod.end(), sub1) != aod.end());

            ripple::Dir bod(*env.current(), keylet::ownerDir(bob.id()));
            BEAST_EXPECT(
                std::distance(bod.begin(), bod.end()) ==
                2);  // trustline + subscription
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

        env(trust(gw, aliceUSD(10000)), txflags(tfSetfAuth));
        env(trust(alice, USD(10000)));
        env(trust(bob, USD(10000)));
        env.close();
        env(pay(gw, alice, USD(1000)));
        env.close();

        // Cannot create subscription without dest auth
        {
            env(subscription::create(alice, bob, USD(125), 100s),
                ter(tecNO_AUTH));
            env.close();
        }

        // Set auth on bob and retry
        {
            env(trust(gw, bobUSD(10000)), txflags(tfSetfAuth));
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
            env(subscription::create(alice, bob, USD(125), 100s),
                ter(tecFROZEN));
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
            env(subscription::claim(bob, subId, USD(125)), ter(tecFROZEN));
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
            env(subscription::create(alice, bob, USD(125), 100s),
                ter(tecFROZEN));
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
            env(subscription::claim(bob, subId, USD(125)), ter(tecFROZEN));
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
            // This would require precision loss
            env(subscription::create(alice, bob, USD(1), 100s),
                ter(tecPRECISION_LOSS));
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
            auto const amend =
                withSubscription ? features : features - featureSubscription;
            Env env{*this, amend};

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            env.fund(XRP(5000), bob);

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10000)));
            env.close();

            auto const createResult =
                withSubscription ? ter(tesSUCCESS) : ter(temDISABLED);

            auto const seq1 = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, seq1);
            env(subscription::create(alice, bob, MPT(1000), 100s),
                createResult);
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
        mptGw.create(
            {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
        mptGw.authorize({.account = alice});
        mptGw.authorize({.account = bob});
        auto const MPT = mptGw["MPT"];

        // temBAD_AMOUNT: negative MPT
        {
            env(subscription::create(alice, bob, MPT(-1), 100s),
                ter(temBAD_AMOUNT));
            env.close();
        }

        // temBAD_AMOUNT: zero MPT
        {
            env(subscription::create(alice, bob, MPT(0), 100s),
                ter(temBAD_AMOUNT));
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

            auto const mpt = ripple::test::jtx::MPT(
                alice.name(), makeMptID(env.seq(alice), alice));
            Json::Value jv = subscription::create(alice, bob, mpt(10), 100s);
            jv[jss::Amount][jss::mpt_issuance_id] =
                "00000004A407AF5856CCF3C42619DAA925813FC955C72983";
            env(jv, ter(tecOBJECT_NOT_FOUND));
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
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            auto const MPT = mptGw["MPT"];

            env(subscription::create(alice, bob, MPT(4), 100s),
                ter(tecOBJECT_NOT_FOUND));
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
                 .flags =
                     tfMPTCanEscrow | tfMPTCanTransfer | tfMPTRequireAuth});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = gw, .holder = alice});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env.close();

            // unauthorize account
            mptGw.authorize(
                {.account = gw, .holder = alice, .flags = tfMPTUnauthorize});

            env(subscription::create(alice, bob, MPT(5), 100s),
                ter(tecNO_AUTH));
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
                 .flags =
                     tfMPTCanEscrow | tfMPTCanTransfer | tfMPTRequireAuth});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = gw, .holder = alice});
            mptGw.authorize({.account = bob});
            mptGw.authorize({.account = gw, .holder = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env(pay(gw, bob, MPT(10'000)));
            env.close();

            // unauthorize dest
            mptGw.authorize(
                {.account = gw, .holder = bob, .flags = tfMPTUnauthorize});

            env(subscription::create(alice, bob, MPT(6), 100s),
                ter(tecNO_AUTH));
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

            env(subscription::create(alice, bob, MPT(7), 100s), ter(tecLOCKED));
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

            env(subscription::create(alice, bob, MPT(8), 100s), ter(tecLOCKED));
            env.close();
        }

        // tecNO_AUTH: mpt cannot be transferred
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanEscrow});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env(pay(gw, bob, MPT(10'000)));
            env.close();

            env(subscription::create(alice, bob, MPT(9), 100s),
                ter(tecNO_AUTH));
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
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, bob, MPT(10)));
            env.close();

            env(subscription::create(alice, bob, MPT(10), 100s),
                ter(tecINSUFFICIENT_FUNDS));
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
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10)));
            env(pay(gw, bob, MPT(10)));
            env.close();

            env(subscription::create(alice, bob, MPT(11), 100s),
                ter(tecINSUFFICIENT_FUNDS));
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
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanTransfer | tfMPTRequireAuth});
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
            mptGw.authorize(
                {.account = gw, .holder = bob, .flags = tfMPTUnauthorize});

            env(subscription::claim(bob, subId, MPT(10)), ter(tecNO_AUTH));
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
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanTransfer | tfMPTCanLock});
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

            env(subscription::claim(bob, subId, MPT(8)), ter(tecLOCKED));
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
            auto const reserve = env.current()->fees().accountReserve(0);
            auto const incReserve = env.current()->fees().increment;

            env.fund(reserve + (incReserve - 1), bob);
            env.close();

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10000)));
            env.close();

            auto const seq1 = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, seq1);
            env(subscription::create(alice, bob, MPT(10), 100s));
            env.close();

            env(subscription::claim(bob, subId, MPT(10)),
                ter(tecINSUFFICIENT_RESERVE));
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
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env.close();

            auto const seq1 = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, seq1);
            env(subscription::create(alice, bob, MPT(10), 100s));
            env.close();

            env(subscription::claim(bob, subId, MPT(10)), ter(tesSUCCESS));
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
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env.close();

            auto const seq1 = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, seq1);
            env(subscription::create(alice, bob, MPT(10), 100s));
            env.close();

            env(subscription::claim(carol, subId, MPT(10)),
                ter(tecNO_PERMISSION));
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
        mptGw.create(
            {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
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
        mptGw.create(
            {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
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

            ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
            BEAST_EXPECT(
                std::distance(aod.begin(), aod.end()) ==
                2);  // mptoken + subscription
            BEAST_EXPECT(std::find(aod.begin(), aod.end(), sub) != aod.end());

            ripple::Dir bod(*env.current(), keylet::ownerDir(bob.id()));
            BEAST_EXPECT(
                std::distance(bod.begin(), bod.end()) ==
                2);  // mptoken + subscription
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
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
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
            BEAST_EXPECT(env.balance(gw, MPT) == preOutstanding + MPT(1000));
        }

        // Issuer as destination
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
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
            BEAST_EXPECT(env.balance(gw, MPT) == preOutstanding - MPT(1000));
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
            {.ownerCount = 1,
             .holderCount = 0,
             .flags = tfMPTCanTransfer | tfMPTRequireAuth});
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
        mptGw.create(
            {.ownerCount = 1,
             .holderCount = 0,
             .flags = tfMPTCanTransfer | tfMPTCanLock});
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
            env(subscription::claim(bob, subId, MPT(100)), ter(tecLOCKED));
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
        mptGw.create(
            {.ownerCount = 1,
             .holderCount = 0,
             .flags = 0});  // No tfMPTCanTransfer
        mptGw.authorize({.account = alice});
        mptGw.authorize({.account = bob});
        auto const MPT = mptGw["MPT"];
        env(pay(gw, alice, MPT(10000)));
        env(pay(gw, bob, MPT(10000)));
        env.close();

        // Cannot create subscription to non-issuer without transfer
        {
            env(subscription::create(alice, bob, MPT(100), 100s),
                ter(tecNO_AUTH));
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
        auto const sa = testable_amendments();
        testWithFeats(sa);
    }
};

BEAST_DEFINE_TESTSUITE(Subscription, app, ripple);
}  // namespace test
}  // namespace ripple

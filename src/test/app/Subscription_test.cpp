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

        Env env{*this, features};

        env.fund(XRP(1000), alice, bob, gw);
        env.close();

        /*
        CREATE
        */

        // temMALFORMED: SetSubscription: SubscriptionID is not present, and
        // required fields are also present.
        {
            Json::Value txn;
            txn[jss::TransactionType] = jss::SubscriptionSet;
            txn[jss::Account] = alice.human();

            // no sfDestination
            env(txn, ter(temMALFORMED));
            env.close();

            // no sfAmount
            txn[sfDestination.jsonName] = bob.human();
            env(txn, ter(temMALFORMED));
            env.close();

            // no sfFrequency
            txn[sfDestination.jsonName] = bob.human();
            txn[sfAmount.jsonName] = XRP(10).value().getJson(JsonOptions::none);
            env(txn, ter(temMALFORMED));
            env.close();
        }

        // temDST_IS_SRC: SetSubscription: Malformed transaction: Account is the
        // same as the destination.
        {
            env(subscription::create(alice, alice, XRP(10), 100s),
                ter(temDST_IS_SRC));
            env.close();
        }

        /*
        UPDATE
        */

        // temMALFORMED: SetSubscription: SubscriptionID is present, but
        // optional fields are also present.
        {
            auto const aliceSeq = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, aliceSeq);

            Json::Value txn = subscription::update(alice, subId, XRP(10));

            // sfDestination
            txn[sfDestination.jsonName] = bob.human();
            env(txn, ter(temMALFORMED));
            env.close();

            // sfFrequency
            auto const frequency = 100s;
            txn[sfDestination.jsonName] = bob.human();
            txn[sfFrequency.jsonName] = to_string(frequency.count());
            env(txn, ter(temMALFORMED));
            env.close();

            // sfStartTime
            auto const startTime = env.now() + 0s;
            txn[sfDestination.jsonName] = bob.human();
            txn[sfFrequency.jsonName] = to_string(frequency.count());
            env(txn, subscription::start_time(startTime), ter(temMALFORMED));
            env.close();
        }

        /*
        BOTH
        */

        // temINVALID_FLAG:
        {
            env(subscription::create(alice, bob, XRP(10), 100s),
                txflags(tfSetfAuth),
                ter(temINVALID_FLAG));
            env.close();
        }

        // temBAD_AMOUNT: SetSubscription: Malformed transaction: bad amount:
        {
            env(subscription::create(alice, bob, XRP(-10), 100s),
                ter(temBAD_AMOUNT));
            env.close();
        }

        // temBAD_CURRENCY: SetSubscription: Malformed transaction: Bad
        // currency.
        {
            IOU const BAD{gw, badCurrency()};
            env(subscription::create(alice, bob, BAD(10), 100s),
                ter(temBAD_CURRENCY));
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

        // tecNO_DST:
        {
            env(subscription::create(alice, dne, XRP(10), 100s),
                ter(tecNO_DST));
            env.close();
        }

        // temMALFORMED: SetSubscription: The frequency is less than or equal to
        // 0.
        {
            env(subscription::create(alice, bob, XRP(10), 0s),
                ter(temMALFORMED));
            env.close();
        }

        // temMALFORMED: SetSubscription: The start time is in the past.
        {
            auto const start = env.now() - 10s;
            env(subscription::create(alice, bob, XRP(10), 100s),
                subscription::start_time(start),
                ter(temMALFORMED));
            env.close();
        }

        // temBAD_EXPIRATION: SetSubscription: The expiration time is in the
        // past.
        {
            auto const expire = env.now() - 10s;
            env(subscription::create(alice, bob, XRP(10), 100s, expire),
                ter(temBAD_EXPIRATION));
            env.close();
        }

        // temBAD_EXPIRATION: SetSubscription: The expiration time is less than
        // the next payment time.
        {
            auto const start = env.now() + 0s;
            auto const expire = env.now() - 10s;
            env(subscription::create(alice, bob, XRP(10), 100s, expire),
                subscription::start_time(start),
                ter(temBAD_EXPIRATION));
            env.close();
        }

        /*
        UPDATE
        */

        // tecNO_ENTRY: SetSubscription: Subscription does not exist.
        {
            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::update(alice, subId, XRP(100)), ter(tecNO_ENTRY));
            env.close();
        }

        // tecNO_PERMISSION: SetSubscription: Account is not the owner of the
        // subscription.
        {
            auto const subId = getSubscriptionIndex(alice, bob, env.seq(alice));
            env(subscription::create(alice, bob, XRP(100), 100s));
            env.close();

            env(subscription::update(bob, subId, XRP(100)),
                ter(tecNO_PERMISSION));
            env.close();
        }

        // temBAD_EXPIRATION: SetSubscription: The expiration time is in the
        // past.
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

        env.fund(XRP(1000), alice, bob);
        env.close();

        /*
        CREATE
        */

        // tecINSUFFICIENT_RESERVE

        /*
        UPDATE
        */
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
    }

    void
    testCancelDoApplyInvalid(FeatureBitset features)
    {
        testcase("cancel doApply invalid");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        // setup env
        auto const alice = Account("alice");
        auto const bob = Account("bob");

        Env env{*this, features};

        env.fund(XRP(1000), alice, bob);
        env.close();

        // tefBAD_LEDGER: TODO: Use Genesis Ledger
        // tefBAD_LEDGER: TODO: Use Genesis Ledger
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

        // temINVALID_FLAG
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

        // tecNO_TARGET
        // temBAD_AMOUNT: ClaimSubscription: The transaction amount is greater
        // than the subscription amount. tefFAILURE: ClaimSubscription: The
        // subscription has not reached the next payment time.
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

        // tecNO_PERMISSION
        {
            auto const aliceSeq = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, aliceSeq);

            env(subscription::create(alice, bob, XRP(10), 100s));
            env.close();

            env(subscription::claim(alice, subId, XRP(1)),
                ter(tecNO_PERMISSION));
            env.close();
        }

        // tecUNFUNDED_PAYMENT
        {
            auto const aliceSeq = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, aliceSeq);

            env(subscription::create(alice, bob, XRP(10000), 100s));
            env.close();

            env(subscription::claim(bob, subId, XRP(10000)),
                ter(tecUNFUNDED_PAYMENT));
            env.close();
        }

        // tecNO_LINE_INSUF_RESERVE
        // {
        //     auto const aliceSeq = env.seq(alice);
        //     auto const subId = getSubscriptionIndex(alice, bob, aliceSeq);

        //     env(subscription::create(alice, bob, XRP(10000), 100s));
        //     env.close();

        //     env(subscription::claim(bob, subId, XRP(1)),
        //     ter(tecNO_LINE_INSUF_RESERVE)); env.close();
        // }

        // tecPATH_PARTIAL
        {
            auto const aliceSeq = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, aliceSeq);

            env(subscription::create(alice, bob, USD(10000), 100s));
            env.close();

            env(subscription::claim(bob, subId, USD(10000)),
                ter(tecPATH_PARTIAL));
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
                subSle->getFieldU32(sfNextPaymentTime) ==
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
                subSle->getFieldU32(sfNextPaymentTime) ==
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

        // Claim XRP
        {
            // setup env
            auto const alice = Account("alice");
            auto const bob = Account("bob");

            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;

            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const aliceSeq = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, aliceSeq);
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            env(subscription::create(alice, bob, XRP(10), 100s));
            env.close();

            auto const [preKey, preSubSle] =
                subKeyAndSle(*env.current(), subId);

            env(subscription::claim(bob, subId, XRP(10)));
            env.close();

            BEAST_EXPECT(env.balance(alice) == preAlice - baseFee - XRP(10));
            BEAST_EXPECT(env.balance(bob) == preBob - baseFee + XRP(10));
            auto const [key, subSle] = subKeyAndSle(*env.current(), subId);
            BEAST_EXPECT(
                subSle->getFieldU32(sfNextPaymentTime) ==
                preSubSle->getFieldU32(sfNextPaymentTime) +
                    preSubSle->getFieldU32(sfFrequency));
        }

        // Claim IOU Has Trustline
        {
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

            auto const aliceSeq = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, aliceSeq);
            auto const preAlice = env.balance(alice, USD.issue());
            auto const preBob = env.balance(bob, USD.issue());

            env(subscription::create(alice, bob, USD(10), 100s));
            env.close();

            auto const [preKey, preSubSle] =
                subKeyAndSle(*env.current(), subId);

            env(subscription::claim(bob, subId, USD(10)));
            env.close();

            BEAST_EXPECT(env.balance(alice, USD.issue()) == preAlice - USD(10));
            BEAST_EXPECT(env.balance(bob, USD.issue()) == preBob + USD(10));
            auto const [key, subSle] = subKeyAndSle(*env.current(), subId);
            BEAST_EXPECT(
                subSle->getFieldU32(sfNextPaymentTime) ==
                preSubSle->getFieldU32(sfNextPaymentTime) +
                    preSubSle->getFieldU32(sfFrequency));
        }

        // Claim IOU No Trustline
        {
            // setup env
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];

            Env env{*this, features};

            env.fund(XRP(1000), alice, bob, gw);
            env.close();
            env(trust(alice, USD(10000)));
            env.close();
            env(pay(gw, alice, USD(1000)));
            env.close();

            auto const aliceSeq = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, aliceSeq);
            auto const preAlice = env.balance(alice, USD.issue());

            env(subscription::create(alice, bob, USD(10), 100s));
            env.close();

            auto const [preKey, preSubSle] =
                subKeyAndSle(*env.current(), subId);

            env(subscription::claim(bob, subId, USD(10)));
            env.close();

            BEAST_EXPECT(env.balance(alice, USD.issue()) == preAlice - USD(10));
            BEAST_EXPECT(env.balance(bob, USD.issue()) == USD(10));
            auto const [key, subSle] = subKeyAndSle(*env.current(), subId);
            BEAST_EXPECT(
                subSle->getFieldU32(sfNextPaymentTime) ==
                preSubSle->getFieldU32(sfNextPaymentTime) +
                    preSubSle->getFieldU32(sfFrequency));
        }

        // Claim Expire
        {
            // setup env
            auto const alice = Account("alice");
            auto const bob = Account("bob");

            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;

            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const aliceSeq = env.seq(alice);
            auto const subId = getSubscriptionIndex(alice, bob, aliceSeq);
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);

            auto const expire = env.now() + 10s;
            env(subscription::create(alice, bob, XRP(10), 100s, expire));
            env.close(10s);

            env(subscription::claim(bob, subId, XRP(10)));
            env.close();

            BEAST_EXPECT(!subscriptionExists(*env.current(), subId));
            BEAST_EXPECT(env.balance(alice) == preAlice - baseFee - XRP(10));
            BEAST_EXPECT(env.balance(bob) == preBob - baseFee + XRP(10));

            env(subscription::claim(bob, subId, XRP(10)), ter(tecNO_TARGET));
            env.close();
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

        // Claim / Cancel (Account)
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

        // Claim / Cancel (Destination)
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
    testWithFeats(FeatureBitset features)
    {
        testEnabled(features);
        testSetPreflightInvalid(features);
        testSetPreclaimInvalid(features);
        testSetDoApplyInvalid(features);
        testCancelPreflightInvalid(features);
        testCancelPreclaimInvalid(features);
        testCancelDoApplyInvalid(features);
        testClaimPreflightInvalid(features);
        testClaimPreclaimInvalid(features);
        testClaimDoApplyInvalid(features);
        testSet(features);
        testUpdate(features);
        testCancel(features);
        testClaim(features);
        testDstTag(features);
        // testDepositAuth(features);
        // testRippleState(features);
        // testGateway(features);
        // testRequireAuth(features);
        // testFreeze(features);
        testAccountDelete(features);
        testUsingTickets(features);

        // TODO: Should the create subscription transaction take the first
        // payment?
        // TODO: Should the next payment be the sfNextPaymentTime + sfFrequency?
        // Or should it be the current time + sfFrequency?
        // TODO: Is there any limitations on the update to Expire Time?
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        auto const sa = supported_amendments();
        testWithFeats(sa);
    }
};

BEAST_DEFINE_TESTSUITE(Subscription, app, ripple);
}  // namespace test
}  // namespace ripple

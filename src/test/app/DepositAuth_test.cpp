//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2017 Ripple Labs Inc.

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
#include <xrpl/protocol/Feature.h>

namespace ripple {
namespace test {

// Helper function that returns the reserve on an account based on
// the passed in number of owners.
static XRPAmount
reserve(jtx::Env& env, std::uint32_t count)
{
    return env.current()->fees().accountReserve(count);
}

// Helper function that returns true if acct has the lsfDepostAuth flag set.
static bool
hasDepositAuth(jtx::Env const& env, jtx::Account const& acct)
{
    return ((*env.le(acct))[sfFlags] & lsfDepositAuth) == lsfDepositAuth;
}

struct DepositAuth_test : public beast::unit_test::suite
{
    void
    testEnable()
    {
        testcase("Enable");

        using namespace jtx;
        Account const alice{"alice"};

        {
            // featureDepositAuth is disabled.
            Env env(*this, supported_amendments() - featureDepositAuth);
            env.fund(XRP(10000), alice);

            // Note that, to support old behavior, invalid flags are ignored.
            env(fset(alice, asfDepositAuth));
            env.close();
            BEAST_EXPECT(!hasDepositAuth(env, alice));

            env(fclear(alice, asfDepositAuth));
            env.close();
            BEAST_EXPECT(!hasDepositAuth(env, alice));
        }
        {
            // featureDepositAuth is enabled.
            Env env(*this);
            env.fund(XRP(10000), alice);

            env(fset(alice, asfDepositAuth));
            env.close();
            BEAST_EXPECT(hasDepositAuth(env, alice));

            env(fclear(alice, asfDepositAuth));
            env.close();
            BEAST_EXPECT(!hasDepositAuth(env, alice));
        }
    }

    void
    testPayIOU()
    {
        // Exercise IOU payments and non-direct XRP payments to an account
        // that has the lsfDepositAuth flag set.
        testcase("Pay IOU");

        using namespace jtx;
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const gw{"gw"};
        IOU const USD = gw["USD"];

        Env env(*this);

        env.fund(XRP(10000), alice, bob, carol, gw);
        env.trust(USD(1000), alice, bob);
        env.close();

        env(pay(gw, alice, USD(150)));
        env(offer(carol, USD(100), XRP(100)));
        env.close();

        // Make sure bob's trust line is all set up so he can receive USD.
        env(pay(alice, bob, USD(50)));
        env.close();

        // bob sets the lsfDepositAuth flag.
        env(fset(bob, asfDepositAuth), require(flags(bob, asfDepositAuth)));
        env.close();

        // None of the following payments should succeed.
        auto failedIouPayments = [this, &env, &alice, &bob, &USD]() {
            env.require(flags(bob, asfDepositAuth));

            // Capture bob's balances before hand to confirm they don't change.
            PrettyAmount const bobXrpBalance{env.balance(bob, XRP)};
            PrettyAmount const bobUsdBalance{env.balance(bob, USD)};

            env(pay(alice, bob, USD(50)), ter(tecNO_PERMISSION));
            env.close();

            // Note that even though alice is paying bob in XRP, the payment
            // is still not allowed since the payment passes through an offer.
            env(pay(alice, bob, drops(1)),
                sendmax(USD(1)),
                ter(tecNO_PERMISSION));
            env.close();

            BEAST_EXPECT(bobXrpBalance == env.balance(bob, XRP));
            BEAST_EXPECT(bobUsdBalance == env.balance(bob, USD));
        };

        //  Test when bob has an XRP balance > base reserve.
        failedIouPayments();

        // Set bob's XRP balance == base reserve.  Also demonstrate that
        // bob can make payments while his lsfDepositAuth flag is set.
        env(pay(bob, alice, USD(25)));
        env.close();

        {
            STAmount const bobPaysXRP{env.balance(bob, XRP) - reserve(env, 1)};
            XRPAmount const bobPaysFee{reserve(env, 1) - reserve(env, 0)};
            env(pay(bob, alice, bobPaysXRP), fee(bobPaysFee));
            env.close();
        }

        // Test when bob's XRP balance == base reserve.
        BEAST_EXPECT(env.balance(bob, XRP) == reserve(env, 0));
        BEAST_EXPECT(env.balance(bob, USD) == USD(25));
        failedIouPayments();

        // Test when bob has an XRP balance == 0.
        env(noop(bob), fee(reserve(env, 0)));
        env.close();

        BEAST_EXPECT(env.balance(bob, XRP) == XRP(0));
        failedIouPayments();

        // Give bob enough XRP for the fee to clear the lsfDepositAuth flag.
        env(pay(alice, bob, drops(env.current()->fees().base)));

        // bob clears the lsfDepositAuth and the next payment succeeds.
        env(fclear(bob, asfDepositAuth));
        env.close();

        env(pay(alice, bob, USD(50)));
        env.close();

        env(pay(alice, bob, drops(1)), sendmax(USD(1)));
        env.close();
    }

    void
    testPayXRP()
    {
        // Exercise direct XRP payments to an account that has the
        // lsfDepositAuth flag set.
        testcase("Pay XRP");

        using namespace jtx;
        Account const alice{"alice"};
        Account const bob{"bob"};

        Env env(*this);

        env.fund(XRP(10000), alice, bob);

        // bob sets the lsfDepositAuth flag.
        env(fset(bob, asfDepositAuth), fee(drops(10)));
        env.close();
        BEAST_EXPECT(env.balance(bob, XRP) == XRP(10000) - drops(10));

        // bob has more XRP than the base reserve.  Any XRP payment should fail.
        env(pay(alice, bob, drops(1)), ter(tecNO_PERMISSION));
        env.close();
        BEAST_EXPECT(env.balance(bob, XRP) == XRP(10000) - drops(10));

        // Change bob's XRP balance to exactly the base reserve.
        {
            STAmount const bobPaysXRP{env.balance(bob, XRP) - reserve(env, 1)};
            XRPAmount const bobPaysFee{reserve(env, 1) - reserve(env, 0)};
            env(pay(bob, alice, bobPaysXRP), fee(bobPaysFee));
            env.close();
        }

        // bob has exactly the base reserve.  A small enough direct XRP
        // payment should succeed.
        BEAST_EXPECT(env.balance(bob, XRP) == reserve(env, 0));
        env(pay(alice, bob, drops(1)));
        env.close();

        // bob has exactly the base reserve + 1.  No payment should succeed.
        BEAST_EXPECT(env.balance(bob, XRP) == reserve(env, 0) + drops(1));
        env(pay(alice, bob, drops(1)), ter(tecNO_PERMISSION));
        env.close();

        // Take bob down to a balance of 0 XRP.
        env(noop(bob), fee(reserve(env, 0) + drops(1)));
        env.close();
        BEAST_EXPECT(env.balance(bob, XRP) == drops(0));

        // We should not be able to pay bob more than the base reserve.
        env(pay(alice, bob, reserve(env, 0) + drops(1)), ter(tecNO_PERMISSION));
        env.close();

        // However a payment of exactly the base reserve should succeed.
        env(pay(alice, bob, reserve(env, 0) + drops(0)));
        env.close();
        BEAST_EXPECT(env.balance(bob, XRP) == reserve(env, 0));

        // We should be able to pay bob the base reserve one more time.
        env(pay(alice, bob, reserve(env, 0) + drops(0)));
        env.close();
        BEAST_EXPECT(
            env.balance(bob, XRP) == (reserve(env, 0) + reserve(env, 0)));

        // bob's above the threshold again.  Any payment should fail.
        env(pay(alice, bob, drops(1)), ter(tecNO_PERMISSION));
        env.close();
        BEAST_EXPECT(
            env.balance(bob, XRP) == (reserve(env, 0) + reserve(env, 0)));

        // Take bob back down to a zero XRP balance.
        env(noop(bob), fee(env.balance(bob, XRP)));
        env.close();
        BEAST_EXPECT(env.balance(bob, XRP) == drops(0));

        // bob should not be able to clear lsfDepositAuth.
        env(fclear(bob, asfDepositAuth), ter(terINSUF_FEE_B));
        env.close();

        // We should be able to pay bob 1 drop now.
        env(pay(alice, bob, drops(1)));
        env.close();
        BEAST_EXPECT(env.balance(bob, XRP) == drops(1));

        // Pay bob enough so he can afford the fee to clear lsfDepositAuth.
        env(pay(alice, bob, drops(9)));
        env.close();

        // Interestingly, at this point the terINSUF_FEE_B retry grabs the
        // request to clear lsfDepositAuth.  So the balance should be zero
        // and lsfDepositAuth should be cleared.
        BEAST_EXPECT(env.balance(bob, XRP) == drops(0));
        env.require(nflags(bob, asfDepositAuth));

        // Since bob no longer has lsfDepositAuth set we should be able to
        // pay him more than the base reserve.
        env(pay(alice, bob, reserve(env, 0) + drops(1)));
        env.close();
        BEAST_EXPECT(env.balance(bob, XRP) == reserve(env, 0) + drops(1));
    }

    void
    testNoRipple()
    {
        // It its current incarnation the DepositAuth flag does not change
        // any behaviors regarding rippling and the NoRipple flag.
        // Demonstrate that.
        testcase("No Ripple");

        using namespace jtx;
        Account const gw1("gw1");
        Account const gw2("gw2");
        Account const alice("alice");
        Account const bob("bob");

        IOU const USD1(gw1["USD"]);
        IOU const USD2(gw2["USD"]);

        auto testIssuer = [&](FeatureBitset const& features,
                              bool noRipplePrev,
                              bool noRippleNext,
                              bool withDepositAuth) {
            assert(!withDepositAuth || features[featureDepositAuth]);

            Env env(*this, features);

            env.fund(XRP(10000), gw1, alice, bob);
            env(trust(gw1, alice["USD"](10), noRipplePrev ? tfSetNoRipple : 0));
            env(trust(gw1, bob["USD"](10), noRippleNext ? tfSetNoRipple : 0));
            env.trust(USD1(10), alice, bob);

            env(pay(gw1, alice, USD1(10)));

            if (withDepositAuth)
                env(fset(gw1, asfDepositAuth));

            TER const result = (noRippleNext && noRipplePrev) ? TER{tecPATH_DRY}
                                                              : TER{tesSUCCESS};
            env(pay(alice, bob, USD1(10)), path(gw1), ter(result));
        };

        auto testNonIssuer = [&](FeatureBitset const& features,
                                 bool noRipplePrev,
                                 bool noRippleNext,
                                 bool withDepositAuth) {
            assert(!withDepositAuth || features[featureDepositAuth]);

            Env env(*this, features);

            env.fund(XRP(10000), gw1, gw2, alice);
            env(trust(alice, USD1(10), noRipplePrev ? tfSetNoRipple : 0));
            env(trust(alice, USD2(10), noRippleNext ? tfSetNoRipple : 0));
            env(pay(gw2, alice, USD2(10)));

            if (withDepositAuth)
                env(fset(alice, asfDepositAuth));

            TER const result = (noRippleNext && noRipplePrev) ? TER{tecPATH_DRY}
                                                              : TER{tesSUCCESS};
            env(pay(gw1, gw2, USD2(10)),
                path(alice),
                sendmax(USD1(10)),
                ter(result));
        };

        // Test every combo of noRipplePrev, noRippleNext, and withDepositAuth
        for (int i = 0; i < 8; ++i)
        {
            auto const noRipplePrev = i & 0x1;
            auto const noRippleNext = i & 0x2;
            auto const withDepositAuth = i & 0x4;
            testIssuer(
                supported_amendments() | featureDepositAuth,
                noRipplePrev,
                noRippleNext,
                withDepositAuth);

            if (!withDepositAuth)
                testIssuer(
                    supported_amendments() - featureDepositAuth,
                    noRipplePrev,
                    noRippleNext,
                    withDepositAuth);

            testNonIssuer(
                supported_amendments() | featureDepositAuth,
                noRipplePrev,
                noRippleNext,
                withDepositAuth);

            if (!withDepositAuth)
                testNonIssuer(
                    supported_amendments() - featureDepositAuth,
                    noRipplePrev,
                    noRippleNext,
                    withDepositAuth);
        }
    }

    void
    run() override
    {
        testEnable();
        testPayIOU();
        testPayXRP();
        testNoRipple();
    }
};

static Json::Value
ledgerEntryDepositPreauth(
    jtx::Env& env,
    jtx::Account const& acc,
    std::vector<jtx::deposit::AuthorizeCredentials> const& auth)
{
    Json::Value jvParams;
    jvParams[jss::ledger_index] = jss::validated;
    jvParams[jss::deposit_preauth][jss::owner] = acc.human();
    auto& arr(
        jvParams[jss::deposit_preauth][jss::authorize_credentials] =
            Json::arrayValue);
    for (auto const& o : auth)
    {
        arr.append(o.toLEJson());
    }
    return env.rpc("json", "ledger_entry", to_string(jvParams));
}

struct DepositPreauth_test : public beast::unit_test::suite
{
    void
    testEnable()
    {
        testcase("Enable");

        using namespace jtx;
        Account const alice{"alice"};
        Account const becky{"becky"};
        {
            // featureDepositPreauth is disabled.
            Env env(*this, supported_amendments() - featureDepositPreauth);
            env.fund(XRP(10000), alice, becky);
            env.close();

            // Should not be able to add a DepositPreauth to alice.
            env(deposit::auth(alice, becky), ter(temDISABLED));
            env.close();
            env.require(owners(alice, 0));
            env.require(owners(becky, 0));

            // Should not be able to remove a DepositPreauth from alice.
            env(deposit::unauth(alice, becky), ter(temDISABLED));
            env.close();
            env.require(owners(alice, 0));
            env.require(owners(becky, 0));
        }
        {
            // featureDepositPreauth is enabled.  The valid case is really
            // simple:
            //  o We should be able to add and remove an entry, and
            //  o That entry should cost one reserve.
            //  o The reserve should be returned when the entry is removed.
            Env env(*this);
            env.fund(XRP(10000), alice, becky);
            env.close();

            // Add a DepositPreauth to alice.
            env(deposit::auth(alice, becky));
            env.close();
            env.require(owners(alice, 1));
            env.require(owners(becky, 0));

            // Remove a DepositPreauth from alice.
            env(deposit::unauth(alice, becky));
            env.close();
            env.require(owners(alice, 0));
            env.require(owners(becky, 0));
        }
        {
            // Verify that an account can be preauthorized and unauthorized
            // using tickets.
            Env env(*this);
            env.fund(XRP(10000), alice, becky);
            env.close();

            env(ticket::create(alice, 2));
            std::uint32_t const aliceSeq{env.seq(alice)};
            env.close();
            env.require(tickets(alice, 2));

            // Consume the tickets from biggest seq to smallest 'cuz we can.
            std::uint32_t aliceTicketSeq{env.seq(alice)};

            // Add a DepositPreauth to alice.
            env(deposit::auth(alice, becky), ticket::use(--aliceTicketSeq));
            env.close();
            // Alice uses a ticket but gains a preauth entry.
            env.require(tickets(alice, 1));
            env.require(owners(alice, 2));
            BEAST_EXPECT(env.seq(alice) == aliceSeq);
            env.require(owners(becky, 0));

            // Remove a DepositPreauth from alice.
            env(deposit::unauth(alice, becky), ticket::use(--aliceTicketSeq));
            env.close();
            env.require(tickets(alice, 0));
            env.require(owners(alice, 0));
            BEAST_EXPECT(env.seq(alice) == aliceSeq);
            env.require(owners(becky, 0));
        }
    }

    void
    testInvalid()
    {
        testcase("Invalid");

        using namespace jtx;
        Account const alice{"alice"};
        Account const becky{"becky"};
        Account const carol{"carol"};

        Env env(*this);

        // Tell env about alice, becky and carol since they are not yet funded.
        env.memoize(alice);
        env.memoize(becky);
        env.memoize(carol);

        // Add DepositPreauth to an unfunded account.
        env(deposit::auth(alice, becky), seq(1), ter(terNO_ACCOUNT));

        env.fund(XRP(10000), alice, becky);
        env.close();

        // Bad fee.
        env(deposit::auth(alice, becky), fee(drops(-10)), ter(temBAD_FEE));
        env.close();

        // Bad flags.
        env(deposit::auth(alice, becky), txflags(tfSell), ter(temINVALID_FLAG));
        env.close();

        {
            // Neither auth not unauth.
            Json::Value tx{deposit::auth(alice, becky)};
            tx.removeMember(sfAuthorize.jsonName);
            env(tx, ter(temMALFORMED));
            env.close();
        }
        {
            // Both auth and unauth.
            Json::Value tx{deposit::auth(alice, becky)};
            tx[sfUnauthorize.jsonName] = becky.human();
            env(tx, ter(temMALFORMED));
            env.close();
        }
        {
            // Alice authorizes a zero account.
            Json::Value tx{deposit::auth(alice, becky)};
            tx[sfAuthorize.jsonName] = to_string(xrpAccount());
            env(tx, ter(temINVALID_ACCOUNT_ID));
            env.close();
        }

        // alice authorizes herself.
        env(deposit::auth(alice, alice), ter(temCANNOT_PREAUTH_SELF));
        env.close();

        // alice authorizes an unfunded account.
        env(deposit::auth(alice, carol), ter(tecNO_TARGET));
        env.close();

        // alice successfully authorizes becky.
        env.require(owners(alice, 0));
        env.require(owners(becky, 0));
        env(deposit::auth(alice, becky));
        env.close();
        env.require(owners(alice, 1));
        env.require(owners(becky, 0));

        // alice attempts to create a duplicate authorization.
        env(deposit::auth(alice, becky), ter(tecDUPLICATE));
        env.close();
        env.require(owners(alice, 1));
        env.require(owners(becky, 0));

        // carol attempts to preauthorize but doesn't have enough reserve.
        env.fund(drops(249'999'999), carol);
        env.close();

        env(deposit::auth(carol, becky), ter(tecINSUFFICIENT_RESERVE));
        env.close();
        env.require(owners(carol, 0));
        env.require(owners(becky, 0));

        // carol gets enough XRP to (barely) meet the reserve.
        env(pay(alice, carol, drops(11)));
        env.close();
        env(deposit::auth(carol, becky));
        env.close();
        env.require(owners(carol, 1));
        env.require(owners(becky, 0));

        // But carol can't meet the reserve for another preauthorization.
        env(deposit::auth(carol, alice), ter(tecINSUFFICIENT_RESERVE));
        env.close();
        env.require(owners(carol, 1));
        env.require(owners(becky, 0));
        env.require(owners(alice, 1));

        // alice attempts to remove an authorization she doesn't have.
        env(deposit::unauth(alice, carol), ter(tecNO_ENTRY));
        env.close();
        env.require(owners(alice, 1));
        env.require(owners(becky, 0));

        // alice successfully removes her authorization of becky.
        env(deposit::unauth(alice, becky));
        env.close();
        env.require(owners(alice, 0));
        env.require(owners(becky, 0));

        // alice removes becky again and gets an error.
        env(deposit::unauth(alice, becky), ter(tecNO_ENTRY));
        env.close();
        env.require(owners(alice, 0));
        env.require(owners(becky, 0));
    }

    void
    testPayment(FeatureBitset features)
    {
        testcase("Payment");

        using namespace jtx;
        Account const alice{"alice"};
        Account const becky{"becky"};
        Account const gw{"gw"};
        IOU const USD(gw["USD"]);

        bool const supportsPreauth = {features[featureDepositPreauth]};

        {
            // The initial implementation of DepositAuth had a bug where an
            // account with the DepositAuth flag set could not make a payment
            // to itself.  That bug was fixed in the DepositPreauth amendment.
            Env env(*this, features);
            env.fund(XRP(5000), alice, becky, gw);
            env.close();

            env.trust(USD(1000), alice);
            env.trust(USD(1000), becky);
            env.close();

            env(pay(gw, alice, USD(500)));
            env.close();

            env(offer(alice, XRP(100), USD(100), tfPassive),
                require(offers(alice, 1)));
            env.close();

            // becky pays herself USD (10) by consuming part of alice's offer.
            // Make sure the payment works if PaymentAuth is not involved.
            env(pay(becky, becky, USD(10)), path(~USD), sendmax(XRP(10)));
            env.close();

            // becky decides to require authorization for deposits.
            env(fset(becky, asfDepositAuth));
            env.close();

            // becky pays herself again.  Whether it succeeds depends on
            // whether featureDepositPreauth is enabled.
            TER const expect{
                supportsPreauth ? TER{tesSUCCESS} : TER{tecNO_PERMISSION}};

            env(pay(becky, becky, USD(10)),
                path(~USD),
                sendmax(XRP(10)),
                ter(expect));
            env.close();

            {
                // becky setup depositpreauth with credentials

                const char credType[] = "abcde";
                Account const carol{"carol"};

                env.fund(XRP(5000), carol);

                if (supportsPreauth)
                {
                    env(deposit::auth(
                        becky,
                        std::vector<deposit::AuthorizeCredentials>{
                            {carol, credType}}));
                    env.close();
                }

                // gw accept credentials
                auto jv = credentials::create(gw, carol, credType);
                env(jv);
                env.close();
                env(credentials::accept(gw, carol, credType));
                env.close();

                auto const jCred = credentials::ledgerEntryCredential(
                    env, gw, carol, credType);
                std::string const credIdx =
                    jCred[jss::result][jss::index].asString();

                TER const expect{
                    supportsPreauth ? TER{tesSUCCESS} : TER{temDISABLED}};
                env(pay(gw, becky, USD(100), {credIdx}), ter(expect));
                env.close();
            }

            {
                using namespace std::chrono;

                if (!supportsPreauth)
                {
                    auto const seq1 = env.seq(alice);
                    env(escrow(alice, becky, XRP(100)),
                        finish_time(env.now() + 1s));
                    env.close();

                    // Failed as rule is disabled
                    env(finish(gw, alice, seq1),
                        fee(1500),
                        ter(tecNO_PERMISSION));
                    env.close();
                }
            }
        }

        if (supportsPreauth)
        {
            // Make sure DepositPreauthorization works for payments.

            Account const carol{"carol"};

            Env env(*this, features);
            env.fund(XRP(5000), alice, becky, carol, gw);
            env.close();

            env.trust(USD(1000), alice);
            env.trust(USD(1000), becky);
            env.trust(USD(1000), carol);
            env.close();

            env(pay(gw, alice, USD(1000)));
            env.close();

            // Make XRP and IOU payments from alice to becky.  Should be fine.
            env(pay(alice, becky, XRP(100)));
            env(pay(alice, becky, USD(100)));
            env.close();

            // becky decides to require authorization for deposits.
            env(fset(becky, asfDepositAuth));
            env.close();

            // alice can no longer pay becky.
            env(pay(alice, becky, XRP(100)), ter(tecNO_PERMISSION));
            env(pay(alice, becky, USD(100)), ter(tecNO_PERMISSION));
            env.close();

            // becky preauthorizes carol for deposit, which doesn't provide
            // authorization for alice.
            env(deposit::auth(becky, carol));
            env.close();

            // alice still can't pay becky.
            env(pay(alice, becky, XRP(100)), ter(tecNO_PERMISSION));
            env(pay(alice, becky, USD(100)), ter(tecNO_PERMISSION));
            env.close();

            // becky preauthorizes alice for deposit.
            env(deposit::auth(becky, alice));
            env.close();

            // alice can now pay becky.
            env(pay(alice, becky, XRP(100)));
            env(pay(alice, becky, USD(100)));
            env.close();

            // alice decides to require authorization for deposits.
            env(fset(alice, asfDepositAuth));
            env.close();

            // Even though alice is authorized to pay becky, becky is not
            // authorized to pay alice.
            env(pay(becky, alice, XRP(100)), ter(tecNO_PERMISSION));
            env(pay(becky, alice, USD(100)), ter(tecNO_PERMISSION));
            env.close();

            // becky unauthorizes carol.  Should have no impact on alice.
            env(deposit::unauth(becky, carol));
            env.close();

            env(pay(alice, becky, XRP(100)));
            env(pay(alice, becky, USD(100)));
            env.close();

            // becky unauthorizes alice.  alice now can't pay becky.
            env(deposit::unauth(becky, alice));
            env.close();

            env(pay(alice, becky, XRP(100)), ter(tecNO_PERMISSION));
            env(pay(alice, becky, USD(100)), ter(tecNO_PERMISSION));
            env.close();

            // becky decides to remove authorization for deposits.  Now
            // alice can pay becky again.
            env(fclear(becky, asfDepositAuth));
            env.close();

            env(pay(alice, becky, XRP(100)));
            env(pay(alice, becky, USD(100)));
            env.close();
        }
    }

    void
    testCredentials()
    {
        using namespace jtx;

        const char credType[] = "abcde";

        {
            testcase("Payment with credentials.");

            Env env(*this);
            Account const iss{"issuer"};
            Account const alice{"alice"};
            Account const bob{"bob"};

            env.fund(XRP(5000), iss, alice, bob);
            env.close();

            // Issuer create credentials, but Alice didn't accept them yet
            env(credentials::create(alice, iss, credType));
            env.close();

            // Get the index of the credentials
            auto const jCred =
                credentials::ledgerEntryCredential(env, alice, iss, credType);
            std::string const credIdx =
                jCred[jss::result][jss::index].asString();

            // Bob require preauthorization
            env(fset(bob, asfDepositAuth), fee(drops(10)));
            env.close();

            // Bob will accept payements from accounts with credentials signed
            // by 'iss'
            env(deposit::auth(
                bob,
                std::vector<deposit::AuthorizeCredentials>{{iss, credType}}));
            env.close();

            auto const jDP =
                ledgerEntryDepositPreauth(env, bob, {{iss, credType}});
            BEAST_EXPECT(
                jDP.isObject() && jDP.isMember(jss::result) &&
                !jDP[jss::result].isMember(jss::error) &&
                jDP[jss::result].isMember(jss::node) &&
                jDP[jss::result][jss::node].isMember("LedgerEntryType") &&
                jDP[jss::result][jss::node]["LedgerEntryType"] ==
                    jss::DepositPreauth);

            // Alice can't pay - empty credentials array
            {
                auto jv = pay(alice, bob, XRP(100));
                jv[sfCredentialIDs.jsonName] = Json::arrayValue;
                env(jv, ter(temMALFORMED));
                env.close();
            }

            // Alice can't pay - not accepeted credentials
            env(pay(alice, bob, XRP(100), {credIdx}), ter(temBAD_CREDENTIALS));
            env.close();

            // Alice accept the credentials
            env(credentials::accept(alice, iss, credType));
            env.close();

            // Now Alice can pay
            env(pay(alice, bob, XRP(100), {credIdx}));
            env.close();

            {
                testcase("Escrow with credentials.");
                using namespace std::chrono;
                auto const seq1 = env.seq(alice);
                env(escrow(alice, bob, XRP(1000)), finish_time(env.now() + 1s));
                env.close();

                Account const john("john");

                env.fund(XRP(5000), john);
                env.close();

                {
                    // Don't use credentials for yourself
                    env(finish(bob, alice, seq1, {credIdx}),
                        fee(1500),
                        ter(tecNO_PERMISSION));
                    env.close();
                }
                {
                    env(credentials::create(john, iss, credType));
                    env.close();
                    env(credentials::accept(john, iss, credType));
                    env.close();
                    auto const jCred = credentials::ledgerEntryCredential(
                        env, john, iss, credType);
                    std::string const credIdx =
                        jCred[jss::result][jss::index].asString();

                    // john is pre-authorized and can finish escrow for bob
                    env(finish(john, alice, seq1, {credIdx}), fee(1500));
                    env.close();
                }
            }
        }

        {
            testcase("Creating / deleting with credentials.");

            Env env(*this);
            Account const iss{"issuer"};
            Account const alice{"alice"};
            Account const bob{"bob"};

            env.fund(XRP(5000), iss, alice, bob);
            env.close();

            {
                // both included [AuthorizeCredentials UnauthorizeCredentials]
                auto jv = deposit::auth(
                    bob,
                    std::vector<deposit::AuthorizeCredentials>{
                        {iss, credType}});
                jv[sfUnauthorizeCredentials.jsonName] = Json::arrayValue;
                env(jv, ter(temMALFORMED));
            }

            {
                // both included [Unauthorize, AuthorizeCredentials]
                auto jv = deposit::auth(
                    bob,
                    std::vector<deposit::AuthorizeCredentials>{
                        {iss, credType}});
                jv[sfUnauthorize.jsonName] = iss.human();
                env(jv, ter(temMALFORMED));
            }

            {
                // both included [Authorize, AuthorizeCredentials]
                auto jv = deposit::auth(
                    bob,
                    std::vector<deposit::AuthorizeCredentials>{
                        {iss, credType}});
                jv[sfAuthorize.jsonName] = iss.human();
                env(jv, ter(temMALFORMED));
            }

            {
                // AuthorizeCredentials is empty
                auto jv = deposit::auth(
                    bob, std::vector<deposit::AuthorizeCredentials>{});
                env(jv, ter(temMALFORMED));
            }

            {
                // invalid account
                auto jv = deposit::auth(
                    bob, std::vector<deposit::AuthorizeCredentials>{});
                auto& arr(jv[sfAuthorizeCredentials.jsonName]);
                Json::Value jcred = Json::objectValue;
                jcred[jss::Issuer] = to_string(xrpAccount());
                jcred[sfCredentialType.jsonName] =
                    strHex(std::string_view(credType));
                Json::Value j2;
                j2[jss::Credential] = jcred;
                arr.append(std::move(j2));

                env(jv, ter(temINVALID_ACCOUNT_ID));
            }

            {
                // try preauth itself
                auto jv = deposit::auth(
                    bob,
                    std::vector<deposit::AuthorizeCredentials>{
                        {bob, credType}});
                env(jv, ter(temCANNOT_PREAUTH_SELF));
            }

            {
                // empty credential type
                auto jv = deposit::auth(
                    bob, std::vector<deposit::AuthorizeCredentials>{{iss, {}}});
                env(jv, ter(temMALFORMED));
            }

            {
                // AuthorizeCredentials is larger than 8 elements
                Account const a("a"), b("b"), c("c"), d("d"), e("e"), f("f"),
                    g("g"), h("h"), i("i");
                auto const& z = credType;
                auto jv = deposit::auth(
                    bob,
                    std::vector<deposit::AuthorizeCredentials>{
                        {a, z},
                        {b, z},
                        {c, z},
                        {d, z},
                        {e, z},
                        {f, z},
                        {g, z},
                        {h, z},
                        {i, z}});
                env(jv, ter(temMALFORMED));
            }

            {
                // Can create with non-existing issuer
                Account const rick{"rick"};
                auto jv = deposit::auth(
                    bob,
                    std::vector<deposit::AuthorizeCredentials>{
                        {rick, credType}});
                env(jv);
                env.close();
            }

            {
                // not enough resevre
                Account const john{"john"};
                env.fund(env.current()->fees().accountReserve(0), john);
                auto jv = deposit::auth(
                    john,
                    std::vector<deposit::AuthorizeCredentials>{
                        {iss, credType}});
                env(jv, ter(tecINSUFFICIENT_RESERVE));
            }

            {
                // NO deposit object exists
                env(deposit::unauth(
                        bob,
                        std::vector<deposit::AuthorizeCredentials>{
                            {iss, credType}}),
                    ter(tecNO_ENTRY));
            }

            // Create DepositPreauth object
            {
                env(deposit::auth(
                    bob,
                    std::vector<deposit::AuthorizeCredentials>{
                        {iss, credType}}));
                env.close();

                auto const jDP =
                    ledgerEntryDepositPreauth(env, bob, {{iss, credType}});
                BEAST_EXPECT(
                    jDP.isObject() && jDP.isMember(jss::result) &&
                    !jDP[jss::result].isMember(jss::error) &&
                    jDP[jss::result].isMember(jss::node) &&
                    jDP[jss::result][jss::node].isMember("LedgerEntryType") &&
                    jDP[jss::result][jss::node]["LedgerEntryType"] ==
                        jss::DepositPreauth);

                // can't create duplicate
                env(deposit::auth(
                        bob,
                        std::vector<deposit::AuthorizeCredentials>{
                            {iss, credType}}),
                    ter(tecDUPLICATE));
            }

            // Delete DepositPreauth object
            {
                env(deposit::unauth(
                    bob,
                    std::vector<deposit::AuthorizeCredentials>{
                        {iss, credType}}));
                env.close();
                auto const jDP =
                    ledgerEntryDepositPreauth(env, bob, {{iss, credType}});
                BEAST_EXPECT(
                    jDP.isObject() && jDP.isMember(jss::result) &&
                    jDP[jss::result].isMember(jss::error));
            }
        }

        {
            testcase("Payment failed with invalid credentials.");

            Env env(*this);
            Account const iss{"issuer"};
            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const maria{"maria"};

            Account const gw{"gw"};
            IOU const USD = gw["USD"];

            env.fund(XRP(10000), iss, alice, bob, maria, gw);
            env.close();

            // Issuer create credentials, but Alice didn't accept them yet
            env(credentials::create(alice, iss, credType));
            env.close();
            // Alice accept the credentials
            env(credentials::accept(alice, iss, credType));
            env.close();
            // Get the index of the credentials
            auto const jCred =
                credentials::ledgerEntryCredential(env, alice, iss, credType);
            std::string const credIdx =
                jCred[jss::result][jss::index].asString();

            {
                // Fail as destination didn't enable preauthorization
                env(pay(alice, bob, XRP(100), {credIdx}),
                    ter(tecNO_PERMISSION));
            }

            // Bob require preauthorization
            env(fset(bob, asfDepositAuth), fee(drops(10)));
            env.close();

            {
                // Fail as destination didn't setup DepositPreauth object
                env(pay(alice, bob, XRP(100), {credIdx}),
                    ter(tecNO_PERMISSION));
            }

            // Bob setup DepositPreauth object, duplicates will be eliminated
            env(deposit::auth(
                bob,
                std::vector<deposit::AuthorizeCredentials>{
                    {iss, credType}, {iss, credType}}));
            env.close();

            {
                std::string const invalidIdx =
                    "0E0B04ED60588A758B67E21FBBE95AC5A63598BA951761DC0EC9C08D7E"
                    "01E034";
                // Alice can't pay with non-existing credentials
                env(pay(alice, bob, XRP(100), {invalidIdx}),
                    ter(temBAD_CREDENTIALS));
            }

            {  // maria can't pay using valid credentials but issued for
               // different account
                env(pay(maria, bob, XRP(100), {credIdx}),
                    ter(temBAD_CREDENTIALS));
            }

            {
                // create another valid credential
                const char credType2[] = "fghij";
                env(credentials::create(alice, iss, credType2));
                env.close();
                env(credentials::accept(alice, iss, credType2));
                env.close();
                auto const jCred2 = credentials::ledgerEntryCredential(
                    env, alice, iss, credType2);
                std::string const credIdx2 =
                    jCred2[jss::result][jss::index].asString();

                // Alice can't pay with invalid set of valid credentials
                env(pay(alice, bob, XRP(100), {credIdx, credIdx2}),
                    ter(tecNO_PERMISSION));
            }

            // Alice can pay, duplicate credentials will be eliminated
            env(pay(alice, bob, XRP(100), {credIdx, credIdx}));
            env.close();
            env(pay(alice, bob, XRP(100), {credIdx}));
            env.close();
        }

        {
            testcase("Payment failed with disabled rules.");

            Env env(*this, supported_amendments() - featureCredentials);
            Account const iss{"issuer"};
            Account const bob{"bob"};

            env.fund(XRP(5000), iss, bob);
            env.close();

            // Bob require preauthorization
            env(fset(bob, asfDepositAuth), fee(drops(10)));
            env.close();

            // Setup DepositPreauth object failed - amendent is not supported
            env(deposit::auth(
                    bob,
                    std::vector<deposit::AuthorizeCredentials>{
                        {iss, credType}}),
                ter(temDISABLED));
            env.close();

            {
                // Payment with CredentialsIDs failed - amendent is not
                // supported
                std::string const invalidIdx =
                    "0E0B04ED60588A758B67E21FBBE95AC5A63598BA951761DC0EC9C08D7E"
                    "01E034";
                env(pay(iss, bob, XRP(10), {invalidIdx}), ter(temDISABLED));
            }
        }
    }

    void
    testExpCreds()
    {
        using namespace jtx;
        const char credType[] = "abcde";

        {
            testcase("Payment failed with expired credentials.");

            Env env(*this);
            Account const iss{"issuer"};
            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const gw{"gw"};
            IOU const USD = gw["USD"];

            env.fund(XRP(10000), iss, alice, bob, gw);
            env.close();

            // Create credentials
            auto jv = credentials::create(alice, iss, credType);
            // Current time in ripple epoch.
            // Every time ledger close, unittest timer increase by 10s
            uint32_t const t = env.now().time_since_epoch().count() + 40;
            jv[sfExpiration.jsonName] = t;
            env(jv);
            env.close();

            // Alice accept the credentials
            env(credentials::accept(alice, iss, credType));
            env.close();

            // Get the index of the credentials
            auto const jCred =
                credentials::ledgerEntryCredential(env, alice, iss, credType);
            std::string const credIdx =
                jCred[jss::result][jss::index].asString();

            // Bob require preauthorization
            env(fset(bob, asfDepositAuth), fee(drops(10)));
            env.close();
            // Bob setup DepositPreauth object
            env(deposit::auth(
                bob,
                std::vector<deposit::AuthorizeCredentials>{{iss, credType}}));
            env.close();

            {
                // Alice can pay
                env(pay(alice, bob, XRP(100), {credIdx}));
                env.close();

                // Ledger closed, time increased, alice can't pay anymore
                env(pay(alice, bob, XRP(100), {credIdx}), ter(tecEXPIRED));
                env.close();

                // check that expired credentials were deleted
                auto const jDelCred = credentials::ledgerEntryCredential(
                    env, alice, iss, credType);
                BEAST_EXPECT(
                    jDelCred.isObject() && jDelCred.isMember(jss::result) &&
                    jDelCred[jss::result].isMember(jss::error));
            }

            {
                auto jv = credentials::create(gw, iss, credType);
                uint32_t const t = env.now().time_since_epoch().count() + 40;
                jv[sfExpiration.jsonName] = t;
                env(jv);
                env.close();
                env(credentials::accept(gw, iss, credType));
                env.close();

                auto const jCred =
                    credentials::ledgerEntryCredential(env, gw, iss, credType);
                std::string const credIdx =
                    jCred[jss::result][jss::index].asString();

                env.close();
                env.close();
                env.close();

                // credentails are expired
                env(pay(gw, bob, USD(150), {credIdx}), ter(tecEXPIRED));
                env.close();

                // check that expired credentials were deleted
                auto const jDelCred =
                    credentials::ledgerEntryCredential(env, gw, iss, credType);
                BEAST_EXPECT(
                    jDelCred.isObject() && jDelCred.isMember(jss::result) &&
                    jDelCred[jss::result].isMember(jss::error));
            }
        }

        {
            using namespace std::chrono;

            testcase("Escrow failed with expired credentials.");

            Env env(*this);
            Account const iss{"issuer"};
            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const zelda{"zelda"};

            env.fund(XRP(5000), iss, alice, bob, zelda);
            env.close();

            // Create credentials
            auto jv = credentials::create(zelda, iss, credType);
            uint32_t const t = env.now().time_since_epoch().count() + 50;
            jv[sfExpiration.jsonName] = t;
            env(jv);
            env.close();

            // Zelda accept the credentials
            env(credentials::accept(zelda, iss, credType));
            env.close();

            // Get the index of the credentials
            auto const jCred =
                credentials::ledgerEntryCredential(env, zelda, iss, credType);
            std::string const credIdx =
                jCred[jss::result][jss::index].asString();

            // Bob require preauthorization
            env(fset(bob, asfDepositAuth), fee(drops(10)));
            env.close();
            // Bob setup DepositPreauth object
            env(deposit::auth(
                bob,
                std::vector<deposit::AuthorizeCredentials>{{iss, credType}}));
            env.close();

            auto const seq1 = env.seq(alice);
            env(escrow(alice, bob, XRP(1000)), finish_time(env.now() + 1s));
            env.close();

            // zelda can't finish escrow with invalid credentials
            {
                auto jv = finish(zelda, alice, seq1, {});
                jv[sfCredentialIDs.jsonName] = Json::arrayValue;
                env(jv, fee(1500), ter(temMALFORMED));
                env.close();
            }

            {
                // zelda can't finish escrow with invalid credentials
                std::string const invalidIdx =
                    "0E0B04ED60588A758B67E21FBBE95AC5A63598BA951761DC0EC9C08D7E"
                    "01E034";
                auto jv = finish(zelda, alice, seq1, {invalidIdx});
                env(jv, fee(1500), ter(temBAD_CREDENTIALS));
                env.close();
            }

            {  // Ledger closed, time increased, zelda can't finish escrow
                env(finish(zelda, alice, seq1, {credIdx}),
                    fee(1500),
                    ter(tecEXPIRED));
                env.close();
            }

            // check that expired credentials were deleted
            auto const jDelCred =
                credentials::ledgerEntryCredential(env, zelda, iss, credType);
            BEAST_EXPECT(
                jDelCred.isObject() && jDelCred.isMember(jss::result) &&
                jDelCred[jss::result].isMember(jss::error));
        }
    }

    void
    run() override
    {
        testEnable();
        testInvalid();
        auto const supported{jtx::supported_amendments()};
        testPayment(supported - featureDepositPreauth);
        testPayment(supported);
        testCredentials();
        testExpCreds();
    }
};

BEAST_DEFINE_TESTSUITE(DepositAuth, app, ripple);
BEAST_DEFINE_TESTSUITE(DepositPreauth, app, ripple);

}  // namespace test
}  // namespace ripple

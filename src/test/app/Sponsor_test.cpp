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
#include <test/jtx/TestHelpers.h>
#include <test/jtx/check.h>
#include <test/jtx/did.h>
#include <test/jtx/sponsor.h>

#include <xrpl/protocol/Feature.h>

namespace ripple {

class Sponsor_test : public beast::unit_test::suite
{
public:
    void
    testDisabled()
    {
        testcase("Disabled");
        using namespace test::jtx;
        Env env{*this, testable_amendments() - featureSponsor};
        Account const alice("alice");
        Account const sponsor("sponsor");
        env.fund(XRP(10000), alice, sponsor);

        env(noop(alice),
            fee(XRP(1)),
            sponsor::as(sponsor),
            sponsor::sig(sponsor),
            ter(temDISABLED));
    }

    void
    testSponsorFee()
    {
        using namespace test::jtx;

        testcase("Sponsor Fee");
        Env env{*this, testable_amendments()};
        Account const alice("alice");
        Account const sponsor("sponsor");
        env.fund(XRP(10000), alice, sponsor);

        env(noop(alice),
            fee(XRP(1)),
            sponsor::as(sponsor, tfSponsorFee),
            sponsor::sig(sponsor));
        env.close();

        BEAST_EXPECT(env.balance(alice) == XRP(10000));
        BEAST_EXPECT(env.balance(sponsor) == XRP(9999));
    }

    void
    testSponsorAccount()
    {
        testcase("Sponsor Account");
        using namespace test::jtx;
        Env env{*this, testable_amendments()};

        Account const alice("alice");
        Account const sponsor("sponsor");
        env.fund(XRP(10000), alice, sponsor);

        Account const bob("bob");
        env(pay(alice, bob, STAmount(env.current()->fees().accountReserve(0))),
            sponsor::as(sponsor, tfSponsorReserve),
            sponsor::sig(sponsor));
        env.close();

        env.require(sponsored_owners(alice, 0));
        env.require(sponsoring_account_count(sponsor, 1));
    }

    void
    testCheck()
    {
        testcase("Check");
        using namespace test::jtx;
        Env env{*this, testable_amendments()};
        Account const alice("alice");
        Account const bob("bob");
        Account const sponsor("sponsor");

        env.fund(XRP(10000), alice, bob, sponsor);

        // CheckCreate
        auto const seq = env.seq(alice);
        env(check::create(alice, bob, XRP(1)),
            sponsor::as(sponsor, tfSponsorReserve),
            sponsor::sig(sponsor));
        env.close();

        env.require(owners(alice, 1));
        env.require(sponsored_owners(alice, 1));
        env.require(sponsoring_owners(alice, 0));
        env.require(sponsoring_owners(sponsor, 1));

        // CheckCancel
        auto const checkId = keylet::check(alice, seq).key;
        env(check::cancel(alice, checkId));
        env.close();

        env.require(owners(alice, 0));
        env.require(sponsored_owners(alice, 0));
        env.require(sponsoring_owners(sponsor, 0));

        auto const seq2 = env.seq(alice);
        env(check::create(alice, bob, XRP(1)),
            sponsor::as(sponsor, tfSponsorReserve),
            sponsor::sig(sponsor));
        env.close();

        env.require(owners(alice, 1));
        env.require(sponsored_owners(alice, 1));
        env.require(sponsoring_owners(alice, 0));
        env.require(sponsoring_owners(sponsor, 1));

        // CheckCash
        auto const checkId2 = keylet::check(alice, seq2).key;
        env(check::cash(bob, checkId2, XRP(1)));
        env.close();

        env.require(owners(alice, 0));
        env.require(sponsored_owners(alice, 0));
        env.require(sponsoring_owners(sponsor, 0));

        // printf(
        //     "meta: %s\n",
        //     env.meta()->getJson(JsonOptions::none).toStyledString().c_str());
    }

    void
    testOfffer()
    {
        testcase("Offer");
        using namespace test::jtx;
        Env env{*this, testable_amendments()};
        Account const alice("alice");
        Account const gw("gw");
        Account const sponsor("sponsor");

        auto USD = gw["USD"];

        env.fund(XRP(10000), alice, gw, sponsor);

        // OfferCreate
        auto const seq = env.seq(alice);
        env(offer(alice, USD(1), XRP(1)),
            sponsor::as(sponsor, tfSponsorReserve),
            sponsor::sig(sponsor));
        env.close();

        env.require(owners(alice, 1));
        env.require(sponsored_owners(alice, 1));
        env.require(sponsoring_owners(alice, 0));
        env.require(sponsoring_owners(sponsor, 1));

        // OfferCancel
        env(offer_cancel(alice, seq));
        env.close();

        env.require(owners(alice, 0));
        env.require(sponsored_owners(alice, 0));
        env.require(sponsoring_owners(alice, 0));
        env.require(sponsoring_owners(sponsor, 0));

        // TODO: test Offer Execution
    }

    void
    testTicket()
    {
        testcase("Ticket");
        using namespace test::jtx;
        Env env{*this, testable_amendments()};
        Account const alice("alice");
        Account const sponsor("master");

        env.fund(XRP(1000000), alice, sponsor);

        // TicketCreate
        std::uint32_t const ticketSeq{env.seq(alice) + 1};
        env(ticket::create(alice, 250),
            sponsor::as(sponsor, tfSponsorReserve),
            sponsor::sig(sponsor));
        env.close();

        env.require(owners(alice, 250));
        env.require(sponsored_owners(alice, 250));
        env.require(sponsoring_owners(alice, 0));
        env.require(sponsoring_owners(sponsor, 250));

        // use a Ticket
        env(noop(alice), ticket::use(ticketSeq + 1));
        env.close();

        env.require(owners(alice, 249));
        env.require(sponsored_owners(alice, 249));
        env.require(sponsoring_owners(sponsor, 249));
    }

    void
    testCredentials()
    {
        testcase("Credentials");
        using namespace test::jtx;
        Env env{*this, testable_amendments()};
        Account const issuer("issuer");
        Account const subject("subject");
        Account const sponsor("sponsor");

        env.fund(XRP(1000000), issuer, subject, sponsor);

        // CredentialsCreate
        env(credentials::create(subject, issuer, "credType"),
            credentials::uri("uri"),
            sponsor::as(sponsor, tfSponsorReserve),
            sponsor::sig(sponsor));
        env.close();

        env.require(owners(issuer, 1));
        env.require(owners(subject, 0));
        env.require(sponsored_owners(issuer, 1));
        env.require(sponsored_owners(subject, 0));
        env.require(sponsoring_owners(sponsor, 1));

        // CredentialsAccept
        env(credentials::accept(subject, issuer, "credType"),
            sponsor::as(sponsor, tfSponsorReserve),
            sponsor::sig(sponsor));
        env.close();

        env.require(owners(issuer, 0));
        env.require(owners(subject, 1));
        env.require(sponsored_owners(issuer, 0));
        env.require(sponsored_owners(subject, 1));
        env.require(sponsoring_owners(sponsor, 1));

        // CredentialsDelete
        env(credentials::deleteCred(subject, subject, issuer, "credType"));
        env.close();

        env.require(owners(issuer, 0));
        env.require(owners(subject, 0));
        env.require(sponsored_owners(issuer, 0));
        env.require(sponsored_owners(subject, 0));
        env.require(sponsoring_owners(sponsor, 0));
    }

    void
    testDelegate()
    {
        testcase("Delegate");
        using namespace test::jtx;
        Env env{*this, testable_amendments()};
        Account const alice("alice");
        Account const bob("bob");
        Account const sponsor("sponsor");

        env.fund(XRP(1000000), alice, bob, sponsor);

        // DelegateSet
        env(delegate::set(alice, bob, {"Payment"}),
            sponsor::as(sponsor, tfSponsorReserve),
            sponsor::sig(sponsor));
        env.close();

        env.require(owners(alice, 1));
        env.require(sponsored_owners(alice, 1));
        env.require(sponsoring_owners(sponsor, 1));

        // delete
        env(delegate::set(alice, bob, {}));
        env.close();

        env.require(owners(alice, 0));
        env.require(sponsored_owners(alice, 0));
        env.require(sponsoring_owners(sponsor, 0));
    }

    void
    testDepositPreauth()
    {
    }

    void
    testDID()
    {
        testcase("DID");
        using namespace test::jtx;
        Env env{*this, testable_amendments()};
        Account const alice("alice");
        Account const sponsor("sponsor");

        env.fund(XRP(1000000), alice, sponsor);

        // DIDSet
        env(did::set(alice),
            did::uri("uri"),
            sponsor::as(sponsor, tfSponsorReserve),
            sponsor::sig(sponsor));
        env.close();

        env.require(owners(alice, 1));
        env.require(sponsored_owners(alice, 1));
        env.require(sponsoring_owners(sponsor, 1));

        // DIDDelete
        env(did::del(alice));
        env.close();

        env.require(owners(alice, 0));
        env.require(sponsored_owners(alice, 0));
        env.require(sponsoring_owners(sponsor, 0));
    }

    void
    testEscrow()
    {
    }

    void
    testMPToken()
    {
    }

    void
    testNFToken()
    {
    }

    void
    testNFTokenOffer()
    {
    }

    void
    testPayChan()
    {
    }

    void
    testPermissionedDomain()
    {
    }

    void
    testOracle()
    {
    }

    void
    testSignerList()
    {
        testcase("SignerList");
        using namespace test::jtx;
        Env env{*this, testable_amendments()};
        Account const alice("alice");
        Account const sponsor("sponsor");

        env.fund(XRP(1000000), alice, sponsor);

        Account const bob("bob");

        // SignerListSet
        env(signers(alice, 1, {{bob, 1}}),
            sponsor::as(sponsor, tfSponsorReserve),
            sponsor::sig(sponsor));
        env.close();

        env.require(owners(alice, 1));
        env.require(sponsored_owners(alice, 1));
        env.require(sponsoring_owners(sponsor, 1));

        // Delete
        env(signers(alice, none));
        env.close();

        env.require(owners(alice, 0));
        env.require(sponsored_owners(alice, 0));
        env.require(sponsoring_owners(sponsor, 0));
    }

    void
    testTrust()
    {
    }

    void
    testVault()
    {
    }

    void
    testXChain()
    {
    }

    void
    testSponsorReserve()
    {
        testCheck();
        testOfffer();
        testTicket();
        testCredentials();
        testDelegate();
        // testDepositPreauth();
        testDID();
        // testEscrow();
        // testMPToken();
        // testNFToken();
        // testNFTokenOffer();
        // testPayChan();
        // testPermissionedDomain();
        // testOracle();
        testSignerList();
        // testTrust();
        // testVault();
        // testXChain();
    }

    void
    run() override
    {
        testDisabled();
        testSponsorFee();
        testSponsorAccount();
        testSponsorReserve();
    }
};

BEAST_DEFINE_TESTSUITE(Sponsor, app, ripple);

}  // namespace ripple

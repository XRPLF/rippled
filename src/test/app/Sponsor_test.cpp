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
#include <test/jtx/amount.h>
#include <test/jtx/check.h>
#include <test/jtx/did.h>
#include <test/jtx/owners.h>
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

        env(sponsor::transfer(alice), ter(temDISABLED));
    }

    void
    testTransferSponsor()
    {
        testcase("Transfer Sponsor");
        using namespace test::jtx;

        {
            // sponsor account
            Env env{*this, testable_amendments()};
            Account const alice("alice");
            Account const sponsor1("sponsor1");
            Account const sponsor2("sponsor2");
            env.fund(XRP(10000), alice, sponsor1, sponsor2);

            env(sponsor::transfer(alice),
                sponsor::as(sponsor1, tfSponsorReserve),
                sponsor::sig(sponsor1));
            env.close();

            env.require(sponsored_owners(alice, 0));
            env.require(sponsored_owners(sponsor1, 0));
            env.require(sponsoring_owners(alice, 0));
            env.require(sponsoring_owners(sponsor1, 0));
            env.require(sponsoring_account_count(alice, 0));
            env.require(sponsoring_account_count(sponsor1, 1));
            auto const sle1 = env.le(keylet::account(alice));
            BEAST_EXPECT(sle1->isFieldPresent(sfSponsorAccount));
            BEAST_EXPECT(sle1->getAccountID(sfSponsorAccount) == sponsor1.id());

            // transfer sponsor
            env(sponsor::transfer(alice),
                sponsor::as(sponsor2, tfSponsorReserve),
                sponsor::sig(sponsor2));
            env.close();

            env.require(sponsored_owners(alice, 0));
            env.require(sponsored_owners(sponsor1, 0));
            env.require(sponsored_owners(sponsor2, 0));
            env.require(sponsoring_owners(alice, 0));
            env.require(sponsoring_owners(sponsor1, 0));
            env.require(sponsoring_owners(sponsor2, 0));
            env.require(sponsoring_account_count(alice, 0));
            env.require(sponsoring_account_count(sponsor1, 0));
            env.require(sponsoring_account_count(sponsor2, 1));
            auto const sle2 = env.le(keylet::account(alice));
            BEAST_EXPECT(sle2->isFieldPresent(sfSponsorAccount));
            BEAST_EXPECT(sle2->getAccountID(sfSponsorAccount) == sponsor2.id());

            // dissolve sponsor
            env(sponsor::transfer(alice));
            env.close();

            env.require(sponsored_owners(alice, 0));
            env.require(sponsored_owners(sponsor1, 0));
            env.require(sponsored_owners(sponsor2, 0));
            env.require(sponsoring_owners(alice, 0));
            env.require(sponsoring_owners(sponsor1, 0));
            env.require(sponsoring_owners(sponsor2, 0));
            env.require(sponsoring_account_count(alice, 0));
            env.require(sponsoring_account_count(sponsor1, 0));
            env.require(sponsoring_account_count(sponsor2, 0));
            auto const sle3 = env.le(keylet::account(alice));
            BEAST_EXPECT(!sle3->isFieldPresent(sfSponsorAccount));
        }
        {
            // sponsor object
            Env env{*this, testable_amendments()};
            Account const alice("alice");
            Account const bob("bob");
            Account const sponsor1("sponsor1");
            Account const sponsor2("sponsor2");
            env.fund(XRP(10000), alice, bob, sponsor1, sponsor2);

            auto const seq = env.seq(alice);
            env(check::create(alice, bob, XRP(1)));
            env.close();

            auto const checkId = keylet::check(alice, seq).key;
            BEAST_EXPECT(env.le(keylet::unchecked(checkId)) != nullptr);

            env(sponsor::transfer(alice, checkId),
                sponsor::as(sponsor1, tfSponsorReserve),
                sponsor::sig(sponsor1));
            env.close();

            env.require(owners(alice, 1));
            env.require(sponsored_owners(alice, 1));
            env.require(sponsored_owners(sponsor1, 0));
            env.require(sponsoring_owners(alice, 0));
            env.require(sponsoring_owners(sponsor1, 1));
            env.require(sponsoring_account_count(alice, 0));
            env.require(sponsoring_account_count(sponsor1, 0));
            auto const sle1 = env.le(keylet::unchecked(checkId));
            BEAST_EXPECT(sle1->isFieldPresent(sfSponsorAccount));
            BEAST_EXPECT(sle1->getAccountID(sfSponsorAccount) == sponsor1.id());

            // transfer sponsor
            env(sponsor::transfer(alice, checkId),
                sponsor::as(sponsor2, tfSponsorReserve),
                sponsor::sig(sponsor2));
            env.close();

            env.require(sponsored_owners(alice, 1));
            env.require(sponsored_owners(sponsor1, 0));
            env.require(sponsored_owners(sponsor2, 0));
            env.require(sponsoring_owners(alice, 0));
            env.require(sponsoring_owners(sponsor1, 0));
            env.require(sponsoring_owners(sponsor2, 1));
            env.require(sponsoring_account_count(alice, 0));
            env.require(sponsoring_account_count(sponsor1, 0));
            env.require(sponsoring_account_count(sponsor2, 0));
            auto const sle2 = env.le(keylet::unchecked(checkId));
            BEAST_EXPECT(sle2->isFieldPresent(sfSponsorAccount));
            BEAST_EXPECT(sle2->getAccountID(sfSponsorAccount) == sponsor2.id());

            // dissolve sponsor
            env(sponsor::transfer(alice, checkId));
            env.close();

            env.require(sponsored_owners(alice, 0));
            env.require(sponsored_owners(sponsor1, 0));
            env.require(sponsored_owners(sponsor2, 0));
            env.require(sponsoring_owners(alice, 0));
            env.require(sponsoring_owners(sponsor1, 0));
            env.require(sponsoring_owners(sponsor2, 0));
            env.require(sponsoring_account_count(alice, 0));
            env.require(sponsoring_account_count(sponsor1, 0));
            env.require(sponsoring_account_count(sponsor2, 0));
            auto const sle3 = env.le(keylet::unchecked(checkId));
            BEAST_EXPECT(!sle3->isFieldPresent(sfSponsorAccount));
        }
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

        auto const reserve = env.current()->fees().reserve;
        auto const increment = env.current()->fees().increment;

        env.fund(XRP(10000), alice, bob);
        env.fund(drops(reserve) + drops(increment) - drops(1), sponsor);

        // check sponsor balance
        env(check::create(alice, bob, XRP(1)),
            sponsor::as(sponsor, tfSponsorReserve),
            sponsor::sig(sponsor),
            ter(tecINSUFFICIENT_RESERVE));

        env(pay(alice, sponsor, drops(1)));
        env.close();

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
        testTransferSponsor();
        testSponsorFee();
        testSponsorAccount();
        testSponsorReserve();
    }
};

BEAST_DEFINE_TESTSUITE(Sponsor, app, ripple);

}  // namespace ripple

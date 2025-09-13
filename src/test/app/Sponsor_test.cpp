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
        env(sponsor::set(sponsor, alice, 0), ter(temDISABLED));
    }

    void
    testInvalidSponsorSet()
    {
        testcase("Invalid SponsorSet");
        using namespace test::jtx;
        Env env{*this, testable_amendments()};
        Account const alice("alice");
        Account const bob("bob");
        Account const sponsor("sponsor");
        Account const noFunded("noFunded");
        Account const gw("gw");

        auto const USD = gw["USD"];
        env.fund(XRP(10000), alice, sponsor, gw);
        env.close();

        //
        // preflight
        //

        // Invalid flags
        {
            env(sponsor::set(
                    sponsor, alice, ~tfSponsorSetMask - tfInnerBatchTxn),
                ter(temINVALID_FLAG));

            env(sponsor::set(
                    sponsor,
                    alice,
                    tfSponsorshipSetRequireSignForFee |
                        tfSponsorshipClearRequireSignForFee),
                ter(temINVALID_FLAG));

            env(sponsor::set(
                    sponsor,
                    alice,
                    tfSponsorshipSetRequireSignForReserve |
                        tfSponsorshipClearRequireSignForReserve),
                ter(temINVALID_FLAG));

            for (auto flag :
                 {tfSponsorshipSetRequireSignForFee,
                  tfSponsorshipClearRequireSignForFee,
                  tfSponsorshipSetRequireSignForReserve,
                  tfSponsorshipClearRequireSignForReserve})
            {
                env(sponsor::set(sponsor, alice, tfDeleteObject | flag),
                    ter(temINVALID_FLAG));
            }
        }

        // invalid SponsorAccount
        env(sponsor::set(alice, sponsor, tfDeleteObject),
            sponsor::sponsorAcc(alice),
            ter(temMALFORMED));
        env(sponsor::set(alice, sponsor, tfDeleteObject),
            sponsor::sponsorAcc(bob),
            ter(temMALFORMED));
        env(sponsor::set(alice, alice, 0),
            sponsor::sponsorAcc(sponsor),
            ter(temMALFORMED));

        // Invalid Sponsee
        env(sponsor::set(sponsor, sponsor, 0), ter(temMALFORMED));

        // Invalid feeAmount
        env(sponsor::set(
                sponsor, alice, tfSponsorshipClearRequireSignForFee, 0, XRP(1)),
            ter(temMALFORMED));

        for (auto amt : {XRP(-1), XRP(0), USD(1)})
        {
            env(sponsor::set(sponsor, alice, 0, 1, amt), ter(temBAD_AMOUNT));
        }

        // Invalid reserveCount
        env(sponsor::set(
                sponsor, alice, tfSponsorshipClearRequireSignForReserve, 1),
            ter(temMALFORMED));
        env(sponsor::set(sponsor, alice, 0, 0), ter(temMALFORMED));

        // Invalid Delete operation
        env(sponsor::set(sponsor, alice, tfDeleteObject, 1), ter(temMALFORMED));
        env(sponsor::set(sponsor, alice, tfDeleteObject, std::nullopt, XRP(1)),
            ter(temMALFORMED));

        //
        // preclaim
        //

        // Invalid Sponsee
        env(sponsor::set(sponsor, noFunded, 0), ter(tecNO_DST));

        // Invalid Delete operation (not found)
        env(sponsor::set(sponsor, alice, tfDeleteObject), ter(tecNO_ENTRY));

        // DisallowIncomingSponsor: tested in other testcase

        // create sponsor to use above tests
        env(sponsor::set(sponsor, alice, 0, 100, XRP(100)), ter(tesSUCCESS));
        env.close();
    }

    void
    testSingleSigning()
    {
        testcase("Single signing");
        using namespace test::jtx;
        Env env{*this, testable_amendments()};
        Account const alice("alice");
        Account const sponsor("sponsor");
        Account const invalid("invalid");

        env.fund(XRP(10000), alice, sponsor);
        env.close();

        // Signature doesn't exist
        auto tx = noop(alice);
        tx[sfSponsor.jsonName][sfAccount.jsonName] = sponsor.human();
        tx[sfSponsor.jsonName][sfSigningPubKey.jsonName] =
            strHex(sponsor.pk().slice());

        env(tx,
            fee(XRP(1)),
            sponsor::as(sponsor, tfSponsorReserve),
            ter(telENV_RPC_FAILED));

        // Invalid signature
        tx[sfSponsor.jsonName][sfTxnSignature.jsonName] = "DEADBEEF";
        env(tx,
            fee(XRP(1)),
            sponsor::as(sponsor, tfSponsorReserve),
            ter(telENV_RPC_FAILED));

        // Signer account doesn't exist
        env(noop(alice),
            fee(XRP(1)),
            sponsor::as(invalid, tfSponsorReserve),
            sponsor::sig(invalid),
            ter(tefBAD_AUTH));

        // Success
        env(noop(alice),
            fee(XRP(1)),
            sponsor::as(sponsor, tfSponsorReserve),
            sponsor::sig(sponsor),
            ter(tesSUCCESS));
    }

    void
    testMultiSigning()
    {
        testcase("Multi signing");
        using namespace test::jtx;
        Env env{*this, testable_amendments()};
        Account const alice("alice");
        Account const sponsor("sponsor");
        Account const invalid("invalid");

        Account const signer1("signer1");
        Account const signer2("signer2");

        env.fund(XRP(10000), alice, sponsor);
        env.close();

        env(signers(sponsor, 1, {{signer1, 1}, {signer2, 1}}));
        env.close();

        // Invalid signature
        auto tx = noop(alice);
        auto& signers1 =
            tx[sfSponsor.jsonName][sfSigners.jsonName][0U][sfSigner.jsonName];
        signers1[sfAccount.jsonName] = signer1.human();
        signers1[sfSigningPubKey.jsonName] = strHex(signer1.pk().slice());
        signers1[sfTxnSignature.jsonName] = "DEADBEEF";
        env(tx,
            fee(XRP(1)),
            sponsor::as(sponsor, tfSponsorReserve),
            ter(telENV_RPC_FAILED));

        // Signer account doesn't exist
        env(noop(alice),
            fee(XRP(1)),
            sponsor::as(invalid, tfSponsorReserve),
            sponsor::msig({signer1}),
            ter(tefBAD_AUTH));

        env(noop(alice),
            fee(XRP(1)),
            sponsor::as(sponsor, tfSponsorReserve),
            sponsor::msig({signer1}),
            ter(tesSUCCESS));

        env(signers(sponsor, 2, {{signer1, 1}, {signer2, 1}}));
        env.close();

        env(noop(alice),
            fee(XRP(1)),
            sponsor::as(sponsor, tfSponsorReserve),
            sponsor::msig({signer1, signer2}),
            ter(tesSUCCESS));
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
            env.fund(XRP(10000), alice);
            env.fund(env.current()->fees().reserve * 2 - 1, sponsor1, sponsor2);
            env.close();

            env(sponsor::transfer(alice),
                sponsor::as(sponsor1, tfSponsorReserve),
                sponsor::sig(sponsor1),
                ter(tecINSUFFICIENT_RESERVE));

            env(pay(alice, sponsor1, drops(1)));
            env.close();

            env(sponsor::transfer(alice),
                sponsor::as(sponsor1, tfSponsorReserve),
                sponsor::sig(sponsor1));
            env.close();

            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor1) == 1);
            auto const sle1 = env.le(keylet::account(alice));
            BEAST_EXPECT(sle1->isFieldPresent(sfSponsorAccount));
            BEAST_EXPECT(sle1->getAccountID(sfSponsorAccount) == sponsor1.id());

            // transfer sponsor
            env(sponsor::transfer(alice),
                sponsor::as(sponsor2, tfSponsorReserve),
                sponsor::sig(sponsor2),
                ter(tecINSUFFICIENT_RESERVE));

            env(pay(alice, sponsor2, drops(1)));
            env.close();

            env(sponsor::transfer(alice),
                sponsor::as(sponsor2, tfSponsorReserve),
                sponsor::sig(sponsor2));
            env.close();

            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, sponsor2) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor2) == 1);
            auto const sle2 = env.le(keylet::account(alice));
            BEAST_EXPECT(sle2->isFieldPresent(sfSponsorAccount));
            BEAST_EXPECT(sle2->getAccountID(sfSponsorAccount) == sponsor2.id());

            // dissolve sponsor
            env(pay(alice,
                    sponsor2,
                    (env.balance(alice).value() -
                     env.current()->fees().reserve - XRP(1) + drops(1))),
                fee(XRP(1)));
            env.close();

            BEAST_EXPECT(
                env.balance(alice) == env.current()->fees().reserve - drops(1));
            env(sponsor::transfer(alice), ter(tecINSUFFICIENT_RESERVE));
            env.close();

            env(pay(sponsor2, alice, XRP(1)));
            env.close();

            env(sponsor::transfer(alice));
            env.close();

            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, sponsor2) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor2) == 0);
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
            env.fund(XRP(10000), alice, bob);
            env.fund(
                env.current()->fees().reserve +
                    env.current()->fees().increment - drops(1),
                sponsor1,
                sponsor2);
            env.close();

            auto const seq = env.seq(alice);
            env(check::create(alice, bob, XRP(1)));
            env.close();

            auto const checkId = keylet::check(alice, seq).key;
            BEAST_EXPECT(env.le(keylet::unchecked(checkId)) != nullptr);

            env(sponsor::transfer(alice, checkId),
                sponsor::as(sponsor1, tfSponsorReserve),
                sponsor::sig(sponsor1),
                ter(tecINSUFFICIENT_RESERVE));
            env.close();

            env(pay(alice, sponsor1, drops(1)));
            env.close();

            env(sponsor::transfer(alice, checkId),
                sponsor::as(sponsor1, tfSponsorReserve),
                sponsor::sig(sponsor1));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor1) == 1);
            BEAST_EXPECT(sponsoringAccountCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor1) == 0);
            auto const sle1 = env.le(keylet::unchecked(checkId));
            BEAST_EXPECT(sle1->isFieldPresent(sfSponsorAccount));
            BEAST_EXPECT(sle1->getAccountID(sfSponsorAccount) == sponsor1.id());

            // transfer sponsor
            env(sponsor::transfer(alice, checkId),
                sponsor::as(sponsor2, tfSponsorReserve),
                sponsor::sig(sponsor2),
                ter(tecINSUFFICIENT_RESERVE));

            env(pay(alice, sponsor2, drops(1)));
            env.close();

            env(sponsor::transfer(alice, checkId),
                sponsor::as(sponsor2, tfSponsorReserve),
                sponsor::sig(sponsor2));
            env.close();

            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, sponsor2) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);
            BEAST_EXPECT(sponsoringAccountCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor2) == 0);
            auto const sle2 = env.le(keylet::unchecked(checkId));
            BEAST_EXPECT(sle2->isFieldPresent(sfSponsorAccount));
            BEAST_EXPECT(sle2->getAccountID(sfSponsorAccount) == sponsor2.id());

            // dissolve sponsor
            env(pay(alice,
                    sponsor2,
                    (env.balance(alice).value() -
                     env.current()->fees().reserve -
                     env.current()->fees().increment - XRP(1) + drops(1))),
                fee(XRP(1)));
            env.close();

            env(sponsor::transfer(alice, checkId),
                ter(tecINSUFFICIENT_RESERVE));
            env.close();

            env(pay(sponsor2, alice, XRP(1)));
            env.close();

            env(sponsor::transfer(alice, checkId));
            env.close();

            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, sponsor2) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor2) == 0);
            auto const sle3 = env.le(keylet::unchecked(checkId));
            BEAST_EXPECT(!sle3->isFieldPresent(sfSponsorAccount));
        }
    }

    void
    testSponsorFee()
    {
        using namespace test::jtx;

        testcase("Sponsor Fee");

        {
            // co-signing
            Env env{*this, testable_amendments()};
            Account const alice("alice");
            Account const sponsor("sponsor");
            env.fund(XRP(10000), alice, sponsor);
            env.close();

            env(noop(alice),
                fee(XRP(1)),
                sponsor::as(sponsor, tfSponsorFee),
                sponsor::sig(sponsor),
                ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(alice) == XRP(10000));
            BEAST_EXPECT(env.balance(sponsor) == XRP(9999));
        }
        {
            // pre funded
            Env env{*this, testable_amendments()};
            Account const alice("alice");
            Account const sponsor("sponsor");
            env.fund(XRP(10000), alice, sponsor);
            env.close();

            env(sponsor::set(sponsor, alice, 0, std::nullopt, XRP(1)),
                ter(tesSUCCESS));
            env.close();

            auto const sle = env.le(keylet::sponsor(sponsor, alice));
            BEAST_EXPECT(sle->getFieldAmount(sfFeeAmount) == XRP(1));
            BEAST_EXPECT(!sle->isFieldPresent(sfReserveCount));

            auto const sponsorBalanceBefore = env.balance(sponsor);
            auto const aliceBalanceBefore = env.balance(alice);

            env(noop(alice),
                fee(drops(500)),
                sponsor::as(sponsor, tfSponsorFee),
                ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(alice) == aliceBalanceBefore);
            BEAST_EXPECT(env.balance(sponsor) == sponsorBalanceBefore);

            auto const sle2 = env.le(keylet::sponsor(sponsor, alice));
            BEAST_EXPECT(
                sle2->getFieldAmount(sfFeeAmount) == XRP(1) - drops(500));
        }
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

        BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
        BEAST_EXPECT(sponsoringAccountCount(env, sponsor) == 1);
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
        env.close();

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

        BEAST_EXPECT(ownerCount(env, alice) == 1);
        BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
        BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

        // CheckCancel
        auto const checkId = keylet::check(alice, seq).key;
        env(check::cancel(alice, checkId));
        env.close();

        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);

        auto const seq2 = env.seq(alice);
        env(check::create(alice, bob, XRP(1)),
            sponsor::as(sponsor, tfSponsorReserve),
            sponsor::sig(sponsor));
        env.close();

        BEAST_EXPECT(ownerCount(env, alice) == 1);
        BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
        BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

        // CheckCash
        auto const checkId2 = keylet::check(alice, seq2).key;
        env(check::cash(bob, checkId2, XRP(1)));
        env.close();

        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
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
        env.close();

        // OfferCreate
        auto const seq = env.seq(alice);
        env(offer(alice, USD(1), XRP(1)),
            sponsor::as(sponsor, tfSponsorReserve),
            sponsor::sig(sponsor));
        env.close();

        BEAST_EXPECT(ownerCount(env, alice) == 1);
        BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
        BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

        // OfferCancel
        env(offer_cancel(alice, seq));
        env.close();

        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
        BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);

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
        env.close();

        // TicketCreate
        std::uint32_t const ticketSeq{env.seq(alice) + 1};
        env(ticket::create(alice, 250),
            sponsor::as(sponsor, tfSponsorReserve),
            sponsor::sig(sponsor));
        env.close();

        BEAST_EXPECT(ownerCount(env, alice) == 250);
        BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 250);
        BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 250);

        // use a Ticket
        env(noop(alice), ticket::use(ticketSeq + 1));
        env.close();

        BEAST_EXPECT(ownerCount(env, alice) == 249);
        BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 249);
        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 249);
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
        env.close();

        // CredentialsCreate
        env(credentials::create(subject, issuer, "credType"),
            credentials::uri("uri"),
            sponsor::as(sponsor, tfSponsorReserve),
            sponsor::sig(sponsor));
        env.close();

        BEAST_EXPECT(ownerCount(env, issuer) == 1);
        BEAST_EXPECT(ownerCount(env, subject) == 0);
        BEAST_EXPECT(sponsoredOwnerCount(env, issuer) == 1);
        BEAST_EXPECT(sponsoredOwnerCount(env, subject) == 0);
        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

        // CredentialsAccept
        env(credentials::accept(subject, issuer, "credType"),
            sponsor::as(sponsor, tfSponsorReserve),
            sponsor::sig(sponsor));
        env.close();

        BEAST_EXPECT(ownerCount(env, issuer) == 0);
        BEAST_EXPECT(ownerCount(env, subject) == 1);
        BEAST_EXPECT(sponsoredOwnerCount(env, issuer) == 0);
        BEAST_EXPECT(sponsoredOwnerCount(env, subject) == 1);
        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

        // CredentialsDelete
        env(credentials::deleteCred(subject, subject, issuer, "credType"));
        env.close();

        BEAST_EXPECT(ownerCount(env, issuer) == 0);
        BEAST_EXPECT(ownerCount(env, subject) == 0);
        BEAST_EXPECT(sponsoredOwnerCount(env, issuer) == 0);
        BEAST_EXPECT(sponsoredOwnerCount(env, subject) == 0);
        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
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
        env.close();

        // DelegateSet
        env(delegate::set(alice, bob, {"Payment"}),
            sponsor::as(sponsor, tfSponsorReserve),
            sponsor::sig(sponsor));
        env.close();

        BEAST_EXPECT(ownerCount(env, alice) == 1);
        BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

        // delete
        env(delegate::set(alice, bob, {}));
        env.close();

        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
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
        env.close();

        // DIDSet
        env(did::set(alice),
            did::uri("uri"),
            sponsor::as(sponsor, tfSponsorReserve),
            sponsor::sig(sponsor));
        env.close();

        BEAST_EXPECT(ownerCount(env, alice) == 1);
        BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

        // DIDDelete
        env(did::del(alice));
        env.close();

        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
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
        env.close();

        Account const bob("bob");

        // SignerListSet
        env(signers(alice, 1, {{bob, 1}}),
            sponsor::as(sponsor, tfSponsorReserve),
            sponsor::sig(sponsor));
        env.close();

        BEAST_EXPECT(ownerCount(env, alice) == 1);
        BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

        // Delete
        env(signers(alice, none));
        env.close();

        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
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
    testDisallowIncoming()
    {
        testcase("DisallowIncoming");
        using namespace test::jtx;
        Env env{*this, testable_amendments()};
        Account const alice("alice");
        Account const sponsor("sponsor");

        env.fund(XRP(1000000), alice, sponsor);
        env.close();

        // set DisallowIncomingSponsor
        env(fset(alice, asfDisallowIncomingSponsor));
        env.close();

        // Create sponsor should fail
        env(sponsor::set(sponsor, alice, 0, 100, XRP(100)),
            ter(tecNO_PERMISSION));
        env.close();

        // clear flag
        env(fclear(alice, asfDisallowIncomingSponsor));
        env.close();

        // Create sponsor
        env(sponsor::set(sponsor, alice, 0, 100, XRP(100)), ter(tesSUCCESS));
        env.close();

        // set flag
        env(fset(alice, asfDisallowIncomingSponsor));
        env.close();

        // Update sponsor should success
        env(sponsor::set(sponsor, alice, 0, 100, XRP(100)), ter(tesSUCCESS));
        env.close();

        // Delete sponsor shoud success
        env(sponsor::set(sponsor, alice, tfDeleteObject), ter(tesSUCCESS));
        env.close();
    }
    void
    testAccountDelete()
    {
        testcase("AccountDelete");
        using namespace test::jtx;
        Env env{*this, testable_amendments()};
        Account const alice("alice");
        Account const bob("bob");
        Account const sponsor("sponsor");

        env.fund(XRP(1000000), alice, bob, sponsor);
        env.close();

        // set sponsor
        env(sponsor::set(sponsor, alice, 0, 100, XRP(100)), ter(tesSUCCESS));
        env.close();

        // AccountDelete
        env(acctdelete(alice, bob));
        env.close();

        BEAST_EXPECT(ownerCount(env, alice) == 0);
        BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
        BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
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
        testInvalidSponsorSet();

        testSingleSigning();
        testMultiSigning();
        // testInvalidSigninig(); // borh TxnSignature and Signers are present
        // -> error
        testTransferSponsor();
        testSponsorFee();
        testSponsorAccount();
        testSponsorReserve();
        testDisallowIncoming();

        // testAccountDelete();
    }
};

BEAST_DEFINE_TESTSUITE(Sponsor, app, ripple);

}  // namespace ripple

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
#include <test/jtx/Env.h>
#include <test/jtx/multisign.h>
#include <test/jtx/ticket.h>
#include <test/jtx/xchain_bridge.h>

#include <xrpl/basics/strHex.h>
#include <xrpl/protocol/Feature.h>

namespace ripple {
namespace test {

static STAmount
reserve(jtx::Env& env, std::uint32_t count)
{
    return env.current()->fees().accountReserve(count);
}

static void
adjustAccountXRPBalance(
    jtx::Env& env,
    jtx::Account const& account,
    STAmount const& balanceTo)
{
    using namespace test::jtx;
    XRPL_ASSERT(
        isXRP(balanceTo), "adjustAccountXRPBalance: balanceTo must be XRP");
    auto const currentBalance = env.balance(account);
    if (currentBalance == balanceTo)
        return;

    if (currentBalance > balanceTo)
        env(pay(account, env.master, currentBalance - (balanceTo + XRP(1))),
            fee(XRP(1)));
    else
        env(pay(env.master, account, balanceTo - currentBalance));

    env.close();
}

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
        env(sponsor::set(sponsor, 0), ter(temDISABLED));
    }

    void
    testInvalidSponsorshipSet()
    {
        testcase("Invalid SponsorshipSet");
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
            env(sponsor::set(sponsor, ~tfSponsorshipSetMask - tfInnerBatchTxn),
                sponsor::sponseeAcc(alice),
                ter(temINVALID_FLAG));

            env(sponsor::set(
                    sponsor,
                    tfSponsorshipSetRequireSignForFee |
                        tfSponsorshipClearRequireSignForFee),
                sponsor::sponseeAcc(alice),
                ter(temINVALID_FLAG));

            env(sponsor::set(
                    sponsor,
                    tfSponsorshipSetRequireSignForReserve |
                        tfSponsorshipClearRequireSignForReserve),
                sponsor::sponseeAcc(alice),
                ter(temINVALID_FLAG));

            for (auto flag :
                 {tfSponsorshipSetRequireSignForFee,
                  tfSponsorshipClearRequireSignForFee,
                  tfSponsorshipSetRequireSignForReserve,
                  tfSponsorshipClearRequireSignForReserve})
            {
                env(sponsor::set(sponsor, tfDeleteObject | flag),
                    sponsor::sponseeAcc(alice),
                    ter(temINVALID_FLAG));
            }
        }

        // invalid SponsorAccount / Sponsee
        // Account = Sponsor
        env(sponsor::set(alice, tfDeleteObject),
            sponsor::sponsorAcc(alice),
            ter(temMALFORMED));
        // Account = Sponsee
        env(sponsor::set(alice, tfDeleteObject),
            sponsor::sponseeAcc(alice),
            ter(temMALFORMED));
        // Both Sponsor and Sponsee are specified
        env(sponsor::set(alice, 0),
            sponsor::sponsorAcc(sponsor),
            sponsor::sponseeAcc(alice),
            ter(temMALFORMED));

        // Invalid feeAmount
        for (auto amt : {XRP(-1), XRP(0), USD(1)})
        {
            env(sponsor::set_fee(sponsor, 0, amt),
                sponsor::sponseeAcc(alice),
                ter(temBAD_AMOUNT));
        }

        // Invalid reserveCount
        env(sponsor::set_reserve(sponsor, 0, 0),
            sponsor::sponseeAcc(alice),
            ter(temMALFORMED));

        // Invalid Delete operation
        env(sponsor::set_reserve(sponsor, tfDeleteObject, 1),
            sponsor::sponseeAcc(alice),
            ter(temMALFORMED));
        env(sponsor::set_fee(sponsor, tfDeleteObject, XRP(1)),
            sponsor::sponseeAcc(alice),
            ter(temMALFORMED));
        // TODO: test MaxFee with tfDeleteObject

        //
        // preclaim
        //

        // Invalid Sponsee
        env(sponsor::set(sponsor, 0),
            sponsor::sponseeAcc(noFunded),
            ter(tecNO_DST));

        // Invalid Delete operation (not found)
        env(sponsor::set(sponsor, tfDeleteObject),
            sponsor::sponseeAcc(alice),
            ter(tecNO_ENTRY));

        // DisallowIncomingSponsor: tested in other testcase

        // create sponsor to use above tests
        env(sponsor::set(sponsor, 0, 100, XRP(100)),
            sponsor::sponseeAcc(alice),
            ter(tesSUCCESS));
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
            ter(terNO_ACCOUNT));

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
            ter(tefNOT_MULTI_SIGNING));

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
    testSimpleSponsorshipSet()
    {
        testcase("Simple SponsorshipSet");
        using namespace test::jtx;
        Env env{*this, testable_amendments()};
        Account const alice("alice");
        Account const sponsor("sponsor");
        env.fund(XRP(10000), alice, sponsor);
        env.close();

        //
        env(sponsor::set(
                sponsor,
                tfSponsorshipSetRequireSignForFee |
                    tfSponsorshipSetRequireSignForReserve,
                100,
                XRP(100),
                XRP(1)),
            sponsor::sponseeAcc(alice),
            ter(tesSUCCESS));
        env.close();

        // delete from sponsor
        env(sponsor::del(sponsor), sponsor::sponseeAcc(alice), ter(tesSUCCESS));
        env.close();

        env(sponsor::set(
                sponsor,
                tfSponsorshipSetRequireSignForFee |
                    tfSponsorshipSetRequireSignForReserve,
                100,
                XRP(100),
                XRP(1)),
            sponsor::sponseeAcc(alice),
            ter(tesSUCCESS));
        env.close();

        // delete from sponsee
        env(sponsor::del(alice), sponsor::sponsorAcc(sponsor), ter(tesSUCCESS));
        env.close();
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

            // Invalid Owner
            env(sponsor::transfer(bob, checkId),
                sponsor::as(sponsor1, tfSponsorReserve),
                sponsor::sig(sponsor1),
                ter(tecNO_PERMISSION));
            env.close();

            // Valid Owner
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
            Account const bob("bob");
            Account const sponsor("sponsor");
            env.fund(XRP(10000), alice, bob);
            env.close();

            {
                // Fee should be checked before permission check,
                // otherwise tecNO_SPONSOR_PERMISSION returned when permission
                // check fails could cause context reset to pay fee because it
                // is tec error
                auto aliceBalance = env.balance(alice);
                auto bobBalance = env.balance(bob);
                auto sponsorBalance = env.balance(sponsor);

                env(pay(alice, bob, XRP(100)),
                    fee(XRP(2000)),
                    sponsor::as(sponsor, tfSponsorFee),
                    sponsor::sig(sponsor),
                    ter(terNO_ACCOUNT));
                env.close();
                BEAST_EXPECT(env.balance(alice) == aliceBalance);
                BEAST_EXPECT(env.balance(bob) == bobBalance);
                BEAST_EXPECT(env.balance(sponsor) == sponsorBalance);
            }

            env.fund(XRP(1000), sponsor);
            env.close();

            {
                // Sponsor pays the fee
                auto aliceBalance = env.balance(alice);
                auto bobBalance = env.balance(bob);
                auto sponsorBalance = env.balance(sponsor);

                auto const sendAmt = XRP(100);
                auto const feeAmt = XRP(10);
                env(pay(alice, bob, sendAmt),
                    fee(feeAmt),
                    sponsor::as(sponsor, tfSponsorFee),
                    sponsor::sig(sponsor));
                env.close();
                BEAST_EXPECT(env.balance(alice) == aliceBalance - sendAmt);
                BEAST_EXPECT(env.balance(bob) == bobBalance + sendAmt);
                BEAST_EXPECT(env.balance(sponsor) == sponsorBalance - feeAmt);
            }

            {
                // insufficient balance to pay fee
                auto aliceBalance = env.balance(alice);
                auto bobBalance = env.balance(bob);
                auto sponsorBalance = env.balance(sponsor);

                env(pay(alice, bob, XRP(100)),
                    fee(XRP(2000)),
                    sponsor::as(sponsor, tfSponsorFee),
                    sponsor::sig(sponsor),
                    ter(terINSUF_FEE_B));
                env.close();
                BEAST_EXPECT(env.balance(alice) == aliceBalance);
                BEAST_EXPECT(env.balance(bob) == bobBalance);
                BEAST_EXPECT(env.balance(sponsor) == sponsorBalance);
            }

            {
                // fee is paid by Sponsor
                // on context reset (tec error)
                auto aliceBalance = env.balance(alice);
                auto bobBalance = env.balance(bob);
                auto sponsorBalance = env.balance(sponsor);
                auto const feeAmt = XRP(10);

                env(pay(alice, bob, XRP(20000)),
                    fee(feeAmt),
                    sponsor::as(sponsor, tfSponsorFee),
                    sponsor::sig(sponsor),
                    ter(tecUNFUNDED_PAYMENT));
                env.close();

                BEAST_EXPECT(env.balance(alice) == aliceBalance);
                BEAST_EXPECT(env.balance(bob) == bobBalance);
                BEAST_EXPECT(env.balance(sponsor) == sponsorBalance - feeAmt);
            }
        }

        {
            // pre funded
            Env env{*this, testable_amendments()};
            Account const alice("alice");
            Account const bob("bob");
            Account const sponsor("sponsor");
            env.fund(XRP(10000), alice, bob, sponsor);
            env.close();

            auto const sponsorFeeBalance = [&](Account const& sponsor,
                                               Account const& alice) {
                return env.le(keylet::sponsor(sponsor, alice))
                    ->getFieldAmount(sfFeeAmount)
                    .xrp();
            };

            {
                // Fee should be checked before permission check,
                // otherwise tecNO_SPONSOR_PERMISSION returned when permission
                // check fails could cause context reset to pay fee because it
                // is tec error
                auto aliceBalance = env.balance(alice);
                auto bobBalance = env.balance(bob);
                auto sponsorBalance = env.balance(sponsor);

                env(pay(alice, bob, XRP(100)),
                    fee(XRP(2000)),
                    sponsor::as(sponsor, tfSponsorFee),
                    ter(terNO_SPONSORSHIP));
                env.close();
                BEAST_EXPECT(env.balance(alice) == aliceBalance);
                BEAST_EXPECT(env.balance(bob) == bobBalance);
                BEAST_EXPECT(env.balance(sponsor) == sponsorBalance);
            }

            env(sponsor::set_fee(sponsor, 0, XRP(100)),
                sponsor::sponseeAcc(alice));
            env.close();

            {
                // Sponsor pays the fee
                auto aliceBalance = env.balance(alice);
                auto bobBalance = env.balance(bob);
                auto sponsorBalance = env.balance(sponsor);
                auto sponsorFee = sponsorFeeBalance(sponsor, alice);

                auto const sendAmt = XRP(100);
                auto const feeAmt = XRP(10);
                env(pay(alice, bob, sendAmt),
                    fee(feeAmt),
                    sponsor::as(sponsor, tfSponsorFee));
                env.close();

                BEAST_EXPECT(env.balance(alice) == aliceBalance - sendAmt);
                BEAST_EXPECT(env.balance(bob) == bobBalance + sendAmt);
                BEAST_EXPECT(env.balance(sponsor) == sponsorBalance);
                BEAST_EXPECT(
                    sponsorFeeBalance(sponsor, alice) == sponsorFee - feeAmt);
            }

            {
                // insufficient balance to pay fee
                {
                    // > FeeAmount
                    auto aliceBalance = env.balance(alice);
                    auto bobBalance = env.balance(bob);
                    auto sponsorBalance = env.balance(sponsor);
                    auto sponsorFee = sponsorFeeBalance(sponsor, alice);

                    env(pay(alice, bob, XRP(100)),
                        fee(XRP(100) + drops(1)),
                        sponsor::as(sponsor, tfSponsorFee),
                        ter(terINSUF_FEE_B));
                    env.close();

                    BEAST_EXPECT(env.balance(alice) == aliceBalance);
                    BEAST_EXPECT(env.balance(bob) == bobBalance);
                    BEAST_EXPECT(env.balance(sponsor) == sponsorBalance);
                    BEAST_EXPECT(
                        sponsorFeeBalance(sponsor, alice) == sponsorFee);
                }

                // reset FeeAmount and MaxFee
                env(sponsor::del(sponsor), sponsor::sponseeAcc(alice));
                env.close();
                env(sponsor::set_fee(sponsor, 0, XRP(10), XRP(1)),
                    sponsor::sponseeAcc(alice));
                env.close();

                {
                    // > MaxFee
                    auto aliceBalance = env.balance(alice);
                    auto bobBalance = env.balance(bob);
                    auto sponsorBalance = env.balance(sponsor);
                    auto sponsorFee = sponsorFeeBalance(sponsor, alice);

                    env(pay(alice, bob, XRP(100)),
                        fee(XRP(1) + drops(1)),
                        sponsor::as(sponsor, tfSponsorFee),
                        ter(terINSUF_FEE_B));
                    env.close();

                    BEAST_EXPECT(env.balance(alice) == aliceBalance);
                    BEAST_EXPECT(env.balance(bob) == bobBalance);
                    BEAST_EXPECT(env.balance(sponsor) == sponsorBalance);
                    BEAST_EXPECT(
                        sponsorFeeBalance(sponsor, alice) == sponsorFee);
                }
            }

            {
                // fee is paid by Sponsor
                // on context reset (tec error)
                auto aliceBalance = env.balance(alice);
                auto bobBalance = env.balance(bob);
                auto sponsorBalance = env.balance(sponsor);
                auto sponsorFee = sponsorFeeBalance(sponsor, alice);
                auto const feeAmt = XRP(1);

                env(pay(alice, bob, XRP(20000)),
                    fee(feeAmt),
                    sponsor::as(sponsor, tfSponsorFee),
                    ter(tecUNFUNDED_PAYMENT));
                env.close();

                BEAST_EXPECT(env.balance(alice) == aliceBalance);
                BEAST_EXPECT(env.balance(bob) == bobBalance);
                BEAST_EXPECT(env.balance(sponsor) == sponsorBalance);
                BEAST_EXPECT(
                    sponsorFeeBalance(sponsor, alice) == sponsorFee - feeAmt);
            }
        }

        // test lsfSponsorshipRequireSignForFee
        {
            Env env{*this, testable_amendments()};
            Account const alice("alice");
            Account const bob("bob");
            Account const sponsor("sponsor");
            env.fund(XRP(10000), alice, bob, sponsor);
            env.close();

            // set flag
            env(sponsor::set_fee(
                    sponsor, tfSponsorshipSetRequireSignForFee, XRP(10)),
                sponsor::sponseeAcc(alice));
            env.close();

            env(pay(alice, bob, XRP(100)),
                fee(XRP(10)),
                sponsor::as(sponsor, tfSponsorFee),
                ter(terNO_SPONSORSHIP));
            env.close();

            // clear flag
            env(sponsor::set_fee(
                    sponsor, tfSponsorshipClearRequireSignForFee, XRP(10)),
                sponsor::sponseeAcc(alice));
            env.close();

            env(pay(alice, bob, XRP(100)),
                fee(XRP(10)),
                sponsor::as(sponsor, tfSponsorFee),
                ter(tesSUCCESS));
            env.close();
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
        Account const bob("bob");
        Account const charlie("charlie");
        env.fund(XRP(10000), alice, sponsor);

        // Account is not sponsored by normal Sponsor specification
        {
            env(pay(alice,
                    bob,
                    STAmount(env.current()->fees().accountReserve(0))),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            auto const bobSle = env.le(keylet::account(bob));
            BEAST_EXPECT(!bobSle->isFieldPresent(sfSponsorAccount));
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor) == 0);
        }

        // Use tfSponsorCreatedAccount to sponsor an account
        {
            // to funded accoutn
            env(pay(sponsor,
                    bob,
                    STAmount(env.current()->fees().accountReserve(0))),
                txflags(tfSponsorCreatedAccount),
                ter(tecNO_SPONSOR_PERMISSION));

            // to non-funded account
            env(pay(sponsor,
                    charlie,
                    STAmount(env.current()->fees().accountReserve(0))),
                txflags(tfSponsorCreatedAccount));
            env.close();

            auto const charlieSle = env.le(keylet::account(charlie));
            BEAST_EXPECT(charlieSle->isFieldPresent(sfSponsorAccount));
            BEAST_EXPECT(
                charlieSle->getAccountID(sfSponsorAccount) == sponsor.id());
            BEAST_EXPECT(sponsoredOwnerCount(env, charlie) == 0);
            BEAST_EXPECT(sponsoringAccountCount(env, sponsor) == 1);
        }
    }

    void
    testRequireFlag()
    {
        testcase("SponsorshipRequireSignForReserve");
        using namespace test::jtx;

        Env env{*this, testable_amendments()};
        Account const alice("alice");
        Account const bob("bob");
        Account const sponsor("sponsor");
        env.fund(XRP(10000), alice, bob, sponsor);
        env.close();

        // set flag
        env(sponsor::set_reserve(
                sponsor, tfSponsorshipSetRequireSignForReserve, 10),
            sponsor::sponseeAcc(alice));
        env.close();

        env(check::create(alice, bob, XRP(100)),
            fee(XRP(10)),
            sponsor::as(sponsor, tfSponsorReserve),
            ter(terNO_SPONSORSHIP));
        env.close();

        // clear flag
        env(sponsor::set_reserve(
                sponsor, tfSponsorshipClearRequireSignForReserve, 1),
            sponsor::sponseeAcc(alice));
        env.close();

        env(check::create(alice, bob, XRP(100)),
            fee(XRP(10)),
            sponsor::as(sponsor, tfSponsorReserve),
            ter(tesSUCCESS));
        env.close();
    }

    void
    testAMM()
    {
        testcase("AMM");
        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const gw("gw");
        Account const sponsor("sponsor");

        auto const USD = gw["USD"];
        auto const EUR = gw["EUR"];

        auto const ammCreate = [&](Env& env,
                                   Account const& account,
                                   STAmount const& amount1,
                                   STAmount const& amount2) {
            Json::Value jv;
            jv[jss::TransactionType] = jss::AMMCreate;
            jv[jss::Account] = account.human();
            jv[jss::Amount] = amount1.getJson(JsonOptions::none);
            jv[jss::Amount2] = amount2.getJson(JsonOptions::none);
            jv[jss::TradingFee] = 0;
            jv[jss::Fee] =
                std::to_string(env.current()->fees().increment.drops());
            return jv;
        };

        auto const ammDeposit = [&](Env& env,
                                    Account const& account,
                                    STAmount const& amount1,
                                    STAmount const& amount2) {
            Json::Value jv;
            jv[jss::TransactionType] = jss::AMMDeposit;
            jv[jss::Account] = account.human();
            jv[jss::Asset] =
                STIssue(sfAsset, amount1.issue()).getJson(JsonOptions::none);
            jv[jss::Asset2] =
                STIssue(sfAsset, amount2.issue()).getJson(JsonOptions::none);
            jv[jss::Amount] = amount1.value().getJson(JsonOptions::none);
            jv[jss::Amount2] = amount2.value().getJson(JsonOptions::none);
            jv[jss::Flags] = tfTwoAsset;
            return jv;
        };

        {
            // AMMCreate
            // - sponsor LPToken
            // - doesn't sponsor AMM object
            Env env{*this, testable_amendments()};
            env.fund(XRP(10000), alice, gw, sponsor);
            env.close();

            env(trust(alice, USD(10000)));
            env(trust(alice, EUR(10000)));
            env.close();

            env(pay(gw, alice, USD(1000)));
            env(pay(gw, alice, EUR(1000)));
            env.close();

            {
                // AMMCreate doesn't check INSUFFICIENT_RESERVE now
                // see: https://github.com/XRPLF/rippled/issues/5812
                // check INSUFFICIENT_RESERVE
                adjustAccountXRPBalance(
                    env, sponsor, reserve(env, 1) - drops(1));

                env(ammCreate(env, alice, USD(100), EUR(100)),
                    sponsor::as(sponsor, tfSponsorReserve),
                    sponsor::sig(sponsor),
                    ter(tecINSUF_RESERVE_LINE));
                env.close();
                adjustAccountXRPBalance(env, sponsor, reserve(env, 2));
            }

            env(ammCreate(env, alice, USD(100), EUR(100)),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            auto const amm =
                env.current()->read(keylet::amm(USD.issue(), EUR.issue()));
            auto const ammAccount =
                Account("amm", amm->getAccountID(sfAccount));

            BEAST_EXPECT(
                ownerCount(env, alice) == 3);  // RippleState (USD,EUR/LP Token)
            BEAST_EXPECT(ownerCount(env, ammAccount) == 2);      //  USD, EUR
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);  // LPToken
            BEAST_EXPECT(sponsoredOwnerCount(env, ammAccount) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);  // LPToken
            BEAST_EXPECT(!env.le(keylet::amm(USD.issue(), EUR.issue()))
                              ->isFieldPresent(sfSponsorAccount));
        }
        {
            // AMMDeposit
            // - sponsor new LPToken
            Env env{*this, testable_amendments()};
            env.fund(XRP(10000), alice, bob, gw, sponsor);
            env.close();

            env(trust(alice, USD(10000)));
            env(trust(alice, EUR(10000)));
            env(trust(bob, USD(10000)));
            env(trust(bob, EUR(10000)));
            env.close();

            env(pay(gw, alice, USD(1000)));
            env(pay(gw, alice, EUR(1000)));
            env(pay(gw, bob, USD(1000)));
            env(pay(gw, bob, EUR(1000)));
            env.close();

            env(ammCreate(env, alice, USD(100), EUR(100)));
            env.close();

            BEAST_EXPECT(ownerCount(env, bob) == 2);  // RippleState (USD,EUR)

            {
                // check INSUFFICIENT_RESERVE
                adjustAccountXRPBalance(
                    env, sponsor, reserve(env, 1) - drops(1));

                env(ammDeposit(env, bob, USD(100), EUR(100)),
                    sponsor::as(sponsor, tfSponsorReserve),
                    sponsor::sig(sponsor),
                    ter(tecINSUF_RESERVE_LINE));
                env.close();
                adjustAccountXRPBalance(env, sponsor, reserve(env, 2));
            }

            env(ammDeposit(env, bob, USD(100), EUR(100)),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            BEAST_EXPECT(
                ownerCount(env, bob) == 3);  // RippleState (USD,EUR/LP Token)
            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 1);       // LPToken
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);  // LPToken
        }
        {
            // AMMWithdraw
            {
                // Single Asset Withdraw
                // - sponsor new RippleState
                Env env{*this, testable_amendments()};
                env.fund(XRP(10000), alice, bob, gw, sponsor);
                env.close();

                env(trust(alice, USD(10000)));
                env(trust(alice, EUR(10000)));
                env.close();

                env(pay(gw, alice, USD(1000)));
                env(pay(gw, alice, EUR(1000)));
                env.close();

                env(ammCreate(env, alice, USD(1000), EUR(1000)),
                    sponsor::as(sponsor, tfSponsorReserve),
                    sponsor::sig(sponsor));
                env.close();

                env(trust(alice, USD(0)));
                env(trust(alice, EUR(0)));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == 1);           // LPToken
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);  // LPToken
                BEAST_EXPECT(
                    sponsoringOwnerCount(env, sponsor) == 1);  // LPToken

                Json::Value jv;
                jv[jss::TransactionType] = jss::AMMWithdraw;
                jv[jss::Account] = alice.human();
                jv[jss::Asset] =
                    STIssue(sfAsset, USD.issue()).getJson(JsonOptions::none);
                jv[jss::Asset2] =
                    STIssue(sfAsset, EUR.issue()).getJson(JsonOptions::none);
                jv[jss::Amount] = USD(100).value().getJson(JsonOptions::none);
                jv[jss::Flags] = tfSingleAsset;

                {
                    env(ticket::create(
                        sponsor, 1));  // adjust for free trustline
                    env.close();
                    BEAST_EXPECT(ownerCount(env, sponsor) == 1);
                    BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);
                    // check INSUFFICIENT_RESERVE
                    adjustAccountXRPBalance(
                        env, sponsor, reserve(env, 2) - drops(1));
                    env(jv,
                        sponsor::as(sponsor, tfSponsorReserve),
                        sponsor::sig(sponsor),
                        ter(tecINSUFFICIENT_RESERVE));
                    env.close();
                    adjustAccountXRPBalance(env, sponsor, reserve(env, 3));
                }

                env(jv,
                    sponsor::as(sponsor, tfSponsorReserve),
                    sponsor::sig(sponsor));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == 2);  // USD, LPToken
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 2);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 2);
            }
            {
                // Double Asset Withdraw
                // - sponsor new RippleState * 2
                // - remove sponsored LPToken
                Env env{*this, testable_amendments()};
                env.fund(XRP(10000), alice, bob, gw, sponsor);
                env.close();

                env(trust(alice, USD(10000)));
                env(trust(alice, EUR(10000)));
                env.close();

                env(pay(gw, alice, USD(1000)));
                env(pay(gw, alice, EUR(1000)));
                env.close();

                env(ammCreate(env, alice, USD(1000), EUR(1000)),
                    sponsor::as(sponsor, tfSponsorReserve),
                    sponsor::sig(sponsor));
                env.close();

                env(trust(alice, USD(0)));
                env(trust(alice, EUR(0)));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == 1);  // LPToken
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

                Json::Value jv;
                jv[jss::TransactionType] = jss::AMMWithdraw;
                jv[jss::Account] = alice.human();
                jv[jss::Asset] =
                    STIssue(sfAsset, USD.issue()).getJson(JsonOptions::none);
                jv[jss::Asset2] =
                    STIssue(sfAsset, EUR.issue()).getJson(JsonOptions::none);
                jv[jss::Flags] = tfWithdrawAll;

                {
                    env(ticket::create(
                        sponsor, 1));  // adjust for free trustline
                    env.close();
                    BEAST_EXPECT(ownerCount(env, sponsor) == 1);
                    BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);
                    // check INSUFFICIENT_RESERVE for RippleStates * 2
                    adjustAccountXRPBalance(
                        env, sponsor, reserve(env, 3) - drops(1));
                    env(jv,
                        sponsor::as(sponsor, tfSponsorReserve),
                        sponsor::sig(sponsor),
                        ter(tecINSUFFICIENT_RESERVE));
                    env.close();
                    adjustAccountXRPBalance(env, sponsor, reserve(env, 4));
                }

                env(jv,
                    sponsor::as(sponsor, tfSponsorReserve),
                    sponsor::sig(sponsor));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == 2);  // USD, EUR
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 2);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 2);
            }
        }
        {
            // AMMClawback
            // - doesn't sponsor holder's new RippleState
            // - remove sponsored LPToken
            Account const gw2("gw2");
            auto const EUR2 = gw2["EUR"];

            Env env{*this, testable_amendments()};
            env.fund(XRP(10000), alice, gw, gw2, sponsor);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();

            env(trust(alice, USD(10000)));
            env(trust(alice, EUR2(10000)));
            env.close();

            env(pay(gw, alice, USD(100)));
            env(pay(gw2, alice, EUR2(100)));
            env.close();

            env(ammCreate(env, alice, USD(100), EUR2(100)),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            env(trust(alice, USD(0)));
            env(trust(alice, EUR2(0)));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);  // LPToken
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);
            {
                // doesn't sponsor holder's new RippleState
                env(amm::ammClawback(gw, alice, USD, EUR2, USD(10)),
                    sponsor::as(sponsor, tfSponsorReserve),
                    sponsor::sig(sponsor));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == 2);  // LPToken, EUR2
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);
            }
            {
                env(amm::ammClawback(gw, alice, USD, EUR2, std::nullopt),
                    sponsor::as(sponsor, tfSponsorReserve),
                    sponsor::sig(sponsor));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == 1);  // EUR2
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            }
        }
    }

    void
    testCheck()
    {
        testcase("Check");
        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const gw("gw");
        Account const sponsor("sponsor");
        Account const sponsor2("sponsor2");

        auto const USD = gw["USD"];

        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(10000), alice, bob, sponsor, sponsor2);
            env.close();

            // CheckCreate -> CheckCancel
            auto const seq = env.seq(alice);
            env(check::create(alice, bob, XRP(1)),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);  // Check
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            auto const keylet = keylet::check(alice, seq);
            BEAST_EXPECT(
                env.le(keylet)->getAccountID(sfSponsorAccount) == sponsor.id());

            // transfer sponsor
            env(sponsor::transfer(alice, keylet.key),
                sponsor::as(sponsor2, tfSponsorReserve),
                sponsor::sig(sponsor2));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);  // Check
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

            BEAST_EXPECT(
                env.le(keylet)->getAccountID(sfSponsorAccount) ==
                sponsor2.id());

            // CheckCancel
            env(check::cancel(alice, keylet.key));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
        }

        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(10000), alice, bob, sponsor);
            env.close();

            // CheckCreate -> CheckCash
            auto const seq2 = env.seq(alice);
            env(check::create(alice, bob, XRP(1)),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);  // Check
            BEAST_EXPECT(ownerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            // CheckCash
            auto const checkId2 = keylet::check(alice, seq2).key;
            env(check::cash(bob, checkId2, XRP(1)),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
        }

        // RippleState sponsor (CheckCashMakesTrustLine)
        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(10000), alice, bob, gw, sponsor, sponsor2);
            env.close();

            env.trust(USD(100), alice);
            env.close();
            env(pay(gw, alice, USD(100)));
            env.close();

            // CheckCreate -> CheckCash
            auto const seq2 = env.seq(alice);
            env(check::create(alice, bob, USD(1)),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 2);  // RippleState + Check
            BEAST_EXPECT(ownerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            auto const keylet = keylet::check(alice, seq2);
            BEAST_EXPECT(
                env.le(keylet)->getAccountID(sfSponsorAccount) == sponsor.id());

            // CheckCash
            env(check::cash(bob, keylet.key, USD(1)),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);  // RippleState
            BEAST_EXPECT(ownerCount(env, bob) == 1);    // RippleState
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);
        }

        {
            // check INSUFFICIENT_RESERVE

            {
                // CheckCreate
                Env env{*this, testable_amendments()};
                env.fund(XRP(10000), alice, bob, sponsor);
                env.close();

                adjustAccountXRPBalance(
                    env, sponsor, reserve(env, 1) - drops(1));

                env(check::create(alice, bob, XRP(1)),
                    sponsor::as(sponsor, tfSponsorReserve),
                    sponsor::sig(sponsor),
                    ter(tecINSUFFICIENT_RESERVE));
                env.close();

                adjustAccountXRPBalance(env, sponsor, reserve(env, 1));

                env(check::create(alice, bob, XRP(1)),
                    sponsor::as(sponsor, tfSponsorReserve),
                    sponsor::sig(sponsor),
                    ter(tesSUCCESS));
                env.close();
            }

            {
                //  CheckCash (CheckCashMakesTrustLine)
                Env env{*this, testable_amendments()};
                env.fund(XRP(10000), alice, bob, gw, sponsor);
                env.close();

                env.trust(USD(100), alice);
                env.close();
                env(pay(gw, alice, USD(100)));
                env.close();

                auto const seq = env.seq(alice);
                env(check::create(alice, bob, USD(1)));
                env.close();

                adjustAccountXRPBalance(
                    env, sponsor, reserve(env, 1) - drops(1));

                auto const keylet = keylet::check(alice, seq);
                env(check::cash(bob, keylet.key, USD(1)),
                    sponsor::as(sponsor, tfSponsorReserve),
                    sponsor::sig(sponsor),
                    ter(tecNO_LINE_INSUF_RESERVE));
                env.close();
            }
        }
    }

    void
    testOffer()
    {
        testcase("Offer");
        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const gw("gw");
        Account const sponsor1("sponsor1");
        Account const sponsor2("sponsor2");

        auto USD = gw["USD"];
        auto EUR = gw["EUR"];

        {
            Env env{*this, testable_amendments()};

            env.fund(XRP(10000), alice, gw, sponsor1, sponsor2);
            env.close();

            // OfferCreate
            auto const seq = env.seq(alice);
            env(offer(alice, USD(1), XRP(1)),
                sponsor::as(sponsor1, tfSponsorReserve),
                sponsor::sig(sponsor1));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor1) == 1);

            // transfer sponsor
            auto const keylet = keylet::offer(alice, seq);
            env(sponsor::transfer(alice, keylet.key),
                sponsor::as(sponsor2, tfSponsorReserve),
                sponsor::sig(sponsor2));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

            BEAST_EXPECT(
                env.le(keylet)->getAccountID(sfSponsorAccount) ==
                sponsor2.id());

            // OfferCancel
            env(offer_cancel(alice, seq));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
        }

        {
            Env env{*this, testable_amendments()};

            env.fund(XRP(10000), alice, gw, sponsor1, sponsor2);
            env.close();

            // OfferCreate
            auto const seq = env.seq(alice);
            env(offer(alice, USD(1), XRP(1)),
                sponsor::as(sponsor1, tfSponsorReserve),
                sponsor::sig(sponsor1));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor1) == 1);

            // OfferCreate with Cancel (new sponsor)
            auto const seq2 = env.seq(alice);
            env(offer(alice, USD(1), XRP(1)),
                json(jss::OfferSequence, seq),
                sponsor::as(sponsor2, tfSponsorReserve),
                sponsor::sig(sponsor2));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

            // OfferCreate with Cancel (no sponsor)
            env(offer(alice, USD(1), XRP(1)), json(jss::OfferSequence, seq2));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor1) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
        }

        // test Offer Execution doesn't sponsor new trustline
        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(10000), alice, bob, gw, sponsor1, sponsor2);
            env.close();

            env(trust(alice, USD(100)));
            env(trust(bob, EUR(100)));
            env.close();

            env(pay(gw, alice, USD(100)));
            env(pay(gw, bob, EUR(100)));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 1);

            // OfferCreate
            env(offer(alice, EUR(1), USD(1)),
                sponsor::as(sponsor1, tfSponsorReserve),
                sponsor::sig(sponsor1));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor1) == 1);

            BEAST_EXPECT(ownerCount(env, bob) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);

            // OfferCreate (cross offer)
            env(offer(bob, USD(1), EUR(1)),
                sponsor::as(sponsor2, tfSponsorReserve),
                sponsor::sig(sponsor2));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor1) == 0);

            // does not sponsor new trustline by cross offer
            BEAST_EXPECT(ownerCount(env, bob) == 2);
            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
        }

        {
            // check INSUFFICIENT_RESERVE for OfferCreate
            Env env{*this, testable_amendments()};
            env.fund(XRP(10000), alice, bob, gw, sponsor1, sponsor2);
            env.close();

            env.trust(USD(100), alice);
            env.close();
            env(pay(gw, alice, USD(100)));
            env.close();

            adjustAccountXRPBalance(env, sponsor1, reserve(env, 1) - drops(1));

            // fullly not crossed
            env(offer(alice, USD(1), XRP(1)),
                sponsor::as(sponsor1, tfSponsorReserve),
                sponsor::sig(sponsor1),
                ter(tecINSUF_RESERVE_OFFER));

            // partially crossed
            env(offer(gw, XRP(1), USD(1)));
            env.close();

            env(offer(alice, USD(5), XRP(5)),
                sponsor::as(sponsor1, tfSponsorReserve),
                sponsor::sig(sponsor1),
                ter(tecINSUF_RESERVE_OFFER));
        }
    }

    void
    testTicket()
    {
        testcase("Ticket");
        using namespace test::jtx;
        Account const alice("alice");
        Account const sponsor("master");
        Account const sponsor2("sponsor2");

        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, sponsor, sponsor2);
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

            auto const keylet = keylet::ticket(alice, ticketSeq);
            BEAST_EXPECT(
                env.le(keylet)->getAccountID(sfSponsorAccount) == sponsor.id());

            // transfer sponsor
            env(sponsor::transfer(alice, keylet.key),
                sponsor::as(sponsor2, tfSponsorReserve),
                sponsor::sig(sponsor2));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 250);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 250);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 249);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

            BEAST_EXPECT(
                env.le(keylet)->getAccountID(sfSponsorAccount) ==
                sponsor2.id());

            // use a Ticket
            env(noop(alice), ticket::use(ticketSeq));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 249);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 249);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 249);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
        }

        {
            // check INSUFFICIENT_RESERVE for TicketCreate
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, sponsor, sponsor2);
            env.close();

            adjustAccountXRPBalance(env, sponsor, reserve(env, 1) - drops(1));
            env(ticket::create(alice, 1),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor),
                ter(tecINSUFFICIENT_RESERVE));
            env.close();

            adjustAccountXRPBalance(env, sponsor, reserve(env, 249));
            env(ticket::create(alice, 250),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor),
                ter(tecINSUFFICIENT_RESERVE));
            env.close();
        }
    }

    void
    testCredentials()
    {
        testcase("Credentials");
        using namespace test::jtx;
        Account const issuer("issuer");
        Account const subject("subject");
        Account const sponsor("sponsor");
        Account const sponsor2("sponsor2");

        auto const credType = std::string("credType");
        auto const credTypeSlice = Slice(credType.data(), credType.size());

        // CredentialsCreate
        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), issuer, subject, sponsor, sponsor2);
            env.close();

            env(credentials::create(subject, issuer, credType),
                credentials::uri("uri"),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, issuer) == 1);
            BEAST_EXPECT(ownerCount(env, subject) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, issuer) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, subject) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            // transfer sponsor
            auto const keylet =
                keylet::credential(subject, issuer, credTypeSlice);
            env(sponsor::transfer(issuer, keylet.key),
                sponsor::as(sponsor2, tfSponsorReserve),
                sponsor::sig(sponsor2));
            env.close();

            BEAST_EXPECT(ownerCount(env, issuer) == 1);
            BEAST_EXPECT(ownerCount(env, subject) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, issuer) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, subject) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

            // CredentialsAccept
            env(credentials::accept(subject, issuer, credType),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, subject) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, issuer) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, subject) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);

            // CredentialsDelete
            env(credentials::deleteCred(subject, subject, issuer, credType));
            env.close();

            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, subject) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, issuer) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, subject) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
        }

        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), issuer, subject, sponsor);
            env.close();

            // Accept Sponsored Credentials without sponsoring
            env(credentials::create(subject, issuer, credType),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, issuer) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, issuer) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            env(credentials::accept(subject, issuer, credType));
            env.close();

            // sponsorship is removed
            BEAST_EXPECT(ownerCount(env, issuer) == 0);
            BEAST_EXPECT(ownerCount(env, subject) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, issuer) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, subject) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(
                !env.le(keylet::credential(subject, issuer, credTypeSlice))
                     ->isFieldPresent(sfSponsorAccount));

            env(credentials::deleteCred(subject, subject, issuer, credType));
            env.close();
        }

        {
            // check INSUFFICIENT_RESERVE for Credentials
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), issuer, subject, sponsor);
            env.close();

            // CredentialsCreate
            {
                adjustAccountXRPBalance(
                    env, sponsor, reserve(env, 1) - drops(1));
                env(credentials::create(subject, issuer, credType),
                    sponsor::as(sponsor, tfSponsorReserve),
                    sponsor::sig(sponsor),
                    ter(tecINSUFFICIENT_RESERVE));
                env.close();
            }
            // CredentialsAccept
            {
                env(credentials::create(subject, issuer, credType));
                env.close();

                adjustAccountXRPBalance(
                    env, sponsor, reserve(env, 1) - drops(1));
                env(credentials::accept(subject, issuer, credType),
                    sponsor::as(sponsor, tfSponsorReserve),
                    sponsor::sig(sponsor),
                    ter(tecINSUFFICIENT_RESERVE));
                env.close();
            }
        }
    }

    void
    testDelegate()
    {
        testcase("Delegate");
        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const sponsor("sponsor");
        Account const sponsor2("sponsor2");

        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, sponsor, sponsor2);
            env.close();

            // DelegateSet
            env(delegate::set(alice, bob, {"Payment"}),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            // transfer sponsor
            auto const keylet = keylet::delegate(alice, bob);
            env(sponsor::transfer(alice, keylet.key),
                sponsor::as(sponsor2, tfSponsorReserve),
                sponsor::sig(sponsor2));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

            // delete
            env(delegate::set(alice, bob, {}));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
        }

        {
            // check INSUFFICIENT_RESERVE for DelegateSet
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, sponsor);
            env.close();

            adjustAccountXRPBalance(env, sponsor, reserve(env, 1) - drops(1));
            env(delegate::set(alice, bob, {"Payment"}),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor),
                ter(tecINSUFFICIENT_RESERVE));
            env.close();
        }
    }

    void
    testDepositPreauth()
    {
        testcase("DepositPreauth");
        using namespace test::jtx;
        Account const alice("alice");
        Account const sponsor("sponsor");
        Account const sponsor2("sponsor2");

        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, sponsor, sponsor2);
            env.close();

            // DepositPreauthSet
            env(deposit::auth(alice, sponsor),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            // transfer sponsor
            auto const keylet = keylet::depositPreauth(alice, sponsor);
            env(sponsor::transfer(alice, keylet.key),
                sponsor::as(sponsor2, tfSponsorReserve),
                sponsor::sig(sponsor2));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

            // DepositPreauthDelete
            env(deposit::unauth(alice, sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
        }

        {
            // check INSUFFICIENT_RESERVE for DepositPreauthSet
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, sponsor);
            env.close();

            adjustAccountXRPBalance(env, sponsor, reserve(env, 1) - drops(1));
            env(deposit::auth(alice, sponsor),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor),
                ter(tecINSUFFICIENT_RESERVE));
            env.close();
        }
    }

    void
    testDID()
    {
        testcase("DID");
        using namespace test::jtx;
        Account const alice("alice");
        Account const sponsor("sponsor");
        Account const sponsor2("sponsor2");

        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, sponsor, sponsor2);
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

            // transfer sponsor
            auto const keylet = keylet::did(alice);
            env(sponsor::transfer(alice, keylet.key),
                sponsor::as(sponsor2, tfSponsorReserve),
                sponsor::sig(sponsor2));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

            // DIDDelete
            env(did::del(alice));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
        }

        {
            // check INSUFFICIENT_RESERVE for DIDSet
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, sponsor);
            env.close();

            adjustAccountXRPBalance(env, sponsor, reserve(env, 1) - drops(1));
            env(did::set(alice),
                did::uri("uri"),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor),
                ter(tecINSUFFICIENT_RESERVE));
            env.close();
        }
    }

    void
    testEscrow()
    {
        testcase("Escrow");
        using namespace test::jtx;
        using namespace std::chrono_literals;

        Account const alice("alice");
        Account const bob("bob");
        Account const gw("gw");
        Account const sponsor("sponsor");
        Account const sponsor2("sponsor2");
        auto const USD = gw["USD"];
        {
            // Native Escrow
            Env env{*this, testable_amendments()};
            auto const baseFee = env.current()->fees().base;

            env.fund(XRP(1000000), alice, bob, sponsor, sponsor2);
            env.close();

            // EscrowCreate
            auto const seq = env.seq(alice);
            env(escrow::create(alice, bob, XRP(100)),
                escrow::condition(escrow::cb1),
                escrow::cancel_time(env.now() + 100s),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            BEAST_EXPECT(
                env.le(keylet::escrow(alice, seq))
                    ->getAccountID(sfSponsorAccount) == sponsor.id());

            // transfer sponsor
            env(sponsor::transfer(alice, keylet::escrow(alice, seq).key),
                sponsor::as(sponsor2, tfSponsorReserve),
                sponsor::sig(sponsor2));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

            BEAST_EXPECT(
                env.le(keylet::escrow(alice, seq))
                    ->getAccountID(sfSponsorAccount) == sponsor2.id());

            // EscrowFinish
            env(escrow::finish(bob, alice, seq),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
        }

        {
            // IOU Escrow
            Env env{*this, testable_amendments()};
            auto const baseFee = env.current()->fees().base;

            env.fund(XRP(1000000), alice, bob, gw, sponsor, sponsor2);
            env.close();

            env(fset(gw, asfAllowTrustLineLocking));
            env.close();

            env.trust(USD(1000000), alice);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);

            // EscrowCreate
            auto const seq = env.seq(alice);
            env(escrow::create(alice, bob, USD(100)),
                escrow::condition(escrow::cb1),
                escrow::cancel_time(env.now() + 10s),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            BEAST_EXPECT(
                env.le(keylet::escrow(alice, seq))
                    ->getAccountID(sfSponsorAccount) == sponsor.id());

            // EscrowFinish
            env(escrow::finish(bob, alice, seq),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                sponsor::as(sponsor2, tfSponsorReserve),
                sponsor::sig(sponsor2),
                fee(baseFee * 150));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);

            BEAST_EXPECT(ownerCount(env, bob) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

            BEAST_EXPECT(
                env.le(keylet::line(bob, gw, USD.currency))
                    ->getAccountID(sfHighSponsorAccount) == sponsor2.id());
        }

        {
            // check INSUFFICIENT_RESERVE for EscrowCreate, EscrowFinish
            Env env{*this, testable_amendments()};
            auto const baseFee = env.current()->fees().base;
            env.fund(XRP(1000000), alice, bob, gw, sponsor);
            env.close();

            adjustAccountXRPBalance(env, sponsor, reserve(env, 1) - drops(1));
            env(escrow::create(alice, bob, XRP(100)),
                escrow::condition(escrow::cb1),
                escrow::cancel_time(env.now() + 10s),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor),
                ter(tecINSUFFICIENT_RESERVE));
            env.close();

            env(fset(gw, asfAllowTrustLineLocking));
            env.close();

            env.trust(USD(1000000), alice);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env.close();

            env(escrow::create(alice, bob, USD(100)),
                escrow::condition(escrow::cb1),
                escrow::cancel_time(env.now() + 10s),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor),
                ter(tecINSUFFICIENT_RESERVE));
            env.close();
            auto const seq = env.seq(alice);
            env(escrow::create(alice, bob, USD(100)),
                escrow::condition(escrow::cb1),
                escrow::cancel_time(env.now() + 10s));
            env.close();

            env(escrow::finish(bob, alice, seq),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor),
                fee(baseFee * 150),
                ter(tecNO_LINE_INSUF_RESERVE));
            env.close();
        }
    }

    void
    testMPToken()
    {
        testcase("MPToken");
        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const sponsor("sponsor");
        Account const sponsor2("sponsor2");

        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, sponsor, sponsor2);
            env.close();

            // MPTokenIssuanceCreate
            Json::Value jv = {};
            jv[sfAccount] = alice.human();
            jv[sfTransactionType] = jss::MPTokenIssuanceCreate;
            auto const mptid = makeMptID(env.seq(alice), alice.id());
            env(jv,
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            // transfer sponsor
            auto const mptIssuanceKeylet = keylet::mptIssuance(mptid);
            env(sponsor::transfer(alice, mptIssuanceKeylet.key),
                sponsor::as(sponsor2, tfSponsorReserve),
                sponsor::sig(sponsor2));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

            // MPTokenAuthorize
            jv = {};
            jv[sfTransactionType] = jss::MPTokenAuthorize;
            jv[sfAccount] = bob.human();
            jv[sfMPTokenIssuanceID] = to_string(mptid);
            env(jv,
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            // transfer sponsor
            auto const mptTokenKeylet = keylet::mptoken(mptid, bob);
            env(sponsor::transfer(bob, mptTokenKeylet.key),
                sponsor::as(sponsor2, tfSponsorReserve),
                sponsor::sig(sponsor2));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 2);

            // MPTokenAuthorize Unauthorize
            jv = {};
            jv[sfTransactionType] = jss::MPTokenAuthorize;
            jv[sfAccount] = bob.human();
            jv[sfMPTokenIssuanceID] = to_string(mptid);
            jv[sfFlags] = tfMPTUnauthorize;
            env(jv);
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

            // MPTokenIssuanceDestroy
            jv = {};
            jv[sfTransactionType] = jss::MPTokenIssuanceDestroy;
            jv[sfAccount] = alice.human();
            jv[sfMPTokenIssuanceID] = to_string(mptid);
            env(jv);
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
        }

        {
            // check INSUFFICIENT_RESERVE for MPToken
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, sponsor);
            env.close();

            adjustAccountXRPBalance(env, sponsor, reserve(env, 1) - drops(1));
            {
                // MPTokenIssuanceCreate
                Json::Value jv = {};
                jv[sfAccount] = alice.human();
                jv[sfTransactionType] = jss::MPTokenIssuanceCreate;
                // auto const mptid = makeMptID(env.seq(alice), alice.id());
                env(jv,
                    sponsor::as(sponsor, tfSponsorReserve),
                    sponsor::sig(sponsor),
                    ter(tecINSUFFICIENT_RESERVE));
                env.close();
            }

            {
                // MPTokenAuthorize
                Json::Value jv = {};
                jv[sfAccount] = alice.human();
                jv[sfTransactionType] = jss::MPTokenIssuanceCreate;
                auto const mptid = makeMptID(env.seq(alice), alice.id());
                env(jv);
                env.close();

                // for free mptoken checks
                adjustAccountXRPBalance(env, sponsor, reserve(env, 2));
                std::uint32_t ticketSeq{env.seq(sponsor) + 1};
                env(ticket::create(sponsor, 2));
                env.close();

                adjustAccountXRPBalance(
                    env, sponsor, reserve(env, 3) - drops(1));
                jv = {};
                jv[sfTransactionType] = jss::MPTokenAuthorize;
                jv[sfAccount] = bob.human();
                jv[sfMPTokenIssuanceID] = to_string(mptid);
                // error (non-free mptoken)
                env(jv,
                    sponsor::as(sponsor, tfSponsorReserve),
                    sponsor::sig(sponsor),
                    ter(tecINSUFFICIENT_RESERVE));
                env.close();

                env(noop(sponsor), ticket::use(ticketSeq));
                env.close();

                adjustAccountXRPBalance(
                    env, sponsor, reserve(env, 2) - drops(1));

                // pass (free mptoken)
                env(jv,
                    sponsor::as(sponsor, tfSponsorReserve),
                    sponsor::sig(sponsor),
                    ter(tesSUCCESS));
                env.close();
            }
        }
    }

    void
    testNFToken()
    {
        testcase("NFToken");
        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const sponsor("sponsor");
        Account const sponsor2("sponsor2");

        {
            Env env{*this, testable_amendments()};

            env.fund(XRP(1000000), alice, bob, sponsor);
            env.close();

            // NFTokenMint
            uint256 const nftId{token::getNextID(env, alice, 0)};
            env(token::mint(alice),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            // NFTokenBurn
            env(token::burn(alice, nftId));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);

            // NFTokenMintOffer
            env(token::mint(alice),
                token::amount(XRP(10000)),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 2);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 2);
        }

        {
            // multiple nft page process
            Env env{*this, testable_amendments()};

            env.fund(XRP(1000000), alice, bob, sponsor);
            env.close();

            auto const nftCount = 200;

            // NFTokenMint
            for (auto i = 0; i < nftCount; i++)
            {
                env(token::mint(alice),
                    sponsor::as(sponsor, tfSponsorReserve),
                    sponsor::sig(sponsor));
            }
            env.close();

            BEAST_EXPECT(
                ownerCount(env, alice) == sponsoredOwnerCount(env, alice));
            BEAST_EXPECT(
                sponsoredOwnerCount(env, alice) ==
                sponsoringOwnerCount(env, sponsor));

            // NFTokenBurn
            for (auto i = 0; i < nftCount; i++)
            {
                auto const nftId = token::getID(env, alice, 0, i, 0, 0);
                env(token::burn(alice, nftId));
            }
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
        }

        {
            // check INSUFFICIENT_RESERVE for NFTokenMint
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, sponsor);
            env.close();

            adjustAccountXRPBalance(env, sponsor, reserve(env, 1) - drops(1));
            env(token::mint(alice),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor),
                ter(tecINSUFFICIENT_RESERVE));
            env.close();

            adjustAccountXRPBalance(env, sponsor, reserve(env, 2) - drops(1));
            env(token::mint(alice),
                token::amount(XRP(10000)),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor),
                ter(tecINSUFFICIENT_RESERVE));
            env.close();
        }
    }

    void
    testNFTokenOffer()
    {
        testcase("NFTokenOffer");
        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const broker("broker");
        Account const sponsor("sponsor");
        Account const sponsor2("sponsor2");

        auto const taxon = 0u;

        {
            // Mint + CreateOffer + CancelOffer
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, sponsor, sponsor2);
            env.close();

            // Mint
            uint256 const nftId{
                token::getNextID(env, alice, taxon, tfTransferable)};
            env(token::mint(alice, taxon), txflags(tfTransferable));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);

            // NFTokenOfferCreate
            uint256 const offerIndex1 =
                keylet::nftoffer(alice, env.seq(alice)).key;
            env(token::createOffer(alice, nftId, XRP(1)),
                token::destination(bob),
                txflags(tfSellNFToken),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            uint256 const offerIndex2 =
                keylet::nftoffer(alice, env.seq(alice)).key;
            env(token::createOffer(alice, nftId, XRP(1)),
                token::destination(bob),
                txflags(tfSellNFToken),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 3);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 2);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 2);

            // transfer sponsor
            env(sponsor::transfer(alice, offerIndex1),
                sponsor::as(sponsor2, tfSponsorReserve),
                sponsor::sig(sponsor2));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 3);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 2);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

            // NFTokenOfferCancel
            env(token::cancelOffer(alice, {offerIndex1, offerIndex2}));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
        }

        {
            // Mint + CreateSellOffer + AcceptSellOffer
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, sponsor);
            env.close();

            // Mint
            uint256 const nftId{
                token::getNextID(env, alice, taxon, tfTransferable)};
            env(token::mint(alice, taxon), txflags(tfTransferable));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);

            // NFTokenOfferCreate
            uint256 const offerIndex =
                keylet::nftoffer(alice, env.seq(alice)).key;
            env(token::createOffer(alice, nftId, XRP(1)),
                token::destination(bob),
                txflags(tfSellNFToken),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            // NFTokenOfferAccept
            env(token::acceptSellOffer(bob, offerIndex));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, bob) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
        }

        {
            // Mint + CreateBuyOffer + AcceptBuyOffer
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, sponsor);
            env.close();

            // Mint
            uint256 const nftId{
                token::getNextID(env, alice, taxon, tfTransferable)};
            env(token::mint(alice, taxon), txflags(tfTransferable));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);

            // NFTokenOfferCreate
            uint256 const offerIndex = keylet::nftoffer(bob, env.seq(bob)).key;
            env(token::createOffer(bob, nftId, XRP(1)),
                token::owner(alice),
                token::destination(alice),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, bob) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            // NFTokenOfferAccept
            env(token::acceptBuyOffer(alice, offerIndex));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, bob) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
        }
        {
            // Broker
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, broker, sponsor, sponsor2);
            env.close();

            // Mint
            uint256 const nftId{
                token::getNextID(env, alice, taxon, tfTransferable)};
            env(token::mint(alice, taxon), txflags(tfTransferable));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);

            // NFTokenOfferCreate (BuyOffer)
            uint256 const buyOfferIndex =
                keylet::nftoffer(bob, env.seq(bob)).key;
            env(token::createOffer(bob, nftId, XRP(1)),
                token::owner(alice),
                token::destination(broker),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, bob) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            // NFTokenOfferCreate (SellOffer)
            uint256 const sellOfferIndex =
                keylet::nftoffer(alice, env.seq(alice)).key;
            env(token::createOffer(alice, nftId, XRP(1)),
                txflags(tfSellNFToken),
                token::destination(broker),
                sponsor::as(sponsor2, tfSponsorReserve),
                sponsor::sig(sponsor2));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

            // NFTokenOfferAccept
            env(token::brokerOffers(broker, buyOfferIndex, sellOfferIndex));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, bob) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
        }

        {
            // check INSUFFICIENT_RESERVE for NFTokenOffer

            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, sponsor);
            env.close();

            adjustAccountXRPBalance(env, sponsor, reserve(env, 1) - drops(1));

            auto const nextNftId = token::getNextID(env, alice, 0u, 0u);
            env(token::mint(alice));
            env.close();

            // NFTokenOfferCreate
            env(token::createOffer(alice, nextNftId, XRP(1)),
                txflags(tfSellNFToken),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor),
                ter(tecINSUFFICIENT_RESERVE));
            env.close();

            auto const offerIndex = keylet::nftoffer(alice, env.seq(alice)).key;
            env(token::createOffer(alice, nextNftId, XRP(1)),
                txflags(tfSellNFToken));
            env.close();

            // NFTokenOfferAccept (buyer)
            env(token::acceptSellOffer(bob, offerIndex),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor),
                ter(tecINSUFFICIENT_RESERVE));
            env.close();
        }
    }

    void
    testPayChan()
    {
        testcase("PayChan");
        using namespace test::jtx;
        using namespace std::literals::chrono_literals;
        Account const alice("alice");
        Account const bob("bob");
        Account const sponsor("sponsor");
        Account const sponsor2("sponsor2");

        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, sponsor, sponsor2);
            env.close();

            // PayChanCreate
            auto const pk = alice.pk();
            auto const settleDelay = 10s;
            auto const chan = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, XRP(100), settleDelay, pk),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            // transfer sponsor
            env(sponsor::transfer(alice, chan),
                sponsor::as(sponsor2, tfSponsorReserve),
                sponsor::sig(sponsor2));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

            env.close(env.now() + settleDelay);
            // PayChanClaim (delete PayChan)
            env(paychan::claim(bob, chan), txflags(tfClose));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
        }

        {
            // check INSUFFICIENT_RESERVE for PayChan
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, sponsor);
            env.close();

            adjustAccountXRPBalance(env, sponsor, reserve(env, 1) - drops(1));

            // PayChanCreate
            auto const pk = alice.pk();
            auto const settleDelay = 10s;
            env(paychan::create(alice, bob, XRP(100), settleDelay, pk),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor),
                ter(tecINSUFFICIENT_RESERVE));
            env.close();
        }
    }

    void
    testPermissionedDomain()
    {
        testcase("PermissionedDomain");
        using namespace test::jtx;
        Account const alice("alice");
        Account const sponsor("sponsor");
        Account const sponsor2("sponsor2");
        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, sponsor, sponsor2);
            env.close();

            // PermissionedDomainSet
            auto const seq = env.seq(alice);
            pdomain::Credentials credentials{{alice, "first credential"}};
            env(pdomain::setTx(alice, credentials),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            // transfer sponsor
            env(sponsor::transfer(
                    alice, keylet::permissionedDomain(alice, seq).key),
                sponsor::as(sponsor2, tfSponsorReserve),
                sponsor::sig(sponsor2));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

            // PermissionedDomainDelete
            auto objects = pdomain::getObjects(alice, env);
            auto const domain = objects.begin()->first;
            env(pdomain::deleteTx(alice, domain));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
        }
        {
            // check INSUFFICIENT_RESERVE for PermissionedDomain
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, sponsor);
            env.close();

            adjustAccountXRPBalance(env, sponsor, reserve(env, 1) - drops(1));

            // PermissionedDomainSet
            pdomain::Credentials credentials{{alice, "first credential"}};
            env(pdomain::setTx(alice, credentials),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor),
                ter(tecINSUFFICIENT_RESERVE));
            env.close();
        }
    }

    void
    testOracle()
    {
        testcase("Oracle");
        using namespace test::jtx;
        using namespace std::chrono;
        using DataSeries = std::vector<
            std::tuple<std::string, std::string, std::uint32_t, std::uint8_t>>;

        Account const alice("alice");
        Account const sponsor("sponsor");
        Account const sponsor2("sponsor2");

        auto const oracleSet =
            [](Env& env, Account const& account, uint8_t dataSeriesSize) {
                auto const now = env.timeKeeper().now();
                env.close(now + oracle::testStartTime - epoch_offset);
                Json::Value jv;
                jv[jss::TransactionType] = jss::OracleSet;
                jv[jss::Account] = to_string(account);
                jv[jss::OracleDocumentID] = 1;
                jv[jss::LastUpdateTime] = to_string(
                    duration_cast<seconds>(
                        env.current()->info().closeTime.time_since_epoch())
                        .count() +
                    epoch_offset.count() + 100);
                jv[jss::PriceDataSeries] = Json::arrayValue;
                jv[jss::Provider] = strHex(std::string{"provider"});
                jv[jss::AssetClass] = strHex(std::string{"currency"});

                DataSeries const series = {
                    {"XRP", "US1", 740, 1},
                    {"XRP", "US2", 750, 1},
                    {"XRP", "US3", 740, 1},
                    {"XRP", "US4", 750, 1},
                    {"XRP", "US5", 740, 1},
                    {"XRP", "US6", 750, 1},
                    {"XRP", "US7", 740, 1},
                    {"XRP", "US8", 750, 1},
                    {"XRP", "US9", 740, 1},
                    {"XRP", "U10", 750, 1},
                };

                DataSeries actualSeries(
                    series.begin(), series.begin() + dataSeriesSize);

                Json::Value dataSeries(Json::arrayValue);
                for (auto const& data : actualSeries)
                {
                    Json::Value priceData;
                    Json::Value price;
                    price[jss::BaseAsset] = std::get<0>(data);
                    price[jss::QuoteAsset] = std::get<1>(data);
                    price[jss::AssetPrice] = std::get<2>(data);
                    price[jss::Scale] = std::get<3>(data);
                    priceData[jss::PriceData] = price;
                    dataSeries.append(priceData);
                }
                jv[jss::PriceDataSeries] = dataSeries;
                return jv;
            };

        auto const oracleDelete = [&](Account const& account) {
            Json::Value jv;
            jv[jss::TransactionType] = jss::OracleDelete;
            jv[jss::Account] = to_string(account);
            jv[jss::OracleDocumentID] = 1;
            return jv;
        };

        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, sponsor, sponsor2);
            env.close();

            {
                // OracleSet (reserve 1)
                env(oracleSet(env, alice, 5),
                    sponsor::as(sponsor, tfSponsorReserve),
                    sponsor::sig(sponsor));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == 1);
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

                // transfer sponsor
                env(sponsor::transfer(alice, keylet::oracle(alice, 1).key),
                    sponsor::as(sponsor2, tfSponsorReserve),
                    sponsor::sig(sponsor2));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == 1);
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

                // OracleDelete
                env(oracleDelete(alice));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == 0);
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
            }
            {
                // OracleSet (reserve 2)
                env(oracleSet(env, alice, 6),
                    sponsor::as(sponsor, tfSponsorReserve),
                    sponsor::sig(sponsor));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == 2);
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 2);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 2);

                // transfer sponsor
                env(sponsor::transfer(alice, keylet::oracle(alice, 1).key),
                    sponsor::as(sponsor2, tfSponsorReserve),
                    sponsor::sig(sponsor2));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == 2);
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 2);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 2);

                // OracleDelete
                env(oracleDelete(alice));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == 0);
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
            }
            {
                // OracleSet (reserve 1->2, sponsor1 -> no-sponsor)
                env(oracleSet(env, alice, 5),
                    sponsor::as(sponsor, tfSponsorReserve),
                    sponsor::sig(sponsor));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == 1);
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

                // reserve 1->2
                env(oracleSet(env, alice, 6));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == 2);
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);

                // OracleDelete
                env(oracleDelete(alice));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == 0);
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            }
            {
                // OracleSet (reserve 1->2, sponsor1 -> sponsor2)
                env(oracleSet(env, alice, 5),
                    sponsor::as(sponsor, tfSponsorReserve),
                    sponsor::sig(sponsor));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == 1);
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

                // reserve 1->2
                env(oracleSet(env, alice, 6),
                    sponsor::as(sponsor2, tfSponsorReserve),
                    sponsor::sig(sponsor2));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == 2);
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 2);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 2);

                // OracleDelete
                env(oracleDelete(alice));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == 0);
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
            }
            {
                // OracleSet (reserve 1->2, non-sponsor -> sponsor1)
                env(oracleSet(env, alice, 5));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == 1);

                // reserve 1->2
                env(oracleSet(env, alice, 6),
                    sponsor::as(sponsor, tfSponsorReserve),
                    sponsor::sig(sponsor));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == 2);
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 2);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 2);

                // OracleDelete
                env(oracleDelete(alice));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == 0);
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            }
            for (bool isTwoOwnerCount : {false, true})
            {
                // test sponsor transfer
                auto const dataSeriesSize = isTwoOwnerCount ? 6 : 5;
                auto const ocount = isTwoOwnerCount ? 2 : 1;
                env(oracleSet(env, alice, dataSeriesSize),
                    sponsor::as(sponsor, tfSponsorReserve),
                    sponsor::sig(sponsor));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == ocount);
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == ocount);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == ocount);

                // transfer sponsor
                env(sponsor::transfer(alice, keylet::oracle(alice, 1).key),
                    sponsor::as(sponsor2, tfSponsorReserve),
                    sponsor::sig(sponsor2));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == ocount);
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == ocount);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == ocount);

                // disolve sponsor
                env(sponsor::transfer(alice, keylet::oracle(alice, 1).key));
                env.close();

                BEAST_EXPECT(ownerCount(env, alice) == ocount);
                BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
            }
        }

        {
            // check INSUFFICIENT_RESERVE for OracleSet
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, sponsor);
            env.close();

            adjustAccountXRPBalance(env, sponsor, reserve(env, 1) - drops(1));
            env(oracleSet(env, alice, 5),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor),
                ter(tecINSUFFICIENT_RESERVE));
            env.close();

            adjustAccountXRPBalance(env, sponsor, reserve(env, 2) - drops(1));
            env(oracleSet(env, alice, 6),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor),
                ter(tecINSUFFICIENT_RESERVE));
            env.close();
        }
    }

    void
    testSignerList()
    {
        testcase("SignerList");
        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const sponsor("sponsor");
        Account const sponsor2("sponsor2");

        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, sponsor, sponsor2);
            env.close();

            // SignerListSet
            env(signers(alice, 1, {{bob, 1}}),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            // transfer sponsor
            env(sponsor::transfer(alice, keylet::signers(alice).key),
                sponsor::as(sponsor2, tfSponsorReserve),
                sponsor::sig(sponsor2));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

            // Delete
            env(signers(alice, none));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 0);
        }

        {
            // check INSUFFICIENT_RESERVE for SignerListSet
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, sponsor);
            env.close();

            adjustAccountXRPBalance(env, sponsor, reserve(env, 1) - drops(1));
            env(signers(alice, 1, {{bob, 1}}),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor),
                ter(tecINSUFFICIENT_RESERVE));
            env.close();
        }
    }

    void
    testTrustSet()
    {
        testcase("TrustSet");
        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const sponsor("sponsor");
        Account const sponsor2("sponsor2");

        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, sponsor, sponsor2);
            env.close();

            auto const& highAcc = alice > bob ? alice : bob;
            auto const& lowAcc = alice > bob ? bob : alice;
            for (bool isIssuerHigh : {false, true})
            {
                auto const& issuer = isIssuerHigh ? highAcc : lowAcc;
                auto const& user = isIssuerHigh ? lowAcc : highAcc;

                auto const USD = issuer["USD"];

                // create TrustLine
                env(trust(user, USD(100)),
                    sponsor::as(sponsor, tfSponsorReserve),
                    sponsor::sig(sponsor));
                env.close();

                BEAST_EXPECT(ownerCount(env, user) == 1);
                BEAST_EXPECT(sponsoredOwnerCount(env, user) == 1);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

                auto const line =
                    env.le(keylet::line(user, issuer, USD.currency));
                BEAST_EXPECT(
                    line->getAccountID(
                        isIssuerHigh ? sfLowSponsorAccount
                                     : sfHighSponsorAccount) == sponsor.id());
                BEAST_EXPECT(!line->isFieldPresent(
                    isIssuerHigh ? sfHighSponsorAccount : sfLowSponsorAccount));

                // transfer sponsor
                env(sponsor::transfer(
                        user, keylet::line(user, issuer, USD.currency).key),
                    sponsor::as(sponsor2, tfSponsorReserve),
                    sponsor::sig(sponsor2));
                env.close();

                BEAST_EXPECT(ownerCount(env, user) == 1);
                BEAST_EXPECT(sponsoredOwnerCount(env, user) == 1);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor2) == 1);

                auto const line2 =
                    env.le(keylet::line(user, issuer, USD.currency));
                BEAST_EXPECT(
                    line2->getAccountID(
                        isIssuerHigh ? sfLowSponsorAccount
                                     : sfHighSponsorAccount) == sponsor2.id());
                BEAST_EXPECT(!line2->isFieldPresent(
                    isIssuerHigh ? sfHighSponsorAccount : sfLowSponsorAccount));

                // delete TrustLine
                env(trust(user, USD(0)));
                env.close();

                BEAST_EXPECT(ownerCount(env, user) == 0);
                BEAST_EXPECT(sponsoredOwnerCount(env, user) == 0);
                BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);

                BEAST_EXPECT(!env.le(keylet::line(user, issuer, USD.currency)));
            }
        }

        {
            // check INSUFFICIENT_RESERVE for TrustSet
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, sponsor);
            env.close();

            auto const& highAcc = alice > bob ? alice : bob;
            auto const& lowAcc = alice > bob ? bob : alice;
            for (bool isIssuerHigh : {false})
            {
                auto const& issuer = isIssuerHigh ? highAcc : lowAcc;
                auto const& user = isIssuerHigh ? lowAcc : highAcc;
                auto const USD = issuer["USD"];

                adjustAccountXRPBalance(env, sponsor, reserve(env, 100));

                // free trustline
                std::uint32_t const ticketSeq{env.seq(sponsor) + 1};
                env(ticket::create(sponsor, 2));
                env.close();

                adjustAccountXRPBalance(
                    env, sponsor, reserve(env, 3) - drops(1));

                // create TrustLine
                env(trust(user, USD(100)),
                    sponsor::as(sponsor, tfSponsorReserve),
                    sponsor::sig(sponsor),
                    ter(tecNO_LINE_INSUF_RESERVE));
                env.close();

                env(noop(sponsor), ticket::use(ticketSeq));
                env(noop(sponsor), ticket::use(ticketSeq + 1));
                env.close();
            }
        }
    }

    void
    testVault()
    {
        testcase("Vault");
        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const gw("gw");
        Account const sponsor("sponsor");
        Account const sponsor2("sponsor2");

        Asset asset = gw["IOU"].asset();

        // VaultCreate
        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, gw, sponsor);
            env.close();

            Vault vault{env};
            auto [tx, keylet] = vault.create({.owner = alice, .asset = asset});
            env(tx,
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 2);  // Vault, MPToken(share)
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 2);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 2);
            BEAST_EXPECT(
                env.le(keylet)->getAccountID(sfSponsorAccount) == sponsor.id());
        }
        // VaultDeposit
        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, gw, sponsor);
            env.close();

            Vault vault{env};
            auto [tx, keylet] = vault.create({.owner = alice, .asset = asset});
            env(tx);
            env.close();

            env(trust(bob, asset(1000)));
            env.close();
            env(pay(gw, bob, asset(1000)));
            env.close();

            BEAST_EXPECT(ownerCount(env, bob) == 1);  // RippleState

            env(vault.deposit(
                    {.depositor = bob, .id = keylet.key, .amount = asset(100)}),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            BEAST_EXPECT(
                ownerCount(env, bob) == 2);  // RippleState, MPToken(share)
            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 1);  // MPToken(share)
            BEAST_EXPECT(
                sponsoringOwnerCount(env, sponsor) == 1);  // MPToken(share)
        }
        // VaultWithdraw
        {
            // RippleState Vault
            {
                Env env{*this, testable_amendments()};
                env.fund(XRP(1000000), alice, bob, gw, sponsor);
                env.close();

                Vault vault{env};
                auto [tx, keylet] =
                    vault.create({.owner = alice, .asset = asset});
                env(tx);
                env.close();

                env(trust(bob, asset(100)));
                env.close();
                env(pay(gw, bob, asset(100)));
                env.close();

                env(vault.deposit(
                        {.depositor = bob,
                         .id = keylet.key,
                         .amount = asset(100)}),
                    sponsor::as(sponsor, tfSponsorReserve),
                    sponsor::sig(sponsor));
                env.close();

                env(trust(bob, asset(0)));  // remove trustline
                env.close();

                BEAST_EXPECT(ownerCount(env, bob) == 1);  // MPToken(share)
                BEAST_EXPECT(
                    sponsoredOwnerCount(env, bob) == 1);  // MPToken(share)
                BEAST_EXPECT(
                    sponsoringOwnerCount(env, sponsor) == 1);  // MPToken(share)

                // create Trustline
                env(vault.withdraw(
                        {.depositor = bob,
                         .id = keylet.key,
                         .amount = asset(50)}),
                    sponsor::as(sponsor, tfSponsorReserve),
                    sponsor::sig(sponsor));
                env.close();

                BEAST_EXPECT(
                    ownerCount(env, bob) == 2);  // RippleState, MPToken(share)
                BEAST_EXPECT(
                    sponsoredOwnerCount(env, bob) ==
                    2);  // RippleState, MPToken(share)
                BEAST_EXPECT(
                    sponsoringOwnerCount(env, sponsor) ==
                    2);  // RippleState, MPToken(share)

                // remove sponsored MPToken(share)
                env(vault.withdraw(
                        {.depositor = bob,
                         .id = keylet.key,
                         .amount = asset(50)}),
                    sponsor::as(sponsor, tfSponsorReserve),
                    sponsor::sig(sponsor));
                env.close();

                BEAST_EXPECT(ownerCount(env, bob) == 1);  // RippleState
                BEAST_EXPECT(
                    sponsoredOwnerCount(env, bob) == 1);  // RippleState
                BEAST_EXPECT(
                    sponsoringOwnerCount(env, sponsor) == 1);  // RippleState
            }
            // MPToken Vault
            {
                // VaultWithdraw doesn't create MPToken for depositor
            }
        }
        // VaultClawback
        {
            // remove sponsored shares MPToken
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, gw, sponsor);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();

            Vault vault{env};
            auto [tx, keylet] = vault.create({.owner = alice, .asset = asset});
            env(tx);
            env.close();

            env(trust(bob, asset(100)));
            env.close();
            env(pay(gw, bob, asset(100)));
            env.close();

            env(vault.deposit(
                    {.depositor = bob, .id = keylet.key, .amount = asset(100)}),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            BEAST_EXPECT(
                ownerCount(env, bob) == 2);  // RippleState, MPToken(share)
            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 1);  // MPToken(share)
            BEAST_EXPECT(
                sponsoringOwnerCount(env, sponsor) == 1);  // MPToken(share)

            env(vault.clawback(
                    {.issuer = gw,
                     .id = keylet.key,
                     .holder = bob,
                     .amount = asset(0)}),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, bob) == 1);  // RippleState
            BEAST_EXPECT(sponsoredOwnerCount(env, bob) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
        }
        // VaultDelete
        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, gw, sponsor);
            env.close();

            env(fset(gw, asfAllowTrustLineClawback));
            env.close();

            Vault vault{env};
            auto [tx, keylet] = vault.create({.owner = alice, .asset = asset});
            env(tx,
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 2);  // Vault, MPToken(share)
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 2);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 2);

            env(vault.del({.owner = alice, .id = keylet.key}));
            env.close();

            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);
        }
    }

    void
    testXChain()
    {
        testcase("XChain");
        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const doorA("doorA");
        Account const signer("signer");
        Account const sponsor("sponsor");

        Env env{*this, testable_amendments()};
        env.fund(XRP(1000000), alice, bob, sponsor, doorA);
        env.close();

        auto jvb = bridge(doorA, XRP, env.master, XRP);

        env(signers(doorA, 1, {signer}));
        env.close();

        // XChainCreateBridge
        {
            BEAST_EXPECT(ownerCount(env, doorA) == 1);  // SignerList
            BEAST_EXPECT(sponsoredOwnerCount(env, doorA) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 0);

            env(bridge_create(doorA, jvb, XRP(1), XRP(1)),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            BEAST_EXPECT(ownerCount(env, doorA) == 2);  // Bridge, SignerList
            BEAST_EXPECT(sponsoredOwnerCount(env, doorA) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);
        }
        // XChainCreateClaimID
        {
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);

            env(xchain_create_claim_id(alice, jvb, XRP(1), bob),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            // XChainOwnedClaimID created
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 2);
        }
        // XChainCommit
        {
            BEAST_EXPECT(ownerCount(env, alice) == 1);  // XChainOwnedClaimID
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 2);

            env(xchain_commit(alice, jvb, 1, XRP(100), bob),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            // doesn't sponsor anything
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 2);
        }
        // XChainAddClaimAttestation
        {
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 2);

            env(claim_attestation(
                    alice, jvb, bob, XRP(1), bob, false, 1, bob, signer),
                sponsor::as(sponsor, tfSponsorReserve),
                sponsor::sig(sponsor));
            env.close();

            // XChainOwnedClaimID deleted
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);
        }
        // XChainClaim
        {
            // prepare for claim
            {
                env(xchain_create_claim_id(alice, jvb, XRP(1), bob),
                    sponsor::as(sponsor, tfSponsorReserve),
                    sponsor::sig(sponsor));
                env(xchain_commit(
                    alice, jvb, 2, XRP(100)));  // omit destination
                env(claim_attestation(
                    alice,
                    jvb,
                    bob,
                    XRP(100),
                    bob,
                    false,
                    2,
                    std::nullopt,
                    signer));
                env.close();
            }

            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 1);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 2);

            env(xchain_claim(alice, jvb, 2, XRP(100), bob));
            env.close();

            // XChainOwnedClaimID deleted
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoredOwnerCount(env, alice) == 0);
            BEAST_EXPECT(sponsoringOwnerCount(env, sponsor) == 1);
        }
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
        env(sponsor::set(sponsor, 0, 100, XRP(100)),
            sponsor::sponseeAcc(alice),
            ter(tecNO_PERMISSION));
        env.close();

        // clear flag
        env(fclear(alice, asfDisallowIncomingSponsor));
        env.close();

        // Create sponsor
        env(sponsor::set(sponsor, 0, 100, XRP(100)),
            sponsor::sponseeAcc(alice),
            ter(tesSUCCESS));
        env.close();

        // set flag
        env(fset(alice, asfDisallowIncomingSponsor));
        env.close();

        // Update sponsor should success
        env(sponsor::set(sponsor, 0, 100, XRP(100)),
            sponsor::sponseeAcc(alice),
            ter(tesSUCCESS));
        env.close();

        // Delete sponsor should success
        env(sponsor::set(sponsor, tfDeleteObject),
            sponsor::sponseeAcc(alice),
            ter(tesSUCCESS));
        env.close();
    }
    void
    testAccountDelete()
    {
        testcase("AccountDelete");
        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const sponsor("sponsor");

        {
            // Delete Account with ltSponsorship
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, sponsor);
            env.close();

            // set sponsor
            env(sponsor::set(sponsor, 0, 100, XRP(100)),
                sponsor::sponseeAcc(alice),
                ter(tesSUCCESS));
            env.close();

            incLgrSeqForAccDel(env, alice);

            auto const keylet = keylet::sponsor(sponsor, alice);
            auto const sponsorObj = env.le(keylet);
            BEAST_EXPECT(sponsorObj);

            // AccountDelete
            auto const requiredFee = drops(env.current()->fees().increment);
            env(acctdelete(alice, bob), fee(requiredFee), ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(!env.le(keylet));
            auto const jv = sponsor::ledgerEntry(env, sponsor, alice);
            BEAST_EXPECT(
                jv.isObject() && jv.isMember(jss::result) &&
                jv[jss::result].isMember(jss::error) &&
                jv[jss::result][jss::error] == "entryNotFound");
        }

        {
            // Delete SponsoredAccount
            Env env{*this, testable_amendments()};
            env.memoize(alice);
            env.fund(XRP(1000000), bob, sponsor);
            env.close();

            // create SponsoredAccount
            env(pay(sponsor, alice, XRP(10000)),
                txflags(tfSponsorCreatedAccount));
            env.close();

            incLgrSeqForAccDel(env, alice);

            // AccountDelete: destination = non-sponsor
            auto const requiredFee = drops(env.current()->fees().increment);
            env(acctdelete(alice, bob),
                fee(requiredFee),
                ter(tecNO_SPONSOR_PERMISSION));

            auto const sponsorSle = env.le(keylet::account(sponsor));
            BEAST_EXPECT(
                sponsorSle->getFieldU32(sfSponsoringAccountCount) == 1);

            incLgrSeqForAccDel(env, alice);

            // AccountDelete: destination = sponsor
            env(acctdelete(alice, sponsor), fee(requiredFee), ter(tesSUCCESS));

            auto const sponsorSle2 = env.le(keylet::account(sponsor));
            BEAST_EXPECT(
                !sponsorSle2->isFieldPresent(sfSponsoringAccountCount));
        }
    }

    void
    testDelegatePermission()
    {
        testcase("DelegatePermission");
        using namespace test::jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");

        //
        // Permission SponsorFee
        //
        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, carol);
            env.close();
            auto const testFeePermission = [&](TER result) {
                // FeeAmount
                env(sponsor::set(alice, 0, std::nullopt, XRP(100)),
                    sponsor::sponseeAcc(bob),
                    delegate::as(carol),
                    ter(result));
                // MaxFee
                env(sponsor::set(
                        alice, 0, std::nullopt, std::nullopt, XRP(100)),
                    sponsor::sponseeAcc(bob),
                    delegate::as(carol),
                    ter(result));
                // SetRequireSignForFee flag
                env(sponsor::set(alice, tfSponsorshipSetRequireSignForFee),
                    sponsor::sponseeAcc(bob),
                    delegate::as(carol),
                    ter(result));
                env.close();
            };

            // no delegated
            testFeePermission(tecNO_DELEGATE_PERMISSION);

            // set non-SponsorFee Permission
            env(delegate::set(alice, carol, {"SponsorReserve"}));
            env.close();

            testFeePermission(tecNO_DELEGATE_PERMISSION);

            // set SponsorFee Permission
            env(delegate::set(alice, carol, {"SponsorFee"}));
            env.close();

            testFeePermission(tesSUCCESS);
        }

        //
        // Permission SponsorReserve
        //
        {
            Env env{*this, testable_amendments()};
            env.fund(XRP(1000000), alice, bob, carol);
            env.close();

            auto const testReservePermission = [&](TER result) {
                // ReserveCount
                env(sponsor::set(alice, 0, 100),
                    sponsor::sponseeAcc(bob),
                    delegate::as(carol),
                    ter(result));
                // SetRequireSignForReserve flag
                env(sponsor::set(alice, tfSponsorshipSetRequireSignForReserve),
                    sponsor::sponseeAcc(bob),
                    delegate::as(carol),
                    ter(result));
                env.close();
            };

            // no delegated
            testReservePermission(tecNO_DELEGATE_PERMISSION);

            // set non-SponsorReserve Permission
            env(delegate::set(alice, carol, {"SponsorFee"}));
            env.close();

            testReservePermission(tecNO_DELEGATE_PERMISSION);

            // set SponsorReserve Permission
            env(delegate::set(alice, carol, {"SponsorReserve"}));
            env.close();

            testReservePermission(tesSUCCESS);
        }
    }

    void
    testSponsorReserve()
    {
        // TODO: add checks fo InsufficientReserve for Sponsoring ledger entry
        testRequireFlag();
        testAMM();
        testCheck();
        testOffer();
        testTicket();
        testCredentials();
        testDelegate();
        testDepositPreauth();
        testDID();
        testEscrow();
        testMPToken();
        testNFToken();
        testNFTokenOffer();
        testPayChan();
        testPermissionedDomain();
        testOracle();
        testSignerList();
        testTrustSet();
        testVault();
        testXChain();
    }

    void
    run() override
    {
        testDisabled();
        testInvalidSponsorshipSet();

        testSingleSigning();
        testMultiSigning();
        // testInvalidSigninig(); // borh TxnSignature and Signers are present
        // -> error
        testSimpleSponsorshipSet();

        testTransferSponsor();
        testSponsorFee();
        testSponsorAccount();
        testSponsorReserve();
        testDisallowIncoming();

        testAccountDelete();

        testDelegatePermission();
    }
};

BEAST_DEFINE_TESTSUITE(Sponsor, app, ripple);

}  // namespace test
}  // namespace ripple

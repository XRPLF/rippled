//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2019 Ripple Labs Inc.

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
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {

class AccountDelete_test : public beast::unit_test::suite
{
private:
    std::uint32_t
    openLedgerSeq(jtx::Env& env)
    {
        return env.current()->seq();
    }

    // Helper function that verifies the expected DeliveredAmount is present.
    //
    // NOTE: the function _infers_ the transaction to operate on by calling
    // env.tx(), which returns the result from the most recent transaction.
    void
    verifyDeliveredAmount(jtx::Env& env, STAmount const& amount)
    {
        // Get the hash for the most recent transaction.
        std::string const txHash{
            env.tx()->getJson(JsonOptions::none)[jss::hash].asString()};

        // Verify DeliveredAmount and delivered_amount metadata are correct.
        // We can't use env.meta() here, because meta() doesn't include
        // delivered_amount.
        env.close();
        Json::Value const meta = env.rpc("tx", txHash)[jss::result][jss::meta];

        // Expect there to be a DeliveredAmount field.
        if (!BEAST_EXPECT(meta.isMember(sfDeliveredAmount.jsonName)))
            return;

        // DeliveredAmount and delivered_amount should both be present and
        // equal amount.
        Json::Value const jsonExpect{amount.getJson(JsonOptions::none)};
        BEAST_EXPECT(meta[sfDeliveredAmount.jsonName] == jsonExpect);
        BEAST_EXPECT(meta[jss::delivered_amount] == jsonExpect);
    }

    // Helper function to create a payment channel.
    static Json::Value
    payChanCreate(
        jtx::Account const& account,
        jtx::Account const& to,
        STAmount const& amount,
        NetClock::duration const& settleDelay,
        NetClock::time_point const& cancelAfter,
        PublicKey const& pk)
    {
        Json::Value jv;
        jv[jss::TransactionType] = jss::PaymentChannelCreate;
        jv[jss::Account] = account.human();
        jv[jss::Destination] = to.human();
        jv[jss::Amount] = amount.getJson(JsonOptions::none);
        jv[sfSettleDelay.jsonName] = settleDelay.count();
        jv[sfCancelAfter.jsonName] = cancelAfter.time_since_epoch().count() + 2;
        jv[sfPublicKey.jsonName] = strHex(pk.slice());
        return jv;
    };

    // Close the ledger until the ledger sequence is large enough to close
    // the account.  If margin is specified, close the ledger so `margin`
    // more closes are needed
    void
    incLgrSeqForAccDel(
        jtx::Env& env,
        jtx::Account const& acc,
        std::uint32_t margin = 0)
    {
        int const delta = [&]() -> int {
            if (env.seq(acc) + 255 > openLedgerSeq(env))
                return env.seq(acc) - openLedgerSeq(env) + 255 - margin;
            return 0;
        }();
        BEAST_EXPECT(margin == 0 || delta >= 0);
        for (int i = 0; i < delta; ++i)
            env.close();
        BEAST_EXPECT(openLedgerSeq(env) == env.seq(acc) + 255 - margin);
    }

public:
    void
    testBasics()
    {
        using namespace jtx;

        testcase("Basics");

        Env env{*this};
        Account const alice("alice");
        Account const becky("becky");
        Account const carol("carol");
        Account const gw("gw");

        env.fund(XRP(10000), alice, becky, carol, gw);
        env.close();

        // Alice can't delete her account and then give herself the XRP.
        env(acctdelete(alice, alice), ter(temDST_IS_SRC));

        // alice can't delete her account with a negative fee.
        env(acctdelete(alice, becky), fee(drops(-1)), ter(temBAD_FEE));

        // Invalid flags.
        env(acctdelete(alice, becky),
            txflags(tfImmediateOrCancel),
            ter(temINVALID_FLAG));

        // Account deletion has a high fee.  Make sure the fee requirement
        // behaves as we expect.
        auto const acctDelFee{drops(env.current()->fees().increment)};
        env(acctdelete(alice, becky), ter(telINSUF_FEE_P));

        // Try a fee one drop less than the required amount.
        env(acctdelete(alice, becky),
            fee(acctDelFee - drops(1)),
            ter(telINSUF_FEE_P));

        // alice's account is created too recently to be deleted.
        env(acctdelete(alice, becky), fee(acctDelFee), ter(tecTOO_SOON));

        // Give becky a trustline.  She is no longer deletable.
        env(trust(becky, gw["USD"](1000)));
        env.close();

        // Give carol a deposit preauthorization, an offer, a ticket,
        // a signer list, and a DID.  Even with all that she's still deletable.
        env(deposit::auth(carol, becky));
        std::uint32_t const carolOfferSeq{env.seq(carol)};
        env(offer(carol, gw["USD"](51), XRP(51)));
        std::uint32_t const carolTicketSeq{env.seq(carol) + 1};
        env(ticket::create(carol, 1));
        env(signers(carol, 1, {{alice, 1}, {becky, 1}}));
        env(did::setValid(carol));

        // Deleting should fail with TOO_SOON, which is a relatively
        // cheap check compared to validating the contents of her directory.
        env(acctdelete(alice, becky), fee(acctDelFee), ter(tecTOO_SOON));

        // Close enough ledgers to almost be able to delete alice's account.
        incLgrSeqForAccDel(env, alice, 1);

        // alice's account is still created too recently to be deleted.
        env(acctdelete(alice, becky), fee(acctDelFee), ter(tecTOO_SOON));

        // The most recent delete attempt advanced alice's sequence.  So
        // close two ledgers and her account should be deletable.
        env.close();
        env.close();

        {
            auto const aliceOldBalance{env.balance(alice)};
            auto const beckyOldBalance{env.balance(becky)};

            // Verify that alice's account exists but she has no directory.
            BEAST_EXPECT(env.closed()->exists(keylet::account(alice.id())));
            BEAST_EXPECT(!env.closed()->exists(keylet::ownerDir(alice.id())));

            env(acctdelete(alice, becky), fee(acctDelFee));
            verifyDeliveredAmount(env, aliceOldBalance - acctDelFee);
            env.close();

            // Verify that alice's account and directory are actually gone.
            BEAST_EXPECT(!env.closed()->exists(keylet::account(alice.id())));
            BEAST_EXPECT(!env.closed()->exists(keylet::ownerDir(alice.id())));

            // Verify that alice's XRP, minus the fee, was transferred to becky.
            BEAST_EXPECT(
                env.balance(becky) ==
                aliceOldBalance + beckyOldBalance - acctDelFee);
        }

        // Attempt to delete becky's account but get stopped by the trust line.
        env(acctdelete(becky, carol), fee(acctDelFee), ter(tecHAS_OBLIGATIONS));
        env.close();

        // Verify that becky's account is still there by giving her a regular
        // key.  This has the side effect of setting the lsfPasswordSpent bit
        // on her account root.
        Account const beck("beck");
        env(regkey(becky, beck), fee(drops(0)));
        env.close();

        // Show that the lsfPasswordSpent bit is set by attempting to change
        // becky's regular key for free again.  That fails.
        Account const reb("reb");
        env(regkey(becky, reb), sig(becky), fee(drops(0)), ter(telINSUF_FEE_P));

        // Close enough ledgers that becky's failing regkey transaction is
        // no longer retried.
        for (int i = 0; i < 8; ++i)
            env.close();

        {
            auto const beckyOldBalance{env.balance(becky)};
            auto const carolOldBalance{env.balance(carol)};

            // Verify that Carol's account, directory, deposit
            // preauthorization, offer, ticket, and signer list exist.
            BEAST_EXPECT(env.closed()->exists(keylet::account(carol.id())));
            BEAST_EXPECT(env.closed()->exists(keylet::ownerDir(carol.id())));
            BEAST_EXPECT(env.closed()->exists(
                keylet::depositPreauth(carol.id(), becky.id())));
            BEAST_EXPECT(
                env.closed()->exists(keylet::offer(carol.id(), carolOfferSeq)));
            BEAST_EXPECT(env.closed()->exists(
                keylet::ticket(carol.id(), carolTicketSeq)));
            BEAST_EXPECT(env.closed()->exists(keylet::signers(carol.id())));

            // Delete carol's account even with stuff in her directory.  Show
            // that multisigning for the delete does not increase carol's fee.
            env(acctdelete(carol, becky), fee(acctDelFee), msig(alice));
            verifyDeliveredAmount(env, carolOldBalance - acctDelFee);
            env.close();

            // Verify that Carol's account, directory, and other stuff are gone.
            BEAST_EXPECT(!env.closed()->exists(keylet::account(carol.id())));
            BEAST_EXPECT(!env.closed()->exists(keylet::ownerDir(carol.id())));
            BEAST_EXPECT(!env.closed()->exists(
                keylet::depositPreauth(carol.id(), becky.id())));
            BEAST_EXPECT(!env.closed()->exists(
                keylet::offer(carol.id(), carolOfferSeq)));
            BEAST_EXPECT(!env.closed()->exists(
                keylet::ticket(carol.id(), carolTicketSeq)));
            BEAST_EXPECT(!env.closed()->exists(keylet::signers(carol.id())));

            // Verify that Carol's XRP, minus the fee, was transferred to becky.
            BEAST_EXPECT(
                env.balance(becky) ==
                carolOldBalance + beckyOldBalance - acctDelFee);

            // Since becky received an influx of XRP, her lsfPasswordSpent bit
            // is cleared and she can change her regular key for free again.
            env(regkey(becky, reb), sig(becky), fee(drops(0)));
        }
    }

    void
    testDirectories()
    {
        // The code that deletes consecutive directory entries uses a
        // peculiarity of the implementation.  Make sure that peculiarity
        // behaves as expected across owner directory pages.
        using namespace jtx;

        testcase("Directories");

        Env env{*this};
        Account const alice("alice");
        Account const gw("gw");

        env.fund(XRP(10000), alice, gw);
        env.close();

        // Alice creates enough offers to require two owner directories.
        for (int i{0}; i < 45; ++i)
        {
            env(offer(alice, gw["USD"](1), XRP(1)));
            env.close();
        }
        env.require(offers(alice, 45));

        // Close enough ledgers to be able to delete alice's account.
        incLgrSeqForAccDel(env, alice);

        // Verify that both directory nodes exist.
        Keylet const aliceRootKey{keylet::ownerDir(alice.id())};
        Keylet const alicePageKey{keylet::page(aliceRootKey, 1)};
        BEAST_EXPECT(env.closed()->exists(aliceRootKey));
        BEAST_EXPECT(env.closed()->exists(alicePageKey));

        // Delete alice's account.
        auto const acctDelFee{drops(env.current()->fees().increment)};
        auto const aliceBalance{env.balance(alice)};
        env(acctdelete(alice, gw), fee(acctDelFee));
        verifyDeliveredAmount(env, aliceBalance - acctDelFee);
        env.close();

        // Both of alice's directory nodes should be gone.
        BEAST_EXPECT(!env.closed()->exists(aliceRootKey));
        BEAST_EXPECT(!env.closed()->exists(alicePageKey));
    }

    void
    testOwnedTypes()
    {
        using namespace jtx;

        testcase("Owned types");

        // We want to test both...
        //  o Old-style PayChannels without a recipient backlink as well as
        //  o New-styled PayChannels with the backlink.
        // So we start the test using old-style PayChannels.  Then we pass
        // the amendment to get new-style PayChannels.
        Env env{*this, supported_amendments() - fixPayChanRecipientOwnerDir};
        Account const alice("alice");
        Account const becky("becky");
        Account const gw("gw");

        env.fund(XRP(100000), alice, becky, gw);
        env.close();

        // Give alice and becky a bunch of offers that we have to search
        // through before we figure out that there's a non-deletable
        // entry in their directory.
        for (int i{0}; i < 200; ++i)
        {
            env(offer(alice, gw["USD"](1), XRP(1)));
            env(offer(becky, gw["USD"](1), XRP(1)));
            env.close();
        }
        env.require(offers(alice, 200));
        env.require(offers(becky, 200));

        // Close enough ledgers to be able to delete alice's and becky's
        // accounts.
        incLgrSeqForAccDel(env, alice);
        incLgrSeqForAccDel(env, becky);

        // alice writes a check to becky.  Until that check is cashed or
        // canceled it will prevent alice's and becky's accounts from being
        // deleted.
        uint256 const checkId = keylet::check(alice, env.seq(alice)).key;
        env(check::create(alice, becky, XRP(1)));
        env.close();

        auto const acctDelFee{drops(env.current()->fees().increment)};
        env(acctdelete(alice, gw), fee(acctDelFee), ter(tecHAS_OBLIGATIONS));
        env(acctdelete(becky, gw), fee(acctDelFee), ter(tecHAS_OBLIGATIONS));
        env.close();

        // Cancel the check, but add an escrow.  Again, with the escrow
        // on board, alice and becky should not be able to delete their
        // accounts.
        env(check::cancel(becky, checkId));
        env.close();

        // Lambda to create an escrow.
        auto escrowCreate = [](jtx::Account const& account,
                               jtx::Account const& to,
                               STAmount const& amount,
                               NetClock::time_point const& cancelAfter) {
            Json::Value jv;
            jv[jss::TransactionType] = jss::EscrowCreate;
            jv[jss::Flags] = tfUniversal;
            jv[jss::Account] = account.human();
            jv[jss::Destination] = to.human();
            jv[jss::Amount] = amount.getJson(JsonOptions::none);
            jv[sfFinishAfter.jsonName] =
                cancelAfter.time_since_epoch().count() + 1;
            jv[sfCancelAfter.jsonName] =
                cancelAfter.time_since_epoch().count() + 2;
            return jv;
        };

        using namespace std::chrono_literals;
        std::uint32_t const escrowSeq{env.seq(alice)};
        env(escrowCreate(alice, becky, XRP(333), env.now() + 2s));
        env.close();

        // alice and becky should be unable to delete their accounts because
        // of the escrow.
        env(acctdelete(alice, gw), fee(acctDelFee), ter(tecHAS_OBLIGATIONS));
        env(acctdelete(becky, gw), fee(acctDelFee), ter(tecHAS_OBLIGATIONS));
        env.close();

        // Now cancel the escrow, but create a payment channel between
        // alice and becky.

        // Lambda to cancel an escrow.
        auto escrowCancel =
            [](Account const& account, Account const& from, std::uint32_t seq) {
                Json::Value jv;
                jv[jss::TransactionType] = jss::EscrowCancel;
                jv[jss::Flags] = tfUniversal;
                jv[jss::Account] = account.human();
                jv[sfOwner.jsonName] = from.human();
                jv[sfOfferSequence.jsonName] = seq;
                return jv;
            };

        bool const withTokenEscrow =
            env.current()->rules().enabled(featureTokenEscrow);
        if (withTokenEscrow)
        {
            Account const carol("carol");
            auto const USD = gw["USD"];
            env.fund(XRP(100000), carol);
            env(fset(gw, asfAllowTokenLocking));
            env.close();
            env.trust(USD(10000), carol);
            env.close();
            env(pay(gw, carol, USD(100)));
            env.close();

            std::uint32_t const escrowSeq{env.seq(carol)};
            env(escrowCreate(carol, becky, USD(1), env.now() + 2s));
            env.close();

            env(acctdelete(gw, becky),
                fee(acctDelFee),
                ter(tecHAS_OBLIGATIONS));
            env.close();

            env(escrowCancel(becky, carol, escrowSeq));
            env.close();
        }

        env(escrowCancel(becky, alice, escrowSeq));
        env.close();

        Keylet const alicePayChanKey{
            keylet::payChan(alice, becky, env.seq(alice))};

        env(payChanCreate(
            alice, becky, XRP(57), 4s, env.now() + 2s, alice.pk()));
        env.close();

        // An old-style PayChannel does not add a back link from the
        // destination.  So with the PayChannel in place becky should be
        // able to delete her account, but alice should not.
        auto const beckyBalance{env.balance(becky)};
        env(acctdelete(alice, gw), fee(acctDelFee), ter(tecHAS_OBLIGATIONS));
        env(acctdelete(becky, gw), fee(acctDelFee));
        verifyDeliveredAmount(env, beckyBalance - acctDelFee);
        env.close();

        // Alice cancels her PayChannel which will leave her with only offers
        // in her directory.

        // Lambda to close a PayChannel.
        auto payChanClose = [](jtx::Account const& account,
                               Keylet const& payChanKeylet,
                               PublicKey const& pk) {
            Json::Value jv;
            jv[jss::TransactionType] = jss::PaymentChannelClaim;
            jv[jss::Flags] = tfClose;
            jv[jss::Account] = account.human();
            jv[sfChannel.jsonName] = to_string(payChanKeylet.key);
            jv[sfPublicKey.jsonName] = strHex(pk.slice());
            return jv;
        };
        env(payChanClose(alice, alicePayChanKey, alice.pk()));
        env.close();

        // Now enable the amendment so PayChannels add a backlink from the
        // destination.
        env.enableFeature(fixPayChanRecipientOwnerDir);
        env.close();

        // gw creates a PayChannel with alice as the destination.  With the
        // amendment passed this should prevent alice from deleting her
        // account.
        Keylet const gwPayChanKey{keylet::payChan(gw, alice, env.seq(gw))};

        env(payChanCreate(gw, alice, XRP(68), 4s, env.now() + 2s, alice.pk()));
        env.close();

        // alice can't delete her account because of the PayChannel.
        env(acctdelete(alice, gw), fee(acctDelFee), ter(tecHAS_OBLIGATIONS));
        env.close();

        // alice closes the PayChannel which should (finally) allow her to
        // delete her account.
        env(payChanClose(alice, gwPayChanKey, alice.pk()));
        env.close();

        // Now alice can successfully delete her account.
        auto const aliceBalance{env.balance(alice)};
        env(acctdelete(alice, gw), fee(acctDelFee));
        verifyDeliveredAmount(env, aliceBalance - acctDelFee);
        env.close();
    }

    void
    testResurrection()
    {
        // Create an account with an old-style PayChannel.  Delete the
        // destination of the PayChannel then resurrect the destination.
        // The PayChannel should still work.
        using namespace jtx;

        testcase("Resurrection");

        // We need an old-style PayChannel that doesn't provide a backlink
        // from the destination.  So don't enable the amendment with that fix.
        Env env{*this, supported_amendments() - fixPayChanRecipientOwnerDir};
        Account const alice("alice");
        Account const becky("becky");

        env.fund(XRP(10000), alice, becky);
        env.close();

        // Verify that becky's account root is present.
        Keylet const beckyAcctKey{keylet::account(becky.id())};
        BEAST_EXPECT(env.closed()->exists(beckyAcctKey));

        using namespace std::chrono_literals;
        Keylet const payChanKey{keylet::payChan(alice, becky, env.seq(alice))};
        auto const payChanXRP = XRP(37);

        env(payChanCreate(
            alice, becky, payChanXRP, 4s, env.now() + 1h, alice.pk()));
        env.close();
        BEAST_EXPECT(env.closed()->exists(payChanKey));

        // Close enough ledgers to be able to delete becky's account.
        incLgrSeqForAccDel(env, becky);

        auto const beckyPreDelBalance{env.balance(becky)};

        auto const acctDelFee{drops(env.current()->fees().increment)};
        env(acctdelete(becky, alice), fee(acctDelFee));
        verifyDeliveredAmount(env, beckyPreDelBalance - acctDelFee);
        env.close();

        // Verify that becky's account root is gone.
        BEAST_EXPECT(!env.closed()->exists(beckyAcctKey));

        // All it takes is a large enough XRP payment to resurrect
        // becky's account.  Try too small a payment.
        env(pay(alice,
                becky,
                drops(env.current()->fees().accountReserve(0)) - XRP(1)),
            ter(tecNO_DST_INSUF_XRP));
        env.close();

        // Actually resurrect becky's account.
        env(pay(alice, becky, XRP(10)));
        env.close();

        // becky's account root should be back.
        BEAST_EXPECT(env.closed()->exists(beckyAcctKey));
        BEAST_EXPECT(env.balance(becky) == XRP(10));

        // becky's resurrected account can be the destination of alice's
        // PayChannel.
        auto payChanClaim = [&]() {
            Json::Value jv;
            jv[jss::TransactionType] = jss::PaymentChannelClaim;
            jv[jss::Flags] = tfUniversal;
            jv[jss::Account] = alice.human();
            jv[sfChannel.jsonName] = to_string(payChanKey.key);
            jv[sfBalance.jsonName] =
                payChanXRP.value().getJson(JsonOptions::none);
            return jv;
        };
        env(payChanClaim());
        env.close();

        BEAST_EXPECT(env.balance(becky) == XRP(10) + payChanXRP);
    }

    void
    testAmendmentEnable()
    {
        // Start with the featureDeletableAccounts amendment disabled.
        // Then enable the amendment and delete an account.
        using namespace jtx;

        testcase("Amendment enable");

        Env env{*this, supported_amendments() - featureDeletableAccounts};
        Account const alice("alice");
        Account const becky("becky");

        env.fund(XRP(10000), alice, becky);
        env.close();

        // Close enough ledgers to be able to delete alice's account.
        incLgrSeqForAccDel(env, alice);

        // Verify that alice's account root is present.
        Keylet const aliceAcctKey{keylet::account(alice.id())};
        BEAST_EXPECT(env.closed()->exists(aliceAcctKey));

        auto const alicePreDelBal{env.balance(alice)};
        auto const beckyPreDelBal{env.balance(becky)};

        auto const acctDelFee{drops(env.current()->fees().increment)};
        env(acctdelete(alice, becky), fee(acctDelFee), ter(temDISABLED));
        env.close();

        // Verify that alice's account root is still present and alice and
        // becky both have their XRP.
        BEAST_EXPECT(env.current()->exists(aliceAcctKey));
        BEAST_EXPECT(env.balance(alice) == alicePreDelBal);
        BEAST_EXPECT(env.balance(becky) == beckyPreDelBal);

        // When the amendment is enabled the previous transaction is
        // retried into the new open ledger and succeeds.
        env.enableFeature(featureDeletableAccounts);
        env.close();

        // alice's account is still in the most recently closed ledger.
        BEAST_EXPECT(env.closed()->exists(aliceAcctKey));

        // Verify that alice's account root is gone from the current ledger
        // and becky has alice's XRP.
        BEAST_EXPECT(!env.current()->exists(aliceAcctKey));
        BEAST_EXPECT(
            env.balance(becky) == alicePreDelBal + beckyPreDelBal - acctDelFee);

        env.close();
        BEAST_EXPECT(!env.closed()->exists(aliceAcctKey));
    }

    void
    testTooManyOffers()
    {
        // Put enough offers in an account that we refuse to delete the account.
        using namespace jtx;

        testcase("Too many offers");

        Env env{*this};
        Account const alice("alice");
        Account const gw("gw");

        // Fund alice well so she can afford the reserve on the offers.
        env.fund(XRP(10000000), alice, gw);
        env.close();

        // To increase the number of Books affected, change the currency of
        // each offer.
        std::string currency{"AAA"};

        // Alice creates 1001 offers.  This is one greater than the number of
        // directory entries an AccountDelete will remove.
        std::uint32_t const offerSeq0{env.seq(alice)};
        constexpr int offerCount{1001};
        for (int i{0}; i < offerCount; ++i)
        {
            env(offer(alice, gw[currency](1), XRP(1)));
            env.close();

            // Increment to next currency.
            ++currency[0];
            if (currency[0] > 'Z')
            {
                currency[0] = 'A';
                ++currency[1];
            }
            if (currency[1] > 'Z')
            {
                currency[1] = 'A';
                ++currency[2];
            }
            if (currency[2] > 'Z')
            {
                currency[0] = 'A';
                currency[1] = 'A';
                currency[2] = 'A';
            }
        }

        // Close enough ledgers to be able to delete alice's account.
        incLgrSeqForAccDel(env, alice);

        // Verify the existence of the expected ledger entries.
        Keylet const aliceOwnerDirKey{keylet::ownerDir(alice.id())};
        {
            std::shared_ptr<ReadView const> closed{env.closed()};
            BEAST_EXPECT(closed->exists(keylet::account(alice.id())));
            BEAST_EXPECT(closed->exists(aliceOwnerDirKey));

            // alice's directory nodes.
            for (std::uint32_t i{0}; i < ((offerCount / 32) + 1); ++i)
                BEAST_EXPECT(closed->exists(keylet::page(aliceOwnerDirKey, i)));

            // alice's offers.
            for (std::uint32_t i{0}; i < offerCount; ++i)
                BEAST_EXPECT(
                    closed->exists(keylet::offer(alice.id(), offerSeq0 + i)));
        }

        // Delete alice's account.  Should fail because she has too many
        // offers in her directory.
        auto const acctDelFee{drops(env.current()->fees().increment)};

        env(acctdelete(alice, gw), fee(acctDelFee), ter(tefTOO_BIG));

        // Cancel one of alice's offers.  Then the account delete can succeed.
        env.require(offers(alice, offerCount));
        env(offer_cancel(alice, offerSeq0));
        env.close();
        env.require(offers(alice, offerCount - 1));

        // alice successfully deletes her account.
        auto const alicePreDelBal{env.balance(alice)};
        env(acctdelete(alice, gw), fee(acctDelFee));
        verifyDeliveredAmount(env, alicePreDelBal - acctDelFee);
        env.close();

        // Verify that alice's account root is gone as well as her directory
        // nodes and all of her offers.
        {
            std::shared_ptr<ReadView const> closed{env.closed()};
            BEAST_EXPECT(!closed->exists(keylet::account(alice.id())));
            BEAST_EXPECT(!closed->exists(aliceOwnerDirKey));

            // alice's former directory nodes.
            for (std::uint32_t i{0}; i < ((offerCount / 32) + 1); ++i)
                BEAST_EXPECT(
                    !closed->exists(keylet::page(aliceOwnerDirKey, i)));

            // alice's former offers.
            for (std::uint32_t i{0}; i < offerCount; ++i)
                BEAST_EXPECT(
                    !closed->exists(keylet::offer(alice.id(), offerSeq0 + i)));
        }
    }

    void
    testImplicitlyCreatedTrustline()
    {
        // Show that a trust line that is implicitly created by offer crossing
        // prevents an account from being deleted.
        using namespace jtx;

        testcase("Implicitly created trust line");

        Env env{*this};
        Account const alice{"alice"};
        Account const gw{"gw"};
        auto const BUX{gw["BUX"]};

        env.fund(XRP(10000), alice, gw);
        env.close();

        // alice creates an offer that, if crossed, will implicitly create
        // a trust line.
        env(offer(alice, BUX(30), XRP(30)));
        env.close();

        // gw crosses alice's offer.  alice should end up with BUX(30).
        env(offer(gw, XRP(30), BUX(30)));
        env.close();
        env.require(balance(alice, BUX(30)));

        // Close enough ledgers to be able to delete alice's account.
        incLgrSeqForAccDel(env, alice);

        // alice and gw can't delete their accounts because of the implicitly
        // created trust line.
        auto const acctDelFee{drops(env.current()->fees().increment)};
        env(acctdelete(alice, gw), fee(acctDelFee), ter(tecHAS_OBLIGATIONS));
        env.close();

        env(acctdelete(gw, alice), fee(acctDelFee), ter(tecHAS_OBLIGATIONS));
        env.close();
        {
            std::shared_ptr<ReadView const> closed{env.closed()};
            BEAST_EXPECT(closed->exists(keylet::account(alice.id())));
            BEAST_EXPECT(closed->exists(keylet::account(gw.id())));
        }
    }

    void
    testBalanceTooSmallForFee()
    {
        // See what happens when an account with a balance less than the
        // incremental reserve tries to delete itself.
        using namespace jtx;

        testcase("Balance too small for fee");

        Env env{*this};
        Account const alice("alice");

        // Note that the fee structure for unit tests does not match the fees
        // on the production network (October 2019).  Unit tests have a base
        // reserve of 200 XRP.
        env.fund(env.current()->fees().accountReserve(0), noripple(alice));
        env.close();

        // Burn a chunk of alice's funds so she only has 1 XRP remaining in
        // her account.
        env(noop(alice), fee(env.balance(alice) - XRP(1)));
        env.close();

        auto const acctDelFee{drops(env.current()->fees().increment)};
        BEAST_EXPECT(acctDelFee > env.balance(alice));

        // alice attempts to delete her account even though she can't pay
        // the full fee.  She specifies a fee that is larger than her balance.
        //
        // The balance of env.master should not change.
        auto const masterBalance{env.balance(env.master)};
        env(acctdelete(alice, env.master),
            fee(acctDelFee),
            ter(terINSUF_FEE_B));
        env.close();
        {
            std::shared_ptr<ReadView const> const closed{env.closed()};
            BEAST_EXPECT(closed->exists(keylet::account(alice.id())));
            BEAST_EXPECT(env.balance(env.master) == masterBalance);
        }

        // alice again attempts to delete her account.  This time she specifies
        // her current balance in XRP.  Again the transaction fails.
        BEAST_EXPECT(env.balance(alice) == XRP(1));
        env(acctdelete(alice, env.master), fee(XRP(1)), ter(telINSUF_FEE_P));
        env.close();
        {
            std::shared_ptr<ReadView const> closed{env.closed()};
            BEAST_EXPECT(closed->exists(keylet::account(alice.id())));
            BEAST_EXPECT(env.balance(env.master) == masterBalance);
        }
    }

    void
    testWithTickets()
    {
        testcase("With Tickets");

        using namespace test::jtx;

        Account const alice{"alice"};
        Account const bob{"bob"};

        Env env{*this};
        env.fund(XRP(100000), alice, bob);
        env.close();

        // bob grabs as many tickets as he is allowed to have.
        std::uint32_t const ticketSeq{env.seq(bob) + 1};
        env(ticket::create(bob, 250));
        env.close();
        env.require(owners(bob, 250));

        {
            std::shared_ptr<ReadView const> closed{env.closed()};
            BEAST_EXPECT(closed->exists(keylet::account(bob.id())));
            for (std::uint32_t i = 0; i < 250; ++i)
            {
                BEAST_EXPECT(
                    closed->exists(keylet::ticket(bob.id(), ticketSeq + i)));
            }
        }

        // Close enough ledgers to be able to delete bob's account.
        incLgrSeqForAccDel(env, bob);

        // bob deletes his account using a ticket.  bob's account and all
        // of his tickets should be removed from the ledger.
        auto const acctDelFee{drops(env.current()->fees().increment)};
        auto const bobOldBalance{env.balance(bob)};
        env(acctdelete(bob, alice), ticket::use(ticketSeq), fee(acctDelFee));
        verifyDeliveredAmount(env, bobOldBalance - acctDelFee);
        env.close();
        {
            std::shared_ptr<ReadView const> closed{env.closed()};
            BEAST_EXPECT(!closed->exists(keylet::account(bob.id())));
            for (std::uint32_t i = 0; i < 250; ++i)
            {
                BEAST_EXPECT(
                    !closed->exists(keylet::ticket(bob.id(), ticketSeq + i)));
            }
        }
    }

    void
    testDest()
    {
        testcase("Destination Constraints");

        using namespace test::jtx;

        Account const alice{"alice"};
        Account const becky{"becky"};
        Account const carol{"carol"};
        Account const daria{"daria"};

        Env env{*this};
        env.fund(XRP(100000), alice, becky, carol);
        env.close();

        // alice sets the lsfDepositAuth flag on her account.  This should
        // prevent becky from deleting her account while using alice as the
        // destination.
        env(fset(alice, asfDepositAuth));

        // carol requires a destination tag.
        env(fset(carol, asfRequireDest));
        env.close();

        // Close enough ledgers to be able to delete becky's account.
        incLgrSeqForAccDel(env, becky);

        // becky attempts to delete her account using daria as the destination.
        // Since daria is not in the ledger the delete attempt fails.
        auto const acctDelFee{drops(env.current()->fees().increment)};
        env(acctdelete(becky, daria), fee(acctDelFee), ter(tecNO_DST));
        env.close();

        // becky attempts to delete her account, but carol requires a
        // destination tag which becky has omitted.
        env(acctdelete(becky, carol), fee(acctDelFee), ter(tecDST_TAG_NEEDED));
        env.close();

        // becky attempts to delete her account, but alice won't take her XRP,
        // so the delete is blocked.
        env(acctdelete(becky, alice), fee(acctDelFee), ter(tecNO_PERMISSION));
        env.close();

        // alice preauthorizes deposits from becky.  Now becky can delete her
        // account and forward the leftovers to alice.
        env(deposit::auth(alice, becky));
        env.close();

        auto const beckyOldBalance{env.balance(becky)};
        env(acctdelete(becky, alice), fee(acctDelFee));
        verifyDeliveredAmount(env, beckyOldBalance - acctDelFee);
        env.close();
    }

    void
    testDestinationDepositAuthCredentials()
    {
        {
            testcase(
                "Destination Constraints with DepositPreauth and Credentials");

            using namespace test::jtx;

            Account const alice{"alice"};
            Account const becky{"becky"};
            Account const carol{"carol"};
            Account const daria{"daria"};

            const char credType[] = "abcd";

            Env env{*this};
            env.fund(XRP(100000), alice, becky, carol, daria);
            env.close();

            // carol issue credentials for becky
            env(credentials::create(becky, carol, credType));
            env.close();

            // get credentials index
            auto const jv =
                credentials::ledgerEntry(env, becky, carol, credType);
            std::string const credIdx = jv[jss::result][jss::index].asString();

            // Close enough ledgers to be able to delete becky's account.
            incLgrSeqForAccDel(env, becky);

            auto const acctDelFee{drops(env.current()->fees().increment)};

            // becky use credentials but they aren't accepted
            env(acctdelete(becky, alice),
                credentials::ids({credIdx}),
                fee(acctDelFee),
                ter(tecBAD_CREDENTIALS));
            env.close();

            {
                // alice sets the lsfDepositAuth flag on her account.  This
                // should prevent becky from deleting her account while using
                // alice as the destination.
                env(fset(alice, asfDepositAuth));
                env.close();
            }

            // Fail, credentials still not accepted
            env(acctdelete(becky, alice),
                credentials::ids({credIdx}),
                fee(acctDelFee),
                ter(tecBAD_CREDENTIALS));
            env.close();

            // becky accept the credentials
            env(credentials::accept(becky, carol, credType));
            env.close();

            // Fail, credentials doesn’t belong to carol
            env(acctdelete(carol, alice),
                credentials::ids({credIdx}),
                fee(acctDelFee),
                ter(tecBAD_CREDENTIALS));

            // Fail, no depositPreauth for provided credentials
            env(acctdelete(becky, alice),
                credentials::ids({credIdx}),
                fee(acctDelFee),
                ter(tecNO_PERMISSION));
            env.close();

            // alice create DepositPreauth Object
            env(deposit::authCredentials(alice, {{carol, credType}}));
            env.close();

            // becky attempts to delete her account, but alice won't take her
            // XRP, so the delete is blocked.
            env(acctdelete(becky, alice),
                fee(acctDelFee),
                ter(tecNO_PERMISSION));

            // becky use empty credentials and can't delete account
            env(acctdelete(becky, alice),
                fee(acctDelFee),
                credentials::ids({}),
                ter(temMALFORMED));

            // becky use bad credentials and can't delete account
            env(acctdelete(becky, alice),
                credentials::ids(
                    {"48004829F915654A81B11C4AB8218D96FED67F209B58328A72314FB6E"
                     "A288BE4"}),
                fee(acctDelFee),
                ter(tecBAD_CREDENTIALS));
            env.close();

            // becky use credentials and can delete account
            env(acctdelete(becky, alice),
                credentials::ids({credIdx}),
                fee(acctDelFee));
            env.close();

            {
                // check that credential object deleted too
                auto const jNoCred =
                    credentials::ledgerEntry(env, becky, carol, credType);
                BEAST_EXPECT(
                    jNoCred.isObject() && jNoCred.isMember(jss::result) &&
                    jNoCred[jss::result].isMember(jss::error) &&
                    jNoCred[jss::result][jss::error] == "entryNotFound");
            }

            testcase("Credentials that aren't required");
            {  // carol issue credentials for daria
                env(credentials::create(daria, carol, credType));
                env.close();
                env(credentials::accept(daria, carol, credType));
                env.close();
                std::string const credDaria =
                    credentials::ledgerEntry(
                        env, daria, carol, credType)[jss::result][jss::index]
                        .asString();

                // daria use valid credentials, which aren't required and can
                // delete her account
                env(acctdelete(daria, carol),
                    credentials::ids({credDaria}),
                    fee(acctDelFee));
                env.close();

                // check that credential object deleted too
                auto const jNoCred =
                    credentials::ledgerEntry(env, daria, carol, credType);

                BEAST_EXPECT(
                    jNoCred.isObject() && jNoCred.isMember(jss::result) &&
                    jNoCred[jss::result].isMember(jss::error) &&
                    jNoCred[jss::result][jss::error] == "entryNotFound");
            }

            {
                Account const eaton{"eaton"};
                Account const fred{"fred"};

                env.fund(XRP(5000), eaton, fred);

                // carol issue credentials for eaton
                env(credentials::create(eaton, carol, credType));
                env.close();
                env(credentials::accept(eaton, carol, credType));
                env.close();
                std::string const credEaton =
                    credentials::ledgerEntry(
                        env, eaton, carol, credType)[jss::result][jss::index]
                        .asString();

                // fred make preauthorization through authorized account
                env(fset(fred, asfDepositAuth));
                env.close();
                env(deposit::auth(fred, eaton));
                env.close();

                // Close enough ledgers to be able to delete becky's account.
                incLgrSeqForAccDel(env, eaton);
                auto const acctDelFee{drops(env.current()->fees().increment)};

                // eaton use valid credentials, but he already authorized
                // through "Authorized" field.
                env(acctdelete(eaton, fred),
                    credentials::ids({credEaton}),
                    fee(acctDelFee));
                env.close();

                // check that credential object deleted too
                auto const jNoCred =
                    credentials::ledgerEntry(env, eaton, carol, credType);

                BEAST_EXPECT(
                    jNoCred.isObject() && jNoCred.isMember(jss::result) &&
                    jNoCred[jss::result].isMember(jss::error) &&
                    jNoCred[jss::result][jss::error] == "entryNotFound");
            }

            testcase("Expired credentials");
            {
                Account const john{"john"};

                env.fund(XRP(10000), john);
                env.close();

                auto jv = credentials::create(john, carol, credType);
                uint32_t const t = env.current()
                                       ->info()
                                       .parentCloseTime.time_since_epoch()
                                       .count() +
                    20;
                jv[sfExpiration.jsonName] = t;
                env(jv);
                env.close();
                env(credentials::accept(john, carol, credType));
                env.close();
                jv = credentials::ledgerEntry(env, john, carol, credType);
                std::string const credIdx =
                    jv[jss::result][jss::index].asString();

                incLgrSeqForAccDel(env, john);

                // credentials are expired
                // john use credentials but can't delete account
                env(acctdelete(john, alice),
                    credentials::ids({credIdx}),
                    fee(acctDelFee),
                    ter(tecEXPIRED));
                env.close();

                {
                    // check that expired credential object deleted
                    auto jv =
                        credentials::ledgerEntry(env, john, carol, credType);
                    BEAST_EXPECT(
                        jv.isObject() && jv.isMember(jss::result) &&
                        jv[jss::result].isMember(jss::error) &&
                        jv[jss::result][jss::error] == "entryNotFound");
                }
            }
        }

        {
            testcase("Credentials feature disabled");
            using namespace test::jtx;

            Account const alice{"alice"};
            Account const becky{"becky"};
            Account const carol{"carol"};

            Env env{*this, supported_amendments() - featureCredentials};
            env.fund(XRP(100000), alice, becky, carol);
            env.close();

            // alice sets the lsfDepositAuth flag on her account.  This should
            // prevent becky from deleting her account while using alice as the
            // destination.
            env(fset(alice, asfDepositAuth));
            env.close();

            // Close enough ledgers to be able to delete becky's account.
            incLgrSeqForAccDel(env, becky);

            auto const acctDelFee{drops(env.current()->fees().increment)};

            std::string const credIdx =
                "098B7F1B146470A1C5084DC7832C04A72939E3EBC58E68AB8B579BA072B0CE"
                "CB";

            // and can't delete even with old DepositPreauth
            env(deposit::auth(alice, becky));
            env.close();

            env(acctdelete(becky, alice),
                credentials::ids({credIdx}),
                fee(acctDelFee),
                ter(temDISABLED));
            env.close();
        }
    }

    void
    testDeleteCredentialsOwner()
    {
        {
            testcase("Deleting Issuer deletes issued credentials");

            using namespace test::jtx;

            Account const alice{"alice"};
            Account const becky{"becky"};
            Account const carol{"carol"};

            const char credType[] = "abcd";

            Env env{*this};
            env.fund(XRP(100000), alice, becky, carol);
            env.close();

            // carol issue credentials for becky
            env(credentials::create(becky, carol, credType));
            env.close();
            env(credentials::accept(becky, carol, credType));
            env.close();

            // get credentials index
            auto const jv =
                credentials::ledgerEntry(env, becky, carol, credType);
            std::string const credIdx = jv[jss::result][jss::index].asString();

            // Close enough ledgers to be able to delete carol's account.
            incLgrSeqForAccDel(env, carol);

            auto const acctDelFee{drops(env.current()->fees().increment)};
            env(acctdelete(carol, alice), fee(acctDelFee));
            env.close();

            {  // check that credential object deleted too
                BEAST_EXPECT(!env.le(credIdx));
                auto const jv =
                    credentials::ledgerEntry(env, becky, carol, credType);
                BEAST_EXPECT(
                    jv.isObject() && jv.isMember(jss::result) &&
                    jv[jss::result].isMember(jss::error) &&
                    jv[jss::result][jss::error] == "entryNotFound");
            }
        }

        {
            testcase("Deleting Subject deletes issued credentials");

            using namespace test::jtx;

            Account const alice{"alice"};
            Account const becky{"becky"};
            Account const carol{"carol"};

            const char credType[] = "abcd";

            Env env{*this};
            env.fund(XRP(100000), alice, becky, carol);
            env.close();

            // carol issue credentials for becky
            env(credentials::create(becky, carol, credType));
            env.close();
            env(credentials::accept(becky, carol, credType));
            env.close();

            // get credentials index
            auto const jv =
                credentials::ledgerEntry(env, becky, carol, credType);
            std::string const credIdx = jv[jss::result][jss::index].asString();

            // Close enough ledgers to be able to delete carol's account.
            incLgrSeqForAccDel(env, becky);

            auto const acctDelFee{drops(env.current()->fees().increment)};
            env(acctdelete(becky, alice), fee(acctDelFee));
            env.close();

            {  // check that credential object deleted too
                BEAST_EXPECT(!env.le(credIdx));
                auto const jv =
                    credentials::ledgerEntry(env, becky, carol, credType);
                BEAST_EXPECT(
                    jv.isObject() && jv.isMember(jss::result) &&
                    jv[jss::result].isMember(jss::error) &&
                    jv[jss::result][jss::error] == "entryNotFound");
            }
        }
    }

    void
    run() override
    {
        testBasics();
        testDirectories();
        testOwnedTypes();
        testResurrection();
        testAmendmentEnable();
        testTooManyOffers();
        testImplicitlyCreatedTrustline();
        testBalanceTooSmallForFee();
        testWithTickets();
        testDest();
        testDestinationDepositAuthCredentials();
        testDeleteCredentialsOwner();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(AccountDelete, app, ripple, 2);

}  // namespace test
}  // namespace ripple

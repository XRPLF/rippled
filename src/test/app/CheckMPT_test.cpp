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

#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

class CheckMPT_test : public beast::unit_test::suite
{
    FeatureBitset const disallowIncoming{featureDisallowIncoming};

    static uint256
    getCheckIndex(AccountID const& account, std::uint32_t uSequence)
    {
        return keylet::check(account, uSequence).key;
    }

    // Helper function that returns the Checks on an account.
    static std::vector<std::shared_ptr<SLE const>>
    checksOnAccount(test::jtx::Env& env, test::jtx::Account account)
    {
        std::vector<std::shared_ptr<SLE const>> result;
        forEachItem(
            *env.current(),
            account,
            [&result](std::shared_ptr<SLE const> const& sle) {
                if (sle && sle->getType() == ltCHECK)
                    result.push_back(sle);
            });
        return result;
    }

    // Helper function that verifies the expected DeliveredAmount is present.
    //
    // NOTE: the function _infers_ the transaction to operate on by calling
    // env.tx(), which returns the result from the most recent transaction.
    void
    verifyDeliveredAmount(test::jtx::Env& env, STAmount const& amount)
    {
        // Get the hash for the most recent transaction.
        std::string const txHash{
            env.tx()->getJson(JsonOptions::none)[jss::hash].asString()};

        // Verify DeliveredAmount and delivered_amount metadata are correct.
        env.close();
        Json::Value const meta = env.rpc("tx", txHash)[jss::result][jss::meta];

        // Expect there to be a DeliveredAmount field.
        if (!BEAST_EXPECT(meta.isMember(sfDeliveredAmount.jsonName)))
            return;

        // DeliveredAmount and delivered_amount should both be present and
        // equal amount.
        BEAST_EXPECT(
            meta[sfDeliveredAmount.jsonName] ==
            amount.getJson(JsonOptions::none));
        BEAST_EXPECT(
            meta[jss::delivered_amount] == amount.getJson(JsonOptions::none));
    }

    void
    testCreateValid(FeatureBitset features)
    {
        // Explore many of the valid ways to create a check.
        testcase("Create valid");

        using namespace test::jtx;

        Account const gw{"gateway"};
        Account const alice{"alice"};
        Account const bob{"bob"};

        Env env{*this, features};

        STAmount const startBalance{XRP(1'000).value()};
        env.fund(startBalance, gw, alice, bob);

        MPT const USD = MPTTester({.env = env, .issuer = gw});

        // Note that no trust line has been set up for alice, but alice can
        // still write a check for USD.  You don't have to have the funds
        // necessary to cover a check in order to write a check.
        auto writeTwoChecks = [&env, &USD, this](
                                  Account const& from, Account const& to) {
            std::uint32_t const fromOwnerCount{ownerCount(env, from)};
            std::uint32_t const toOwnerCount{ownerCount(env, to)};

            std::size_t const fromCkCount{checksOnAccount(env, from).size()};
            std::size_t const toCkCount{checksOnAccount(env, to).size()};

            env(check::create(from, to, XRP(2000)));
            env.close();

            env(check::create(from, to, USD(50)));
            env.close();

            BEAST_EXPECT(checksOnAccount(env, from).size() == fromCkCount + 2);
            BEAST_EXPECT(checksOnAccount(env, to).size() == toCkCount + 2);

            env.require(owners(from, fromOwnerCount + 2));
            env.require(
                owners(to, to == from ? fromOwnerCount + 2 : toOwnerCount));
        };
        //  from     to
        writeTwoChecks(alice, bob);
        writeTwoChecks(gw, alice);
        writeTwoChecks(alice, gw);

        // Now try adding the various optional fields.  There's no
        // expected interaction between these optional fields; other than
        // the expiration, they are just plopped into the ledger.  So I'm
        // not looking at interactions.
        using namespace std::chrono_literals;
        std::size_t const aliceCount{checksOnAccount(env, alice).size()};
        std::size_t const bobCount{checksOnAccount(env, bob).size()};
        env(check::create(alice, bob, USD(50)), expiration(env.now() + 1s));
        env.close();

        env(check::create(alice, bob, USD(50)), source_tag(2));
        env.close();
        env(check::create(alice, bob, USD(50)), dest_tag(3));
        env.close();
        env(check::create(alice, bob, USD(50)), invoice_id(uint256{4}));
        env.close();
        env(check::create(alice, bob, USD(50)),
            expiration(env.now() + 1s),
            source_tag(12),
            dest_tag(13),
            invoice_id(uint256{4}));
        env.close();

        BEAST_EXPECT(checksOnAccount(env, alice).size() == aliceCount + 5);
        BEAST_EXPECT(checksOnAccount(env, bob).size() == bobCount + 5);

        // Use a regular key and also multisign to create a check.
        Account const alie{"alie", KeyType::ed25519};
        env(regkey(alice, alie));
        env.close();

        Account const bogie{"bogie", KeyType::secp256k1};
        Account const demon{"demon", KeyType::ed25519};
        env(signers(alice, 2, {{bogie, 1}, {demon, 1}}), sig(alie));
        env.close();

        // alice uses her regular key to create a check.
        env(check::create(alice, bob, USD(50)), sig(alie));
        env.close();
        BEAST_EXPECT(checksOnAccount(env, alice).size() == aliceCount + 6);
        BEAST_EXPECT(checksOnAccount(env, bob).size() == bobCount + 6);

        // alice uses multisigning to create a check.
        XRPAmount const baseFeeDrops{env.current()->fees().base};
        env(check::create(alice, bob, USD(50)),
            msig(bogie, demon),
            fee(3 * baseFeeDrops));
        env.close();
        BEAST_EXPECT(checksOnAccount(env, alice).size() == aliceCount + 7);
        BEAST_EXPECT(checksOnAccount(env, bob).size() == bobCount + 7);
    }

    void
    testCreateDisallowIncoming(FeatureBitset features)
    {
        testcase("Create valid with disallow incoming");

        using namespace test::jtx;

        Account const gw{"gateway"};
        Account const alice{"alice"};
        Account const bob{"bob"};

        Env env{*this, features | disallowIncoming};

        STAmount const startBalance{XRP(1'000).value()};
        env.fund(startBalance, gw, alice, bob);

        MPT const USD = MPTTester({.env = env, .issuer = gw});

        /*
         * Attempt to create two checks from `from` to `to` and
         * require they both result in error/success code `expected`
         */
        auto writeTwoChecksDI = [&env, &USD, this](
                                    Account const& from,
                                    Account const& to,
                                    TER expected) {
            std::uint32_t const fromOwnerCount{ownerCount(env, from)};
            std::uint32_t const toOwnerCount{ownerCount(env, to)};

            std::size_t const fromCkCount{checksOnAccount(env, from).size()};
            std::size_t const toCkCount{checksOnAccount(env, to).size()};

            env(check::create(from, to, XRP(2000)), ter(expected));
            env.close();

            env(check::create(from, to, USD(50)), ter(expected));
            env.close();

            if (expected == tesSUCCESS)
            {
                BEAST_EXPECT(
                    checksOnAccount(env, from).size() == fromCkCount + 2);
                BEAST_EXPECT(checksOnAccount(env, to).size() == toCkCount + 2);

                env.require(owners(from, fromOwnerCount + 2));
                env.require(
                    owners(to, to == from ? fromOwnerCount + 2 : toOwnerCount));
                return;
            }

            BEAST_EXPECT(checksOnAccount(env, from).size() == fromCkCount);
            BEAST_EXPECT(checksOnAccount(env, to).size() == toCkCount);

            env.require(owners(from, fromOwnerCount));
            env.require(owners(to, to == from ? fromOwnerCount : toOwnerCount));
        };

        // enable the DisallowIncoming flag on both bob and alice
        env(fset(bob, asfDisallowIncomingCheck));
        env(fset(alice, asfDisallowIncomingCheck));
        env.close();

        // both alice and bob can't receive checks
        writeTwoChecksDI(alice, bob, tecNO_PERMISSION);
        writeTwoChecksDI(gw, alice, tecNO_PERMISSION);

        // remove the flag from alice but not from bob
        env(fclear(alice, asfDisallowIncomingCheck));
        env.close();

        // now bob can send alice a cheque but not visa-versa
        writeTwoChecksDI(bob, alice, tesSUCCESS);
        writeTwoChecksDI(alice, bob, tecNO_PERMISSION);

        // remove bob's flag too
        env(fclear(bob, asfDisallowIncomingCheck));
        env.close();

        // now they can send checks freely
        writeTwoChecksDI(bob, alice, tesSUCCESS);
        writeTwoChecksDI(alice, bob, tesSUCCESS);
    }

    void
    testCreateInvalid(FeatureBitset features)
    {
        // Explore many of the invalid ways to create a check.
        testcase("Create invalid");

        using namespace test::jtx;

        Account const gw1{"gateway1"};
        Account const gwF{"gatewayFrozen"};
        Account const alice{"alice"};
        Account const bob{"bob"};

        Env env{*this, features};

        STAmount const startBalance{XRP(1'000).value()};
        env.fund(startBalance, gw1, gwF, alice, bob);

        auto USDM = MPTTester(
            {.env = env, .issuer = gw1, .flags = MPTDEXFlags | tfMPTCanLock});
        MPT const USD = USDM;

        // Bad fee.
        env(check::create(alice, bob, USD(50)),
            fee(drops(-10)),
            ter(temBAD_FEE));
        env.close();

        // Bad flags.
        env(check::create(alice, bob, USD(50)),
            txflags(tfImmediateOrCancel),
            ter(temINVALID_FLAG));
        env.close();

        // Check to self.
        env(check::create(alice, alice, XRP(10)), ter(temREDUNDANT));
        env.close();

        // Bad amount.
        env(check::create(alice, bob, drops(-1)), ter(temBAD_AMOUNT));
        env.close();

        env(check::create(alice, bob, drops(0)), ter(temBAD_AMOUNT));
        env.close();

        env(check::create(alice, bob, drops(1)));
        env.close();

        env(check::create(alice, bob, USD(-1)), ter(temBAD_AMOUNT));
        env.close();

        env(check::create(alice, bob, USD(0)), ter(temBAD_AMOUNT));
        env.close();

        env(check::create(alice, bob, USD(1)));
        env.close();
        {
            MPT const BAD(makeMptID(0, xrpAccount()));
            env(check::create(alice, bob, BAD(2)), ter(temBAD_CURRENCY));
            env.close();
        }

        // Bad expiration.
        env(check::create(alice, bob, USD(50)),
            expiration(NetClock::time_point{}),
            ter(temBAD_EXPIRATION));
        env.close();

        // Destination does not exist.
        Account const bogie{"bogie"};
        env(check::create(alice, bogie, USD(50)), ter(tecNO_DST));
        env.close();

        // Require destination tag.
        env(fset(bob, asfRequireDest));
        env.close();

        env(check::create(alice, bob, USD(50)), ter(tecDST_TAG_NEEDED));
        env.close();

        env(check::create(alice, bob, USD(50)), dest_tag(11));
        env.close();

        env(fclear(bob, asfRequireDest));
        env.close();
        {
            // Globally frozen asset.
            env.close();
            auto USFM = MPTTester(
                {.env = env,
                 .issuer = gwF,
                 .flags = MPTDEXFlags | tfMPTCanLock});
            MPT const USF = USFM;
            USFM.set({.flags = tfMPTLock});

            env(check::create(alice, bob, USF(50)), ter(tecFROZEN));
            env.close();

            USFM.set({.flags = tfMPTUnlock});

            env(check::create(alice, bob, USF(50)));
            env.close();
        }
        {
            // Frozen trust line.  Check creation should be similar to payment
            // behavior in the face of locked MPT.
            USDM.authorizeHolders({alice, bob});
            env(pay(gw1, alice, USD(25)));
            env(pay(gw1, bob, USD(25)));
            env.close();

            USDM.set({.holder = alice, .flags = tfMPTLock});
            // Setting MPT locked prevents alice from
            // creating a check for USD ore receiving a check. This is different
            // from IOU where alice can receive checks from bob or gw.
            env.close();
            env(check::create(alice, bob, USD(50)), ter(tecFROZEN));
            env.close();
            // Note that IOU returns tecPATH_DRY in this case.
            // IOU's internal error is terNO_LINE, which is
            // considered ter retriable and changed to tecPATH_DRY.
            env(pay(alice, bob, USD(1)), ter(tecLOCKED));
            env.close();
            env(check::create(bob, alice, USD(50)), ter(tecFROZEN));
            env.close();
            env(pay(bob, alice, USD(1)), ter(tecLOCKED));
            env.close();
            env(check::create(gw1, alice, USD(50)), ter(tecFROZEN));
            env.close();
            env(pay(gw1, alice, USD(1)));
            env.close();

            // Clear that lock.  Now check creation works.
            USDM.set({.holder = alice, .flags = tfMPTUnlock});
            env(check::create(alice, bob, USD(50)));
            env.close();
            env(check::create(bob, alice, USD(50)));
            env.close();
            env(check::create(gw1, alice, USD(50)));
            env.close();
        }

        // Expired expiration.
        env(check::create(alice, bob, USD(50)),
            expiration(env.now()),
            ter(tecEXPIRED));
        env.close();

        using namespace std::chrono_literals;
        env(check::create(alice, bob, USD(50)), expiration(env.now() + 1s));
        env.close();

        // Insufficient reserve.
        Account const cheri{"cheri"};
        env.fund(env.current()->fees().accountReserve(1) - drops(1), cheri);

        env(check::create(cheri, bob, USD(50)),
            fee(drops(env.current()->fees().base)),
            ter(tecINSUFFICIENT_RESERVE));
        env.close();

        env(pay(bob, cheri, drops(env.current()->fees().base + 1)));
        env.close();

        env(check::create(cheri, bob, USD(50)));
        env.close();
    }

    void
    testCashMPT(FeatureBitset features)
    {
        // Explore many of the valid ways to cash a check for an IOU.
        testcase("Cash MPT");

        using namespace test::jtx;

        Account const gw{"gateway"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        {
            // Simple IOU check cashed with Amount (with failures).
            Env env{*this, features};

            env.fund(XRP(1'000), gw, alice, bob);

            MPT const USD = MPTTester(
                {.env = env, .issuer = gw, .holders = {alice}, .maxAmt = 105});

            // alice writes the check before she gets the funds.
            uint256 const chkId1{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, USD(100)));
            env.close();

            // bob attempts to cash the check.  Should fail.
            env(check::cash(bob, chkId1, USD(100)), ter(tecPATH_PARTIAL));
            env.close();

            // alice gets almost enough funds.  bob tries and fails again.
            env(pay(gw, alice, USD(95)));
            env.close();
            env(check::cash(bob, chkId1, USD(100)), ter(tecPATH_PARTIAL));
            env.close();

            // alice gets the last of the necessary funds.
            env(pay(gw, alice, USD(5)));
            env.close();

            // bob for more than the check's SendMax.
            env.close();
            env(check::cash(bob, chkId1, USD(105)), ter(tecPATH_PARTIAL));
            env.close();

            // bob asks for exactly the check amount and the check clears.
            // MPT is authorized automatically
            env(check::cash(bob, chkId1, USD(100)));
            env.close();
            env.require(balance(alice, USD(0)));
            env.require(balance(bob, USD(100)));
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 0);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 0);
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 1);

            // bob tries to cash the same check again, which fails.
            env(check::cash(bob, chkId1, USD(100)), ter(tecNO_ENTRY));
            env.close();

            // bob pays alice USD(70) so he can try another case.
            env(pay(bob, alice, USD(70)));
            env.close();

            uint256 const chkId2{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, USD(70)));
            env.close();
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 1);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 1);

            // bob cashes the check for less than the face amount.  That works,
            // consumes the check, and bob receives as much as he asked for.
            env(check::cash(bob, chkId2, USD(50)));
            env.close();
            env.require(balance(alice, USD(20)));
            env.require(balance(bob, USD(80)));
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 0);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 0);
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 1);

            // alice writes two checks for USD(20), although she only has
            // USD(20).
            uint256 const chkId3{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, USD(20)));
            env.close();
            uint256 const chkId4{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, USD(20)));
            env.close();
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 2);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 2);

            // bob cashes the second check for the face amount.
            env(check::cash(bob, chkId4, USD(20)));
            env.close();
            env.require(balance(alice, USD(0)));
            env.require(balance(bob, USD(100)));
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 1);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 1);
            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(ownerCount(env, bob) == 1);

            // bob is not allowed to cash the last check for USD(0), he must
            // use check::cancel instead.
            env(check::cash(bob, chkId3, USD(0)), ter(temBAD_AMOUNT));
            env.close();
            env.require(balance(alice, USD(0)));
            env.require(balance(bob, USD(100)));
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 1);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 1);
            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(ownerCount(env, bob) == 1);

            {
                // Unlike IOU, cashing a check exceeding the MPT limit doesn't
                // work.  Show that at work.
                //
                // MPT limit is USD(105).  Show that
                // neither a payment to bob or caching can exceed that limit.

                // Payment of 200 USD fails.
                env(pay(gw, bob, USD(200)), ter(tecPATH_PARTIAL));
                env.close();

                uint256 const chkId20{getCheckIndex(gw, env.seq(gw))};
                env(check::create(gw, bob, USD(200)));
                env.close();

                // Cashing a check for 200 USD fails.
                env(check::cash(bob, chkId20, USD(200)), ter(tecPATH_PARTIAL));
                env.close();
                env.require(balance(bob, USD(100)));

                // Clean up this most recent experiment so the rest of the
                // tests work.
                env(pay(bob, gw, USD(100)));
                env(check::cancel(bob, chkId20));
            }

            // ... so bob cancels alice's remaining check.
            env(check::cancel(bob, chkId3));
            env.close();
            env.require(balance(alice, USD(0)));
            env.require(balance(bob, USD(0)));
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 0);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 0);
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 1);
        }
        {
            // Simple MPT check cashed with DeliverMin (with failures).
            Env env{*this, features};

            env.fund(XRP(1'000), gw, alice, bob);

            MPT const USD = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .maxAmt = 20});

            env(pay(gw, alice, USD(8)));
            env.close();

            // alice creates several checks ahead of time.
            uint256 const chkId9{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, USD(9)));
            env.close();
            uint256 const chkId8{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, USD(8)));
            env.close();
            uint256 const chkId7{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, USD(7)));
            env.close();
            uint256 const chkId6{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, USD(6)));
            env.close();

            // bob attempts to cash a check for the amount on the check.
            // Should fail, since alice doesn't have the funds.
            env(check::cash(bob, chkId9, check::DeliverMin(USD(9))),
                ter(tecPATH_PARTIAL));
            env.close();

            // bob sets a DeliverMin of 7 and gets all that alice has.
            env(check::cash(bob, chkId9, check::DeliverMin(USD(7))));
            verifyDeliveredAmount(env, USD(8));
            env.require(balance(alice, USD(0)));
            env.require(balance(bob, USD(8)));
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 3);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 3);
            BEAST_EXPECT(ownerCount(env, alice) == 4);
            BEAST_EXPECT(ownerCount(env, bob) == 1);

            // bob pays alice USD(7) so he can use another check.
            env(pay(bob, alice, USD(7)));
            env.close();

            // Using DeliverMin for the SendMax value of the check (and no
            // transfer fees) should work just like setting Amount.
            env(check::cash(bob, chkId7, check::DeliverMin(USD(7))));
            verifyDeliveredAmount(env, USD(7));
            env.require(balance(alice, USD(0)));
            env.require(balance(bob, USD(8)));
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 2);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 2);
            BEAST_EXPECT(ownerCount(env, alice) == 3);
            BEAST_EXPECT(ownerCount(env, bob) == 1);

            // bob pays alice USD(8) so he can use the last two checks.
            env(pay(bob, alice, USD(8)));
            env.close();

            // alice has USD(8). If bob uses the check for USD(6) and uses a
            // DeliverMin of 4, he should get the SendMax value of the check.
            env(check::cash(bob, chkId6, check::DeliverMin(USD(4))));
            verifyDeliveredAmount(env, USD(6));
            env.require(balance(alice, USD(2)));
            env.require(balance(bob, USD(6)));
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 1);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 1);
            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(ownerCount(env, bob) == 1);

            // bob cashes the last remaining check setting a DeliverMin.
            // of exactly alice's remaining USD.
            env(check::cash(bob, chkId8, check::DeliverMin(USD(2))));
            verifyDeliveredAmount(env, USD(2));
            env.require(balance(alice, USD(0)));
            env.require(balance(bob, USD(8)));
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 0);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 0);
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 1);
        }
        {
            // Examine the effects of the asfRequireAuth flag.
            Env env(*this, features);

            env.fund(XRP(1000), gw, alice, bob);
            auto USDM = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .flags = MPTDEXFlags | tfMPTRequireAuth,
                 .maxAmt = 20});
            MPT const USD = USDM;
            USDM.authorize({.holder = alice});
            env.close();
            env(pay(gw, alice, USD(8)));
            env.close();

            // alice writes a check to bob for USD.  bob can't cash it
            // because he is not authorized to hold gw["USD"].
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, USD(7)));
            env.close();

            env(check::cash(bob, chkId, USD(7)), ter(tecNO_AUTH));
            env.close();

            // Now give bob MPT for USD.  bob still can't cash the
            // check because he is not authorized.
            USDM.authorize({.account = bob});
            env.close();

            env(check::cash(bob, chkId, USD(7)), ter(tecNO_AUTH));
            env.close();

            // bob gets authorization to hold USD.
            USDM.authorize({.holder = bob});
            env.close();

            // Two possible outcomes here depending on whether cashing a
            // check can build a trust line:
            //  o If it can build a trust line, then the check is allowed to
            //    exceed the trust limit and bob gets the full transfer.
            env(check::cash(bob, chkId, check::DeliverMin(USD(4))));
            STAmount const bobGot = USD(7);
            verifyDeliveredAmount(env, bobGot);
            env.require(balance(alice, USD(8) - bobGot));
            env.require(balance(bob, bobGot));

            BEAST_EXPECT(checksOnAccount(env, alice).size() == 0);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 0);
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 1);
        }

        {
            Env env{*this, features};

            env.fund(XRP(1'000), gw, alice, bob);

            MPT const USD = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice, bob},
                 .maxAmt = 20});

            // alice creates her checks ahead of time.
            uint256 const chkId1{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, USD(1)));
            env.close();

            uint256 const chkId2{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, USD(2)));
            env.close();

            env(pay(gw, alice, USD(8)));
            env.close();

            // Give bob a regular key and signers
            Account const bobby{"bobby", KeyType::secp256k1};
            env(regkey(bob, bobby));
            env.close();

            Account const bogie{"bogie", KeyType::secp256k1};
            Account const demon{"demon", KeyType::ed25519};
            env(signers(bob, 2, {{bogie, 1}, {demon, 1}}), sig(bobby));
            env.close();

            int const signersCount = 1;
            BEAST_EXPECT(ownerCount(env, bob) == signersCount + 1);

            // bob uses his regular key to cash a check.
            env(check::cash(bob, chkId1, (USD(1))), sig(bobby));
            env.close();
            env.require(balance(alice, USD(7)));
            env.require(balance(bob, USD(1)));
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 1);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 1);
            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(ownerCount(env, bob) == signersCount + 1);

            // bob uses multisigning to cash a check.
            XRPAmount const baseFeeDrops{env.current()->fees().base};
            env(check::cash(bob, chkId2, (USD(2))),
                msig(bogie, demon),
                fee(3 * baseFeeDrops));
            env.close();
            env.require(balance(alice, USD(5)));
            env.require(balance(bob, USD(3)));
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 0);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 0);
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == signersCount + 1);
        }
    }

    void
    testCashXferFee(FeatureBitset features)
    {
        // Look at behavior when the issuer charges a transfer fee.
        testcase("Cash with transfer fee");

        using namespace test::jtx;

        Account const gw{"gateway"};
        Account const alice{"alice"};
        Account const bob{"bob"};

        Env env{*this, features};

        env.fund(XRP(1'000), gw, alice, bob);

        // Set gw's transfer rate and see the consequences when cashing a check.
        MPT const USD = MPTTester(
            {.env = env,
             .issuer = gw,
             .holders = {alice, bob},
             .transferFee = 25'000,
             .maxAmt = 1'000});

        env.close();
        env(pay(gw, alice, USD(1'000)));
        env.close();

        // alice writes a check with a SendMax of USD(125).  The most bob
        // can get is USD(100) because of the transfer rate.
        uint256 const chkId125{getCheckIndex(alice, env.seq(alice))};
        env(check::create(alice, bob, USD(125)));
        env.close();

        // alice writes another check that won't get cashed until the transfer
        // rate changes so we can see the rate applies when the check is
        // cashed, not when it is created.
#if 0
        uint256 const chkId120{getCheckIndex(alice, env.seq(alice))};
        env(check::create(alice, bob, USD(120)));
        env.close();
#endif

        // bob attempts to cash the check for face value.  Should fail.
        env(check::cash(bob, chkId125, USD(125)), ter(tecPATH_PARTIAL));
        env.close();
        env(check::cash(bob, chkId125, check::DeliverMin(USD(101))),
            ter(tecPATH_PARTIAL));
        env.close();

        // bob decides that he'll accept anything USD(75) or up.
        // He gets USD(100).
        env(check::cash(bob, chkId125, check::DeliverMin(USD(75))));
        verifyDeliveredAmount(env, USD(100));
        env.require(balance(alice, USD(1'000 - 125)));
        env.require(balance(bob, USD(0 + 100)));
        BEAST_EXPECT(checksOnAccount(env, alice).size() == 0);
        BEAST_EXPECT(checksOnAccount(env, bob).size() == 0);

#if 0
        // Adjust gw's rate...
        env(rate(gw, 1.2));
        env.close();

        // bob cashes the second check for less than the face value.  The new
        // rate applies to the actual value transferred.
        env(check::cash(bob, chkId120, USD(50)));
        env.close();
        env.require(balance(alice, USD(1000 - 125 - 60)));
        env.require(balance(bob, USD(0 + 100 + 50)));
        BEAST_EXPECT(checksOnAccount(env, alice).size() == 0);
        BEAST_EXPECT(checksOnAccount(env, bob).size() == 0);
#endif
    }

    void
    testCashInvalid(FeatureBitset features)
    {
        // Explore many of the ways to fail at cashing a check.
        testcase("Cash invalid");

        using namespace test::jtx;

        Account const gw{"gateway"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const zoe{"zoe"};

        Env env(*this, features);

        env.fund(XRP(1000), gw, alice, bob, zoe);

        auto USDM = MPTTester(
            {.env = env,
             .issuer = gw,
             .holders = {alice},
             .flags = MPTDEXFlags | tfMPTCanLock,
             .maxAmt = 20});
        MPT const USD = USDM;

        env(pay(gw, alice, USD(20)));
        env.close();

        USDM.authorize({.account = bob});

        // bob tries to cash a non-existent check from alice.
        {
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::cash(bob, chkId, USD(20)), ter(tecNO_ENTRY));
            env.close();
        }

        // alice creates her checks ahead of time.
        uint256 const chkIdU{getCheckIndex(alice, env.seq(alice))};
        env(check::create(alice, bob, USD(20)));
        env.close();

        uint256 const chkIdX{getCheckIndex(alice, env.seq(alice))};
        env(check::create(alice, bob, XRP(10)));
        env.close();

        using namespace std::chrono_literals;
        uint256 const chkIdExp{getCheckIndex(alice, env.seq(alice))};
        env(check::create(alice, bob, XRP(10)), expiration(env.now() + 1s));
        env.close();

        uint256 const chkIdFroz1{getCheckIndex(alice, env.seq(alice))};
        env(check::create(alice, bob, USD(1)));
        env.close();

        uint256 const chkIdFroz2{getCheckIndex(alice, env.seq(alice))};
        env(check::create(alice, bob, USD(2)));
        env.close();

        uint256 const chkIdFroz3{getCheckIndex(alice, env.seq(alice))};
        env(check::create(alice, bob, USD(3)));
        env.close();

#if 0
        uint256 const chkIdFroz4{getCheckIndex(alice, env.seq(alice))};
        env(check::create(alice, bob, USD(4)));
        env.close();
#endif

        uint256 const chkIdNoDest1{getCheckIndex(alice, env.seq(alice))};
        env(check::create(alice, bob, USD(1)));
        env.close();

        uint256 const chkIdHasDest2{getCheckIndex(alice, env.seq(alice))};
        env(check::create(alice, bob, USD(2)), dest_tag(7));
        env.close();

        // Same set of failing cases for both IOU and XRP check cashing.
        auto failingCases = [&env, &gw, &alice, &bob](
                                uint256 const& chkId, STAmount const& amount) {
            // Bad fee.
            env(check::cash(bob, chkId, amount),
                fee(drops(-10)),
                ter(temBAD_FEE));
            env.close();

            // Bad flags.
            env(check::cash(bob, chkId, amount),
                txflags(tfImmediateOrCancel),
                ter(temINVALID_FLAG));
            env.close();

            // Missing both Amount and DeliverMin.
            {
                Json::Value tx{check::cash(bob, chkId, amount)};
                tx.removeMember(sfAmount.jsonName);
                env(tx, ter(temMALFORMED));
                env.close();
            }
            // Both Amount and DeliverMin present.
            {
                Json::Value tx{check::cash(bob, chkId, amount)};
                tx[sfDeliverMin.jsonName] = amount.getJson(JsonOptions::none);
                env(tx, ter(temMALFORMED));
                env.close();
            }

            // Negative or zero amount.
            {
                STAmount neg{amount};
                neg.negate();
                env(check::cash(bob, chkId, neg), ter(temBAD_AMOUNT));
                env.close();
                env(check::cash(bob, chkId, amount.zeroed()),
                    ter(temBAD_AMOUNT));
                env.close();
            }

            // Bad currency.
            if (!amount.native())
            {
                Issue const badIssue{badCurrency(), amount.getIssuer()};
                STAmount badAmount{amount};
                badAmount.setIssue(Issue{badCurrency(), amount.getIssuer()});
                env(check::cash(bob, chkId, badAmount), ter(temBAD_CURRENCY));
                env.close();
            }

            // Not destination cashing check.
            env(check::cash(alice, chkId, amount), ter(tecNO_PERMISSION));
            env.close();
            env(check::cash(gw, chkId, amount), ter(tecNO_PERMISSION));
            env.close();

            // Currency mismatch.
            {
                MPT const EUR = MPTTester({.env = env, .issuer = gw});
                STAmount badAmount{EUR, amount};
                env(check::cash(bob, chkId, badAmount), ter(temMALFORMED));
                env.close();
            }

            // Issuer mismatch.
            // Every MPT is unique. There is no USD MPT with different issuers.

            // Amount bigger than SendMax.
            env(check::cash(bob, chkId, amount + amount), ter(tecPATH_PARTIAL));
            env.close();

            // DeliverMin bigger than SendMax.
            env(check::cash(bob, chkId, check::DeliverMin(amount + amount)),
                ter(tecPATH_PARTIAL));
            env.close();
        };

        failingCases(chkIdX, XRP(10));
        failingCases(chkIdU, USD(20));

        // Verify that those two checks really were cashable.
        env(check::cash(bob, chkIdU, USD(20)));
        env.close();
        env(check::cash(bob, chkIdX, check::DeliverMin(XRP(10))));
        verifyDeliveredAmount(env, XRP(10));

        // Try to cash an expired check.
        env(check::cash(bob, chkIdExp, XRP(10)), ter(tecEXPIRED));
        env.close();

        // Cancel the expired check.  Anyone can cancel an expired check.
        env(check::cancel(zoe, chkIdExp));
        env.close();

        // Can we cash a check with frozen currency?
        {
            env(pay(bob, alice, USD(20)));
            env.close();
            env.require(balance(alice, USD(20)));
            env.require(balance(bob, USD(0)));

            // Global freeze
            USDM.set({.flags = tfMPTLock});

            env(check::cash(bob, chkIdFroz1, USD(1)), ter(tecPATH_PARTIAL));
            env.close();
            env(check::cash(bob, chkIdFroz1, check::DeliverMin(USD(1))),
                ter(tecPATH_PARTIAL));
            env.close();

            USDM.set({.flags = tfMPTUnlock});

            // No longer frozen.  Success.
            env(check::cash(bob, chkIdFroz1, USD(1)));
            env.close();
            env.require(balance(alice, USD(19)));
            env.require(balance(bob, USD(1)));

            // Freeze individual trustlines.
            USDM.set({.holder = alice, .flags = tfMPTLock});
            env(check::cash(bob, chkIdFroz2, USD(2)), ter(tecPATH_PARTIAL));
            env.close();
            env(check::cash(bob, chkIdFroz2, check::DeliverMin(USD(1))),
                ter(tecPATH_PARTIAL));
            env.close();

            // Clear that freeze.  Now check cashing works.
            USDM.set({.holder = alice, .flags = tfMPTUnlock});
            env(check::cash(bob, chkIdFroz2, USD(2)));
            env.close();
            env.require(balance(alice, USD(17)));
            env.require(balance(bob, USD(3)));

            // Freeze bob's trustline.  bob can't cash the check.
            USDM.set({.holder = bob, .flags = tfMPTLock});
            env(check::cash(bob, chkIdFroz3, USD(3)), ter(tecFROZEN));
            env.close();
            env(check::cash(bob, chkIdFroz3, check::DeliverMin(USD(1))),
                ter(tecFROZEN));
            env.close();

            // Clear that freeze.  Now check cashing works again.
            USDM.set({.holder = bob, .flags = tfMPTUnlock});
            env.close();
            env(check::cash(bob, chkIdFroz3, check::DeliverMin(USD(1))));
            verifyDeliveredAmount(env, USD(3));
            env.require(balance(alice, USD(14)));
            env.require(balance(bob, USD(6)));

#if 0
            // Set bob's freeze bit in the other direction.  Check
            // cashing fails.
            env(trust(bob, USD(20), tfSetFreeze));
            env.close();
            env(check::cash(bob, chkIdFroz4, USD(4)), ter(terNO_LINE));
            env.close();
            env(check::cash(bob, chkIdFroz4, check::DeliverMin(USD(1))),
                ter(terNO_LINE));
            env.close();

            // Clear bob's freeze bit and the check should be cashable.
            env(trust(bob, USD(20), tfClearFreeze));
            env.close();
            env(check::cash(bob, chkIdFroz4, USD(4)));
            env.close();
            env.require(balance(alice, USD(10)));
            env.require(balance(bob, USD(10)));
#endif
        }
        {
            // Set the RequireDest flag on bob's account (after the check
            // was created) then cash a check without a destination tag.
            env(fset(bob, asfRequireDest));
            env.close();
            env(check::cash(bob, chkIdNoDest1, USD(1)), ter(tecDST_TAG_NEEDED));
            env.close();
            env(check::cash(bob, chkIdNoDest1, check::DeliverMin(USD(1))),
                ter(tecDST_TAG_NEEDED));
            env.close();

            // bob can cash a check with a destination tag.
            env(check::cash(bob, chkIdHasDest2, USD(2)));
            env.close();

            env.require(balance(alice, USD(12)));
            env.require(balance(bob, USD(8)));

            // Clear the RequireDest flag on bob's account so he can
            // cash the check with no DestinationTag.
            env(fclear(bob, asfRequireDest));
            env.close();
            env(check::cash(bob, chkIdNoDest1, USD(1)));
            env.close();
            env.require(balance(alice, USD(11)));
            env.require(balance(bob, USD(9)));
        }
    }

    void
    testCancelValid(FeatureBitset features)
    {
        // Explore many of the ways to cancel a check.
        testcase("Cancel valid");

        using namespace test::jtx;

        Account const gw{"gateway"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const zoe{"zoe"};

        {
            Env env{*this, features};

            env.fund(XRP(1'000), gw, alice, bob, zoe);

            MPT const USD = MPTTester({.env = env, .issuer = gw});

            // alice creates her checks ahead of time.
            // Three ordinary checks with no expiration.
            uint256 const chkId1{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, USD(10)));
            env.close();

            uint256 const chkId2{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, XRP(10)));
            env.close();

            uint256 const chkId3{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, USD(10)));
            env.close();

            // Three checks that expire in 10 minutes.
            using namespace std::chrono_literals;
            uint256 const chkIdNotExp1{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, XRP(10)),
                expiration(env.now() + 600s));
            env.close();

            uint256 const chkIdNotExp2{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, USD(10)),
                expiration(env.now() + 600s));
            env.close();

            uint256 const chkIdNotExp3{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, XRP(10)),
                expiration(env.now() + 600s));
            env.close();

            // Three checks that expire in one second.
            uint256 const chkIdExp1{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, USD(10)), expiration(env.now() + 1s));
            env.close();

            uint256 const chkIdExp2{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, XRP(10)), expiration(env.now() + 1s));
            env.close();

            uint256 const chkIdExp3{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, USD(10)), expiration(env.now() + 1s));
            env.close();

            // Two checks to cancel using a regular key and using multisigning.
            uint256 const chkIdReg{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, USD(10)));
            env.close();

            uint256 const chkIdMSig{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, XRP(10)));
            env.close();
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 11);
            BEAST_EXPECT(ownerCount(env, alice) == 11);

            // Creator, destination, and an outsider cancel the checks.
            env(check::cancel(alice, chkId1));
            env.close();
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 10);
            BEAST_EXPECT(ownerCount(env, alice) == 10);

            env(check::cancel(bob, chkId2));
            env.close();
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 9);
            BEAST_EXPECT(ownerCount(env, alice) == 9);

            env(check::cancel(zoe, chkId3), ter(tecNO_PERMISSION));
            env.close();
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 9);
            BEAST_EXPECT(ownerCount(env, alice) == 9);

            // Creator, destination, and an outsider cancel unexpired checks.
            env(check::cancel(alice, chkIdNotExp1));
            env.close();
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 8);
            BEAST_EXPECT(ownerCount(env, alice) == 8);

            env(check::cancel(bob, chkIdNotExp2));
            env.close();
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 7);
            BEAST_EXPECT(ownerCount(env, alice) == 7);

            env(check::cancel(zoe, chkIdNotExp3), ter(tecNO_PERMISSION));
            env.close();
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 7);
            BEAST_EXPECT(ownerCount(env, alice) == 7);

            // Creator, destination, and an outsider cancel expired checks.
            env(check::cancel(alice, chkIdExp1));
            env.close();
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 6);
            BEAST_EXPECT(ownerCount(env, alice) == 6);

            env(check::cancel(bob, chkIdExp2));
            env.close();
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 5);
            BEAST_EXPECT(ownerCount(env, alice) == 5);

            env(check::cancel(zoe, chkIdExp3));
            env.close();
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 4);
            BEAST_EXPECT(ownerCount(env, alice) == 4);

            // Use a regular key and also multisign to cancel checks.
            Account const alie{"alie", KeyType::ed25519};
            env(regkey(alice, alie));
            env.close();

            Account const bogie{"bogie", KeyType::secp256k1};
            Account const demon{"demon", KeyType::ed25519};
            env(signers(alice, 2, {{bogie, 1}, {demon, 1}}), sig(alie));
            env.close();

            int const signersCount{1};

            // alice uses her regular key to cancel a check.
            env(check::cancel(alice, chkIdReg), sig(alie));
            env.close();
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 3);
            BEAST_EXPECT(ownerCount(env, alice) == signersCount + 3);

            // alice uses multisigning to cancel a check.
            XRPAmount const baseFeeDrops{env.current()->fees().base};
            env(check::cancel(alice, chkIdMSig),
                msig(bogie, demon),
                fee(3 * baseFeeDrops));
            env.close();
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 2);
            BEAST_EXPECT(ownerCount(env, alice) == signersCount + 2);

            // Creator and destination cancel the remaining unexpired checks.
            env(check::cancel(alice, chkId3), sig(alice));
            env.close();
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 1);
            BEAST_EXPECT(ownerCount(env, alice) == signersCount + 1);

            env(check::cancel(bob, chkIdNotExp3));
            env.close();
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 0);
            BEAST_EXPECT(ownerCount(env, alice) == signersCount + 0);
        }
    }

    void
    testWithTickets(FeatureBitset features)
    {
        testcase("With Tickets");

        using namespace test::jtx;

        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};

        Env env{*this, features};
        env.fund(XRP(1'000), gw, alice, bob);
        env.close();

        MPT const USD = MPTTester(
            {.env = env,
             .issuer = gw,
             .holders = {alice, bob},
             .maxAmt = 1'000});

        // alice and bob grab enough tickets for all the following
        // transactions.  Note that once the tickets are acquired alice's
        // and bob's account sequence numbers should not advance.
        std::uint32_t aliceTicketSeq{env.seq(alice) + 1};
        env(ticket::create(alice, 10));
        std::uint32_t const aliceSeq{env.seq(alice)};

        std::uint32_t bobTicketSeq{env.seq(bob) + 1};
        env(ticket::create(bob, 10));
        std::uint32_t const bobSeq{env.seq(bob)};

        env.close();
        // MPT + 10 tickets
        env.require(owners(alice, 11));
        env.require(owners(bob, 11));

        env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        env.require(tickets(bob, env.seq(bob) - bobTicketSeq));
        BEAST_EXPECT(env.seq(bob) == bobSeq);

        env(pay(gw, alice, USD(900)));
        env.close();

        // alice creates four checks; two XRP, two MPT.  Bob will cash
        // one of each and cancel one of each.
        uint256 const chkIdXrp1{getCheckIndex(alice, aliceTicketSeq)};
        env(check::create(alice, bob, XRP(200)), ticket::use(aliceTicketSeq++));

        uint256 const chkIdXrp2{getCheckIndex(alice, aliceTicketSeq)};
        env(check::create(alice, bob, XRP(300)), ticket::use(aliceTicketSeq++));

        uint256 const chkIdUsd1{getCheckIndex(alice, aliceTicketSeq)};
        env(check::create(alice, bob, USD(200)), ticket::use(aliceTicketSeq++));

        uint256 const chkIdUsd2{getCheckIndex(alice, aliceTicketSeq)};
        env(check::create(alice, bob, USD(300)), ticket::use(aliceTicketSeq++));

        env.close();
        // Alice used four tickets but created four checks.
        env.require(owners(alice, 11));
        env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
        BEAST_EXPECT(checksOnAccount(env, alice).size() == 4);
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        env.require(owners(bob, 11));
        BEAST_EXPECT(env.seq(bob) == bobSeq);

        // Bob cancels two of alice's checks.
        env(check::cancel(bob, chkIdXrp1), ticket::use(bobTicketSeq++));
        env(check::cancel(bob, chkIdUsd2), ticket::use(bobTicketSeq++));
        env.close();

        env.require(owners(alice, 9));
        env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
        BEAST_EXPECT(checksOnAccount(env, alice).size() == 2);
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        env.require(owners(bob, 9));
        BEAST_EXPECT(env.seq(bob) == bobSeq);

        // Bob cashes alice's two remaining checks.
        env(check::cash(bob, chkIdXrp2, XRP(300)), ticket::use(bobTicketSeq++));
        env(check::cash(bob, chkIdUsd1, USD(200)), ticket::use(bobTicketSeq++));
        env.close();

        env.require(owners(alice, 7));
        env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
        BEAST_EXPECT(checksOnAccount(env, alice).size() == 0);
        BEAST_EXPECT(env.seq(alice) == aliceSeq);
        env.require(balance(alice, USD(700)));
        env.require(balance(alice, drops(699'999'940)));
        env.require(owners(bob, 7));
        BEAST_EXPECT(env.seq(bob) == bobSeq);
        env.require(balance(bob, USD(200)));
        env.require(balance(bob, drops(1'299'999'940)));
    }

    void
    testMPTCreation(FeatureBitset features)
    {
        // Explore automatic MPT creation when a check is cashed.

        testcase("MPT Creation");

        using namespace test::jtx;

        Env env{*this, features};

        // An account that independently tracks its owner count.
        struct AccountOwns
        {
            using iterator = hash_map<std::string, MPTTester>::iterator;
            beast::unit_test::suite& suite;
            Env& env;
            Account const acct;
            std::size_t owners;
            hash_map<std::string, MPTTester> mpts;
            bool const isIssuer;
            bool const requireAuth;

            AccountOwns(
                beast::unit_test::suite& s,
                Env& e,
                Account const& a,
                bool isIssuer_,
                bool requireAuth_ = false)
                : suite(s)
                , env(e)
                , acct(a)
                , owners(0)
                , isIssuer(isIssuer_)
                , requireAuth(requireAuth_)
            {
            }

            void
            verifyOwners(std::uint32_t line, bool print = false) const
            {
                if (print)
                    std::cout << acct.name() << " " << ownerCount(env, acct)
                              << " " << owners << std::endl;
                suite.expect(
                    ownerCount(env, acct) == owners,
                    "Owner count mismatch",
                    __FILE__,
                    line);
            }

            // Operators to make using the class more convenient.
            operator Account const() const
            {
                return acct;
            }

            operator ripple::AccountID() const
            {
                return acct.id();
            }

            /** Create MPTTester if it doesn't exist for the given MPT.
             * Increment owners if created since it creates MPTokenIssuance
             */
            MPT
            operator[](std::string const& s)
            {
                if (!isIssuer)
                    Throw<std::runtime_error>("AccountOwns: must be issuer");
                if (auto const& it = mpts.find(s); it != mpts.end())
                    return it->second[s];
                auto flags = MPTDEXFlags | tfMPTCanLock;
                if (requireAuth)
                    flags |= tfMPTRequireAuth;
                auto [it, _] = mpts.emplace(
                    s, MPTTester({.env = env, .issuer = acct, .flags = flags}));
                (void)_;
                ++owners;

                return it->second[s];
            }

            iterator
            getIt(MPT const& mpt)
            {
                if (!isIssuer)
                    Throw<std::runtime_error>(
                        "AccountOwns::set must be issuer");
                auto it = mpts.find(mpt.name);
                if (it == mpts.end())
                    Throw<std::runtime_error>(
                        "AccountOwns::set mpt doesn't exist");
                return it;
            }

            void
            set(MPT const& mpt, std::uint32_t flag)
            {
                auto it = getIt(mpt);
                it->second.set({.flags = flag});
            }

            void
            authorize(MPT const& mpt, AccountOwns& id)
            {
                auto it = getIt(mpt);
                it->second.authorize({.account = id});
                ++id.owners;
            }

            void
            cleanup(MPT const& mpt, AccountOwns& id)
            {
                auto it = getIt(mpt);
                // redeem to the issuer
                if (auto const redeem = it->second.getBalance(id))
                    pay(it, id, acct, redeem);
                // delete mptoken
                it->second.authorize(
                    {.account = id, .flags = tfMPTUnauthorize});
                --id.owners;
            }

            void
            pay(iterator& it,
                Account const& src,
                Account const& dst,
                std::uint64_t amount)
            {
                if (env.le(keylet::account(dst))->isFlag(lsfDepositAuth))
                {
                    env(fclear(dst, asfDepositAuth));
                    it->second.pay(src, dst, amount);
                    env(fset(dst, asfDepositAuth));
                }
                else
                    it->second.pay(src, dst, amount);
            }

            void
            pay(Account const& src, Account const& dst, PrettyAmount amount)
            {
                auto it = getIt(amount.name());
                pay(it, src, dst, amount.value().mpt().value());
            }
        };

        AccountOwns alice{*this, env, "alice", false};
        AccountOwns bob{*this, env, "bob", false};
        AccountOwns gw1{*this, env, "gw1", true};

        // Fund with noripple so the accounts do not have any flags set.
        env.fund(XRP(5000), noripple(alice, bob));
        env.close();

        // Automatic MPT creation should fail if the check destination
        // can't afford the reserve for the trust line.
        {
            // Fund gw1 with noripple (even though that's atypical for a
            // gateway) so it does not have any flags set.  We'll set flags
            // on gw1 later.
            env.fund(XRP(5'000), noripple(gw1));
            env.close();

            MPT const CK8 = gw1["CK8"];
            gw1.verifyOwners(__LINE__);

            Account const yui{"yui"};

            // Note the reserve in unit tests is 200 XRP, not 20.  So here
            // we're just barely giving yui enough XRP to meet the
            // account reserve.
            env.fund(XRP(200), yui);
            env.close();

            uint256 const chkId{getCheckIndex(gw1, env.seq(gw1))};
            env(check::create(gw1, yui, CK8(99)));
            env.close();

            env(check::cash(yui, chkId, CK8(99)),
                ter(tecNO_LINE_INSUF_RESERVE));
            env.close();
            alice.verifyOwners(__LINE__);

            // Give yui enough XRP to meet the trust line's reserve.  Cashing
            // the check succeeds and creates the trust line.
            env(pay(env.master, yui, XRP(51)));
            env.close();
            env(check::cash(yui, chkId, CK8(99)));
            verifyDeliveredAmount(env, CK8(99));
            env.close();
            BEAST_EXPECT(ownerCount(env, yui) == 1);

            // The automatic trust line does not take a reserve from gw1.
            // Since gw1's check was consumed it has no owners.
            gw1.verifyOwners(__LINE__);
        }

        // We'll be looking at the effects of various account root flags and
        // MPT flags.

        // Automatically create MPT using
        //   o Offers and
        //   o Check cashing

        //----------- No account root flags, check written by issuer -----------
        {
            // No account root flags on any participant.
            // Automatic trust line from issuer to destination.

            BEAST_EXPECT((*env.le(gw1))[sfFlags] == 0);
            BEAST_EXPECT((*env.le(alice))[sfFlags] == 0);
            BEAST_EXPECT((*env.le(bob))[sfFlags] == 0);

            // Use offers to automatically create MPT
            MPT const OF1 = gw1["OF1"];
            env(offer(gw1, XRP(98), OF1(98)));
            env.close();
            BEAST_EXPECT(
                env.le(keylet::mptoken(OF1.issuanceID, alice)) == nullptr);
            env(offer(alice, OF1(98), XRP(98)));
            ++alice.owners;
            env.close();

            // Both offers should be consumed.
            // Since gw1's offer was consumed and the trust line was not
            // created by gw1, gw1's owner count should be 0.
            gw1.verifyOwners(__LINE__);

            // alice's automatically created MPT bumps her owner count.
            alice.verifyOwners(__LINE__);

            // Use check cashing to automatically create the trust line.
            MPT const CK1 = gw1["CK1"];
            uint256 const chkId{getCheckIndex(gw1, env.seq(gw1))};
            env(check::create(gw1, alice, CK1(98)));
            env.close();
            BEAST_EXPECT(
                env.le(keylet::mptoken(CK1.issuanceID, alice)) == nullptr);
            env(check::cash(alice, chkId, CK1(98)));
            ++alice.owners;
            verifyDeliveredAmount(env, CK1(98));
            env.close();

            // gw1's check should be consumed.
            // Since gw1's check was consumed and the trust line was not
            // created by gw1, gw1's owner count should be 0.
            gw1.verifyOwners(__LINE__);

            // alice's automatically created trust line bumps her owner count.
            alice.verifyOwners(__LINE__);

            // cmpTrustLines(gw1, alice, OF1, CK1);
        }
        //--------- No account root flags, check written by non-issuer ---------
        {
            // No account root flags on any participant.

            // Use offers to automatically create MPT.
            // Transfer of assets using offers does not require rippling.
            // So bob's offer is successfully crossed which creates MPT.
            MPT const OF1 = gw1["OF1"];
            env(offer(alice, XRP(97), OF1(97)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(OF1, bob)) == nullptr);
            env(offer(bob, OF1(97), XRP(97)));
            ++bob.owners;
            env.close();

            // Both offers should be consumed.
            env.require(balance(alice, OF1(1)));
            env.require(balance(bob, OF1(97)));

            // bob now has an owner count of 1 due to new MPT.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Use check cashing to automatically create MPT.
            //
            // Unlike IOU where cashing a check (unlike crossing offers)
            // requires rippling through the currency's issuer, rippling doesn't
            // impact MPT. Even though gw1 does not have rippling enabled, the
            // check cash succeeds for MPT and MPT is created.
            MPT const CK1 = gw1["CK1"];
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, CK1(97)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(CK1, bob)) == nullptr);
            env(check::cash(bob, chkId, CK1(97)));
            ++bob.owners;
            env.close();

            BEAST_EXPECT(env.le(keylet::mptoken(OF1, bob)) != nullptr);

            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);
        }

        //------------- lsfDefaultRipple, check written by issuer --------------
        {
            // gw1 enables rippling.
            // This doesn't impact automatic MPT creation.
            env(fset(gw1, asfDefaultRipple));
            env.close();

            // Use offers to automatically create the trust line.
            MPT const OF2 = gw1["OF2"];
            env(offer(gw1, XRP(96), OF2(96)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(OF2, alice)) == nullptr);
            env(offer(alice, OF2(96), XRP(96)));
            ++alice.owners;
            env.close();

            // Both offers should be consumed.
            // Since gw1's offer was consumed, gw1 owner count doesn't change.
            gw1.verifyOwners(__LINE__);

            // alice's automatically created MPT bumps her owner count.
            alice.verifyOwners(__LINE__);

            // Use check cashing to automatically create MPT.
            MPT const CK2 = gw1["CK2"];
            uint256 const chkId{getCheckIndex(gw1, env.seq(gw1))};
            env(check::create(gw1, alice, CK2(96)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(CK2, alice)) == nullptr);
            env(check::cash(alice, chkId, CK2(96)));
            ++alice.owners;
            verifyDeliveredAmount(env, CK2(96));
            env.close();

            // gw1's check should be consumed.
            // Since gw1's check was consumed and MPT was not
            // created by gw1, gw1's owner count doesn't change.
            gw1.verifyOwners(__LINE__);

            // alice's automatically created trust line bumps her owner count.
            alice.verifyOwners(__LINE__);
        }

        //----------- lsfDefaultRipple, check written by non-issuer ------------
        {
            // gw1 enabled rippling doesn't impact MPT, so automatic MPT from
            // non-issuer to non-issuer should work.

            // Use offers to automatically create MPT.
            MPT const OF2 = gw1["OF2"];
            env(offer(alice, XRP(95), OF2(95)));
            env.close();
            // alice already has OF2 MPT
            BEAST_EXPECT(env.le(keylet::mptoken(OF2, alice)) != nullptr);
            env(offer(bob, OF2(95), XRP(95)));
            ++bob.owners;
            env.close();

            // bob's owner count should increase due to the new MPT.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Use check cashing to automatically create MPT.
            MPT const CK2 = gw1["CK2"];
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, CK2(95)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(CK2, bob)) == nullptr);
            env(check::cash(bob, chkId, CK2(95)));
            ++bob.owners;
            verifyDeliveredAmount(env, CK2(95));
            env.close();

            // bob's owner count should increase due to the new MPT.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);
        }

        //-------------- lsfDepositAuth, check written by issuer ---------------
        {
            // Both offers and checks ignore the lsfDepositAuth flag, since
            // the destination signs the transaction that delivers their funds.
            // So setting lsfDepositAuth on all the participants should not
            // change any outcomes.
            //
            // Automatic MPT from issuer to non-issuer should still work.
            env(fset(gw1, asfDepositAuth));
            env(fset(alice, asfDepositAuth));
            env(fset(bob, asfDepositAuth));
            env.close();

            // Use offers to automatically create MPT.
            MPT const OF3 = gw1["OF3"];
            env(offer(gw1, XRP(94), OF3(94)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(OF3, alice)) == nullptr);
            env(offer(alice, OF3(94), XRP(94)));
            ++alice.owners;
            env.close();

            // Both offers should be consumed.
            // Since gw1's offer was consumed and MPT was not
            // created by gw1, gw1's owner count doesn't change.
            gw1.verifyOwners(__LINE__);

            // alice's automatically created MPT bumps her owner count.
            alice.verifyOwners(__LINE__);

            // Use check cashing to automatically create MPT.
            MPT const CK3 = gw1["CK3"];
            uint256 const chkId{getCheckIndex(gw1, env.seq(gw1))};
            env(check::create(gw1, alice, CK3(94)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(CK3, alice)) == nullptr);
            env(check::cash(alice, chkId, CK3(94)));
            ++alice.owners;
            verifyDeliveredAmount(env, CK3(94));
            env.close();

            // gw1's check should be consumed.
            // Since gw1's check was consumed and MPT was not
            // created by gw1, gw1's owner count doesn't change.
            gw1.verifyOwners(__LINE__);

            // alice's automatically created trust line bumps her owner count.
            alice.verifyOwners(__LINE__);
        }

        //------------ lsfDepositAuth, check written by non-issuer -------------
        {
            // The presence of the lsfDepositAuth flag should not affect
            // automatic MPT creation.

            // Use offers to automatically create MPT.
            MPT const OF3 = gw1["OF3"];
            env(offer(alice, XRP(93), OF3(93)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(OF3, alice)) != nullptr);
            env(offer(bob, OF3(93), XRP(93)));
            ++bob.owners;
            env.close();

            // bob's owner count should increase due to the new MPT.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Use check cashing to automatically create MPT.
            MPT const CK3 = gw1["CK3"];
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, CK3(93)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(CK3, bob)) == nullptr);
            env(check::cash(bob, chkId, CK3(93)));
            ++bob.owners;
            verifyDeliveredAmount(env, CK3(93));
            env.close();

            // bob's owner count should increase due to the new MPT.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);
        }

        //-------------- lsfGlobalFreeze, check written by issuer --------------
        {
            // Set lsfGlobalFreeze on gw1.  That should not stop any automatic
            // MPT from being created.
            env(fset(gw1, asfGlobalFreeze));
            env.close();

            // Use offers to automatically create MPT.
            MPT const OF4 = gw1["OF4"];
            env(offer(gw1, XRP(92), OF4(92)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(OF4, alice)) == nullptr);
            env(offer(alice, OF4(92), XRP(92)));
            ++alice.owners;
            env.close();

            // alice's owner count should increase do to the new MPT.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Use check cashing to automatically create MPT.
            MPT const CK4 = gw1["CK4"];
            uint256 const chkId{getCheckIndex(gw1, env.seq(gw1))};
            env(check::create(gw1, bob, CK4(92)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(CK4, bob)) == nullptr);
            env(check::cash(bob, chkId, CK4(92)));
            verifyDeliveredAmount(env, CK4(92));
            ++bob.owners;
            env.close();

            // bob's owner count should increase due to the new MPT.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // clean up
            gw1.cleanup(OF4, alice);
            gw1.cleanup(CK4, bob);
        }

        //-------------- lsfMPTLock, check written by issuer --------------
        {
            // Set lsfMPTLock on gw1.  That should stop any automatic
            // MPT from being created.

            // Use offers to automatically create MPT.
            MPT const OF4 = gw1["OF4"];
            gw1.set(OF4, tfMPTLock);
            env(offer(gw1, XRP(92), OF4(92)), ter(tecFROZEN));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(OF4, alice)) == nullptr);
            env(offer(alice, OF4(92), XRP(92)), ter(tecFROZEN));
            env.close();

            // No one's owner count should have changed.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Use check cashing to automatically create MPT.
            MPT const CK4 = gw1["CK4"];
            gw1.set(CK4, tfMPTLock);
            uint256 const chkId{getCheckIndex(gw1, env.seq(gw1))};
            env(check::create(gw1, alice, CK4(92)), ter(tecFROZEN));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(CK4, alice)) == nullptr);
            env(check::cash(alice, chkId, CK4(92)), ter(tecNO_ENTRY));
            env.close();

            // No one's owner count should have changed.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Because gw1 has set tfMPTLock, neither MPT
            // is created.
            BEAST_EXPECT(env.le(keylet::mptoken(OF4, alice)) == nullptr);
            BEAST_EXPECT(env.le(keylet::mptoken(CK4, alice)) == nullptr);

            // clear global freeze
            gw1.set(OF4, tfMPTUnlock);
            gw1.set(CK4, tfMPTUnlock);
        }

        //------------ lsfGlobalFreeze, check written by non-issuer ------------
        {
            // lsfGlobalFreeze flag set on gw1 should not stop
            // automatic MPT creation between non-issuers.

            // Use offers to automatically create MPT.
            MPT const OF4 = gw1["OF4"];
            gw1.authorize(OF4, alice);
            gw1.pay(gw1, alice, OF4(91));
            env(offer(alice, XRP(91), OF4(91)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(OF4, alice)) != nullptr);
            env(offer(bob, OF4(91), XRP(91)));
            ++bob.owners;
            env.close();

            // alice's owner count should increase since it created MPT.
            // bob's owner count should increase due to the new MPT.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Use check cashing to automatically create the trust line.
            MPT const CK4 = gw1["CK4"];
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, CK4(91)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(CK4, bob)) == nullptr);
            gw1.authorize(CK4, alice);
            gw1.pay(gw1, alice, CK4(91));
            env(check::cash(bob, chkId, CK4(91)));
            ++bob.owners;
            env.close();

            // alice's owner count should increase since it created MPT.
            // bob's owner count should increase due to the new MPT.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // cleanup
            gw1.cleanup(OF4, alice);
            gw1.cleanup(CK4, alice);
            gw1.cleanup(OF4, bob);
            gw1.cleanup(CK4, bob);
        }

        //------------ lsfMPTLock, check written by non-issuer ------------
        {
            // Since gw1 has the lsfMPTLock flag set, there should be
            // no automatic MPT creation between non-issuers.

            // Use offers to automatically create MPT.
            MPT const OF4 = gw1["OF4"];
            gw1.set(OF4, tfMPTLock);
            env(offer(alice, XRP(91), OF4(91)), ter(tecFROZEN));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(OF4, alice)) == nullptr);
            env(offer(bob, OF4(91), XRP(91)), ter(tecFROZEN));
            env.close();

            // No one's owner count should have changed.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Use check cashing to automatically create the trust line.
            MPT const CK4 = gw1["CK4"];
            gw1.set(CK4, tfMPTLock);
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, CK4(91)), ter(tecFROZEN));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(CK4, bob)) == nullptr);
            env(check::cash(bob, chkId, CK4(91)), ter(tecNO_ENTRY));
            env.close();

            // No one's owner count should have changed.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Because gw1 has set lsfGlobalFreeze, neither trust line
            // is created.
            BEAST_EXPECT(env.le(keylet::mptoken(OF4, bob)) == nullptr);
            BEAST_EXPECT(env.le(keylet::mptoken(CK4, bob)) == nullptr);

            gw1.set(OF4, tfMPTUnlock);
            gw1.set(CK4, tfMPTUnlock);
        }

        //-------------- lsfRequireAuth, check written by issuer ---------------

        // We want to test the lsfRequireAuth flag, but we can't set that
        // flag on an account that already has MPT. So we'll fund
        // a new gateway and use that.
        AccountOwns gw2{*this, env, "gw2", true};
        {
            env.fund(XRP(5'000), gw2);
            env.close();

            // Set lsfRequireAuth on gw2.  That should not stop any automatic
            // MPT from being created.
            env(fset(gw2, asfRequireAuth));
            env.close();

            // Use offers to automatically create MPT.
            MPT const OF5 = gw2["OF5"];
            env(offer(gw2, XRP(92), OF5(92)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(OF5, alice)) == nullptr);
            env(offer(alice, OF5(92), XRP(92)));
            ++alice.owners;
            env.close();

            // alice's owner count should increase due to the new MPT.
            gw2.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Use check cashing to automatically create MPT.
            MPT const CK5 = gw2["CK5"];
            uint256 const chkId{getCheckIndex(gw2, env.seq(gw2))};
            env(check::create(gw2, alice, CK5(92)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(CK5, alice)) == nullptr);
            env(check::cash(alice, chkId, CK5(92)));
            verifyDeliveredAmount(env, CK5(92));
            ++alice.owners;
            env.close();

            // alice's owner count should increase due to the new MPT.
            gw2.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // cleanup
            gw2.cleanup(OF5, alice);
            gw2.cleanup(CK5, alice);
        }

        // Fund new gw to test since gw2 has MPTokenIssuance already created.
        // Set RequireAuth flag.
        AccountOwns gw3{*this, env, "gw3", true, true};
        {
            env.fund(XRP(5'000), gw3);
            env.close();
            // Use offers to automatically create the trust line.
            MPT const OF5 = gw3["OF5"];
            std::uint32_t gw3OfferSeq = {env.seq(gw3)};
            env(offer(gw3, XRP(92), OF5(92)));
            ++gw3.owners;
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(OF5, alice)) == nullptr);
            env(offer(alice, OF5(92), XRP(92)), ter(tecNO_AUTH));
            env.close();

            // gw3 should still own the offer, but no one else's owner
            // count should have changed.
            gw3.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Since we don't need it anymore, remove gw3's offer.
            env(offer_cancel(gw3, gw3OfferSeq));
            --gw3.owners;
            env.close();
            gw3.verifyOwners(__LINE__);

            // Use check cashing to automatically create the trust line.
            MPT const CK5 = gw3["CK5"];
            uint256 const chkId{getCheckIndex(gw3, env.seq(gw3))};
            env(check::create(gw3, alice, CK5(92)));
            ++gw3.owners;
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(CK5, alice)) == nullptr);
            env(check::cash(alice, chkId, CK5(92)), ter(tecNO_AUTH));
            env.close();

            // gw3 should still own the check, but no one else's owner
            // count should have changed.
            gw3.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Because gw3 has set lsfRequireAuth, neither trust line
            // is created.
            BEAST_EXPECT(env.le(keylet::mptoken(OF5, alice)) == nullptr);
            BEAST_EXPECT(env.le(keylet::mptoken(CK5, alice)) == nullptr);

            // Since we don't need it anymore, remove gw3's check.
            env(check::cancel(gw3, chkId));
            --gw3.owners;
            env.close();
            gw3.verifyOwners(__LINE__);
        }

        //------------ lsfRequireAuth, check written by non-issuer -------------
        {
            // gw2 lsfRequireAuth flag set should not affect
            // automatic MPT creation between non-issuers.

            // Use offers to automatically create MPT.
            MPT const OF5 = gw2["OF5"];
            gw2.authorize(OF5, alice);
            gw2.pay(gw2, alice, OF5(91));
            env(offer(alice, XRP(91), OF5(91)));
            env.close();
            env(offer(bob, OF5(91), XRP(91)));
            ++bob.owners;
            env.close();

            // bob's owner count should increase due to the new MPT.
            gw2.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Use check cashing to automatically create the trust line.
            MPT const CK5 = gw2["CK5"];
            gw2.authorize(CK5, alice);
            gw2.pay(gw2, alice, CK5(91));
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, CK5(91)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(CK5, bob)) == nullptr);
            env(check::cash(bob, chkId, CK5(91)));
            ++bob.owners;
            env.close();

            // bob's owner count should increase due to the new MPT.
            gw2.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);
        }

        //------------ lsfMPTRequireAuth, check written by non-issuer
        //-------------
        {
            // Since gw3 has the lsfMPTRequireAuth flag set, there should be
            // no automatic MPT creation between non-issuers.

            // Use offers to automatically create the trust line.
            MPT const OF5 = gw3["OF5"];
            env(offer(alice, XRP(91), OF5(91)), ter(tecUNFUNDED_OFFER));
            env.close();
            env(offer(bob, OF5(91), XRP(91)), ter(tecNO_AUTH));
            BEAST_EXPECT(env.le(keylet::mptoken(OF5, bob)) == nullptr);
            env.close();

            gw3.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Use check cashing to automatically create the trust line.
            MPT const CK5 = gw3["CK5"];
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, CK5(91)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(CK5, bob)) == nullptr);
            env(check::cash(bob, chkId, CK5(91)), ter(tecPATH_PARTIAL));
            env.close();

            // Delete alice's check since it is no longer needed.
            env(check::cancel(alice, chkId));
            env.close();

            // No one's owner count should have changed.
            gw3.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Because gw3 has set lsfRequireAuth, neither trust line
            // is created.
            BEAST_EXPECT(env.le(keylet::mptoken(OF5, bob)) == nullptr);
            BEAST_EXPECT(env.le(keylet::mptoken(CK5, bob)) == nullptr);
        }
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testCreateValid(features);
        testCreateDisallowIncoming(features);
        testCreateInvalid(features);
        testCashMPT(features);
        testCashXferFee(features);
        testCashInvalid(features);
        testCancelValid(features);
        testWithTickets(features);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        auto const sa = supported_amendments();
        testWithFeats(sa);

        testMPTCreation(sa);
    }
};

BEAST_DEFINE_TESTSUITE(CheckMPT, tx, ripple);

}  // namespace ripple

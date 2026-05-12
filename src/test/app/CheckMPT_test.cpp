
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>
#include <test/jtx/check.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/invoice_id.h>
#include <test/jtx/mpt.h>
#include <test/jtx/multisign.h>
#include <test/jtx/offer.h>
#include <test/jtx/owners.h>
#include <test/jtx/pay.h>
#include <test/jtx/regkey.h>
#include <test/jtx/sig.h>
#include <test/jtx/ter.h>
#include <test/jtx/ticket.h>
#include <test/jtx/txflags.h>

#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace xrpl {

class CheckMPT_test : public beast::unit_test::Suite
{
    // Helper function that returns the Checks on an account.
    static std::vector<std::shared_ptr<SLE const>>
    checksOnAccount(test::jtx::Env& env, test::jtx::Account account)
    {
        std::vector<std::shared_ptr<SLE const>> result;
        forEachItem(*env.current(), account, [&result](std::shared_ptr<SLE const> const& sle) {
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
            env.tx()->getJson(JsonOptions::Values::None)[jss::hash].asString()};

        // Verify DeliveredAmount and delivered_amount metadata are correct.
        env.close();
        json::Value const meta = env.rpc("tx", txHash)[jss::result][jss::meta];

        // Expect there to be a DeliveredAmount field.
        if (!BEAST_EXPECT(meta.isMember(sfDeliveredAmount.jsonName)))
            return;

        // DeliveredAmount and delivered_amount should both be present and
        // equal amount.
        BEAST_EXPECT(meta[sfDeliveredAmount.jsonName] == amount.getJson(JsonOptions::Values::None));
        BEAST_EXPECT(meta[jss::delivered_amount] == amount.getJson(JsonOptions::Values::None));
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

        MPT const usd = MPTTester({.env = env, .issuer = gw});

        // Note that no MPToken has been set up for alice, but alice can
        // still write a check for USD.  You don't have to have the funds
        // necessary to cover a check in order to write a check.
        auto writeTwoChecks = [&env, &usd, this](Account const& from, Account const& to) {
            std::uint32_t const fromOwnerCount{ownerCount(env, from)};
            std::uint32_t const toOwnerCount{ownerCount(env, to)};

            std::size_t const fromCkCount{checksOnAccount(env, from).size()};
            std::size_t const toCkCount{checksOnAccount(env, to).size()};

            env(check::create(from, to, XRP(2000)));
            env.close();

            env(check::create(from, to, usd(50)));
            env.close();

            BEAST_EXPECT(checksOnAccount(env, from).size() == fromCkCount + 2);
            BEAST_EXPECT(checksOnAccount(env, to).size() == toCkCount + 2);

            env.require(Owners(from, fromOwnerCount + 2));
            env.require(Owners(to, to == from ? fromOwnerCount + 2 : toOwnerCount));
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
        env(check::create(alice, bob, usd(50)), Expiration(env.now() + 1s));
        env.close();

        env(check::create(alice, bob, usd(50)), SourceTag(2));
        env.close();
        env(check::create(alice, bob, usd(50)), DestTag(3));
        env.close();
        env(check::create(alice, bob, usd(50)), InvoiceId(uint256{4}));
        env.close();
        env(check::create(alice, bob, usd(50)),
            Expiration(env.now() + 1s),
            SourceTag(12),
            DestTag(13),
            InvoiceId(uint256{4}));
        env.close();

        BEAST_EXPECT(checksOnAccount(env, alice).size() == aliceCount + 5);
        BEAST_EXPECT(checksOnAccount(env, bob).size() == bobCount + 5);

        // Use a regular key and also multisign to create a check.
        Account const alie{"alie", KeyType::Ed25519};
        env(regkey(alice, alie));
        env.close();

        Account const bogie{"bogie", KeyType::Secp256k1};
        Account const demon{"demon", KeyType::Ed25519};
        env(signers(alice, 2, {{bogie, 1}, {demon, 1}}), Sig(alie));
        env.close();

        // alice uses her regular key to create a check.
        env(check::create(alice, bob, usd(50)), Sig(alie));
        env.close();
        BEAST_EXPECT(checksOnAccount(env, alice).size() == aliceCount + 6);
        BEAST_EXPECT(checksOnAccount(env, bob).size() == bobCount + 6);

        // alice uses multisigning to create a check.
        XRPAmount const baseFeeDrops{env.current()->fees().base};
        env(check::create(alice, bob, usd(50)), Msig(bogie, demon), Fee(3 * baseFeeDrops));
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

        Env env{*this, features};

        STAmount const startBalance{XRP(1'000).value()};
        env.fund(startBalance, gw, alice, bob);

        MPT const usd = MPTTester({.env = env, .issuer = gw});

        /*
         * Attempt to create two checks from `from` to `to` and
         * require they both result in error/success code `expected`
         */
        auto writeTwoChecksDI = [&env, &usd, this](
                                    Account const& from, Account const& to, TER expected) {
            std::uint32_t const fromOwnerCount{ownerCount(env, from)};
            std::uint32_t const toOwnerCount{ownerCount(env, to)};

            std::size_t const fromCkCount{checksOnAccount(env, from).size()};
            std::size_t const toCkCount{checksOnAccount(env, to).size()};

            env(check::create(from, to, XRP(2000)), Ter(expected));
            env.close();

            env(check::create(from, to, usd(50)), Ter(expected));
            env.close();

            if (expected == tesSUCCESS)
            {
                BEAST_EXPECT(checksOnAccount(env, from).size() == fromCkCount + 2);
                BEAST_EXPECT(checksOnAccount(env, to).size() == toCkCount + 2);

                env.require(Owners(from, fromOwnerCount + 2));
                env.require(Owners(to, to == from ? fromOwnerCount + 2 : toOwnerCount));
                return;
            }

            BEAST_EXPECT(checksOnAccount(env, from).size() == fromCkCount);
            BEAST_EXPECT(checksOnAccount(env, to).size() == toCkCount);

            env.require(Owners(from, fromOwnerCount));
            env.require(Owners(to, to == from ? fromOwnerCount : toOwnerCount));
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

        auto usdm = MPTTester({.env = env, .issuer = gw1, .flags = kMptDexFlags | tfMPTCanLock});
        MPT const usd = usdm;

        // Bad fee.
        env(check::create(alice, bob, usd(50)), Fee(drops(-10)), Ter(temBAD_FEE));
        env.close();

        // Bad flags.
        env(check::create(alice, bob, usd(50)), Txflags(tfImmediateOrCancel), Ter(temINVALID_FLAG));
        env.close();

        // Check to self.
        env(check::create(alice, alice, XRP(10)), Ter(temREDUNDANT));
        env.close();

        // Bad amount.
        env(check::create(alice, bob, drops(-1)), Ter(temBAD_AMOUNT));
        env.close();

        env(check::create(alice, bob, drops(0)), Ter(temBAD_AMOUNT));
        env.close();

        env(check::create(alice, bob, drops(1)));
        env.close();

        env(check::create(alice, bob, usd(-1)), Ter(temBAD_AMOUNT));
        env.close();

        env(check::create(alice, bob, usd(0)), Ter(temBAD_AMOUNT));
        env.close();

        env(check::create(alice, bob, usd(1)));
        env.close();
        {
            MPT const bad(makeMptID(0, xrpAccount()));
            env(check::create(alice, bob, bad(2)), Ter(temBAD_CURRENCY));
            env.close();
        }

        // Bad expiration.
        env(check::create(alice, bob, usd(50)),
            Expiration(NetClock::time_point{}),
            Ter(temBAD_EXPIRATION));
        env.close();

        // Destination does not exist.
        Account const bogie{"bogie"};
        env(check::create(alice, bogie, usd(50)), Ter(tecNO_DST));
        env.close();

        // Require destination tag.
        env(fset(bob, asfRequireDest));
        env.close();

        env(check::create(alice, bob, usd(50)), Ter(tecDST_TAG_NEEDED));
        env.close();

        env(check::create(alice, bob, usd(50)), DestTag(11));
        env.close();

        env(fclear(bob, asfRequireDest));
        env.close();
        {
            // Globally frozen asset.
            env.close();
            auto usfm =
                MPTTester({.env = env, .issuer = gwF, .flags = kMptDexFlags | tfMPTCanLock});
            MPT const usf = usfm;
            usfm.set({.flags = tfMPTLock});

            env(check::create(alice, bob, usf(50)), Ter(tecLOCKED));
            env.close();

            usfm.set({.flags = tfMPTUnlock});

            env(check::create(alice, bob, usf(50)));
            env.close();
        }
        {
            // Frozen MPT.  Check creation should be similar to payment
            // behavior in the face of locked MPT.
            usdm.authorizeHolders({alice, bob});
            env(pay(gw1, alice, usd(25)));
            env(pay(gw1, bob, usd(25)));
            env.close();

            usdm.set({.holder = alice, .flags = tfMPTLock});
            // Setting MPT locked prevents alice from
            // creating a check for USD ore receiving a check. This is different
            // from IOU where alice can receive checks from bob or gw.
            env.close();
            env(check::create(alice, bob, usd(50)), Ter(tecLOCKED));
            env.close();
            // Note that IOU returns tecPATH_DRY in this case.
            // IOU's internal error is terNO_LINE, which is
            // considered ter re-triable and changed to tecPATH_DRY.
            env(pay(alice, bob, usd(1)), Ter(tecPATH_DRY));
            env.close();
            env(check::create(bob, alice, usd(50)), Ter(tecLOCKED));
            env.close();
            env(pay(bob, alice, usd(1)), Ter(tecPATH_DRY));
            env.close();
            env(check::create(gw1, alice, usd(50)), Ter(tecLOCKED));
            env.close();
            env(pay(gw1, alice, usd(1)));
            env.close();

            // Clear that lock.  Now check creation works.
            usdm.set({.holder = alice, .flags = tfMPTUnlock});
            env(check::create(alice, bob, usd(50)));
            env.close();
            env(check::create(bob, alice, usd(50)));
            env.close();
            env(check::create(gw1, alice, usd(50)));
            env.close();
        }

        // Expired expiration.
        env(check::create(alice, bob, usd(50)), Expiration(env.now()), Ter(tecEXPIRED));
        env.close();

        using namespace std::chrono_literals;
        env(check::create(alice, bob, usd(50)), Expiration(env.now() + 1s));
        env.close();

        // Insufficient reserve.
        Account const cheri{"cheri"};
        env.fund(env.current()->fees().accountReserve(1) - drops(1), cheri);

        env(check::create(cheri, bob, usd(50)),
            Fee(drops(env.current()->fees().base)),
            Ter(tecINSUFFICIENT_RESERVE));
        env.close();

        env(pay(bob, cheri, drops(env.current()->fees().base + 1)));
        env.close();

        env(check::create(cheri, bob, usd(50)));
        env.close();
    }

    void
    testCashMPT(FeatureBitset features)
    {
        // Explore many of the valid ways to cash a check for an MPT.
        testcase("Cash MPT");

        using namespace test::jtx;

        Account const gw{"gateway"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        {
            // Simple MPT check cashed with Amount (with failures).
            Env env{*this, features};

            env.fund(XRP(1'000), gw, alice, bob);

            MPT const usd =
                MPTTester({.env = env, .issuer = gw, .holders = {alice}, .maxAmt = 105});

            // alice writes the check before she gets the funds.
            uint256 const chkId1{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, usd(100)));
            env.close();

            // bob attempts to cash the check.  Should fail.
            env(check::cash(bob, chkId1, usd(100)), Ter(tecPATH_PARTIAL));
            env.close();

            // alice gets almost enough funds.  bob tries and fails again.
            env(pay(gw, alice, usd(95)));
            env.close();
            env(check::cash(bob, chkId1, usd(100)), Ter(tecPATH_PARTIAL));
            env.close();

            // alice gets the last of the necessary funds.
            env(pay(gw, alice, usd(5)));
            env.close();

            // bob for more than the check's SendMax.
            env.close();
            env(check::cash(bob, chkId1, usd(105)), Ter(tecPATH_PARTIAL));
            env.close();

            // bob asks for exactly the check amount and the check clears.
            // MPT is authorized automatically
            env(check::cash(bob, chkId1, usd(100)));
            env.close();
            env.require(Balance(alice, usd(0)));
            env.require(Balance(bob, usd(100)));
            BEAST_EXPECT(checksOnAccount(env, alice).empty());
            BEAST_EXPECT(checksOnAccount(env, bob).empty());
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 1);

            // bob tries to cash the same check again, which fails.
            env(check::cash(bob, chkId1, usd(100)), Ter(tecNO_ENTRY));
            env.close();

            // bob pays alice USD(70) so he can try another case.
            env(pay(bob, alice, usd(70)));
            env.close();

            uint256 const chkId2{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, usd(70)));
            env.close();
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 1);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 1);

            // bob cashes the check for less than the face amount.  That works,
            // consumes the check, and bob receives as much as he asked for.
            env(check::cash(bob, chkId2, usd(50)));
            env.close();
            env.require(Balance(alice, usd(20)));
            env.require(Balance(bob, usd(80)));
            BEAST_EXPECT(checksOnAccount(env, alice).empty());
            BEAST_EXPECT(checksOnAccount(env, bob).empty());
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 1);

            // alice writes two checks for USD(20), although she only has
            // USD(20).
            uint256 const chkId3{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, usd(20)));
            env.close();
            uint256 const chkId4{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, usd(20)));
            env.close();
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 2);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 2);

            // bob cashes the second check for the face amount.
            env(check::cash(bob, chkId4, usd(20)));
            env.close();
            env.require(Balance(alice, usd(0)));
            env.require(Balance(bob, usd(100)));
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 1);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 1);
            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(ownerCount(env, bob) == 1);

            // bob is not allowed to cash the last check for USD(0), he must
            // use check::cancel instead.
            env(check::cash(bob, chkId3, usd(0)), Ter(temBAD_AMOUNT));
            env.close();
            env.require(Balance(alice, usd(0)));
            env.require(Balance(bob, usd(100)));
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
                env(pay(gw, bob, usd(200)), Ter(tecPATH_PARTIAL));
                env.close();

                uint256 const chkId20{getCheckIndex(gw, env.seq(gw))};
                env(check::create(gw, bob, usd(200)));
                env.close();

                // Cashing a check for 200 USD fails.
                env(check::cash(bob, chkId20, usd(200)), Ter(tecPATH_PARTIAL));
                env.close();
                env.require(Balance(bob, usd(100)));

                // Clean up this most recent experiment so the rest of the
                // tests work.
                env(pay(bob, gw, usd(100)));
                env(check::cancel(bob, chkId20));
            }

            // ... so bob cancels alice's remaining check.
            env(check::cancel(bob, chkId3));
            env.close();
            env.require(Balance(alice, usd(0)));
            env.require(Balance(bob, usd(0)));
            BEAST_EXPECT(checksOnAccount(env, alice).empty());
            BEAST_EXPECT(checksOnAccount(env, bob).empty());
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 1);
        }
        {
            // Simple MPT check cashed with DeliverMin (with failures).
            Env env{*this, features};

            env.fund(XRP(1'000), gw, alice, bob);

            MPT const usd =
                MPTTester({.env = env, .issuer = gw, .holders = {alice, bob}, .maxAmt = 20});

            env(pay(gw, alice, usd(8)));
            env.close();

            // alice creates several checks ahead of time.
            uint256 const chkId9{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, usd(9)));
            env.close();
            uint256 const chkId8{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, usd(8)));
            env.close();
            uint256 const chkId7{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, usd(7)));
            env.close();
            uint256 const chkId6{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, usd(6)));
            env.close();

            // bob attempts to cash a check for the amount on the check.
            // Should fail, since alice doesn't have the funds.
            env(check::cash(bob, chkId9, check::DeliverMin(usd(9))), Ter(tecPATH_PARTIAL));
            env.close();

            // bob sets a DeliverMin of 7 and gets all that alice has.
            env(check::cash(bob, chkId9, check::DeliverMin(usd(7))));
            verifyDeliveredAmount(env, usd(8));
            env.require(Balance(alice, usd(0)));
            env.require(Balance(bob, usd(8)));
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 3);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 3);
            BEAST_EXPECT(ownerCount(env, alice) == 4);
            BEAST_EXPECT(ownerCount(env, bob) == 1);

            // bob pays alice USD(7) so he can use another check.
            env(pay(bob, alice, usd(7)));
            env.close();

            // Using DeliverMin for the SendMax value of the check (and no
            // transfer fees) should work just like setting Amount.
            env(check::cash(bob, chkId7, check::DeliverMin(usd(7))));
            verifyDeliveredAmount(env, usd(7));
            env.require(Balance(alice, usd(0)));
            env.require(Balance(bob, usd(8)));
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 2);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 2);
            BEAST_EXPECT(ownerCount(env, alice) == 3);
            BEAST_EXPECT(ownerCount(env, bob) == 1);

            // bob pays alice USD(8) so he can use the last two checks.
            env(pay(bob, alice, usd(8)));
            env.close();

            // alice has USD(8). If bob uses the check for USD(6) and uses a
            // DeliverMin of 4, he should get the SendMax value of the check.
            env(check::cash(bob, chkId6, check::DeliverMin(usd(4))));
            verifyDeliveredAmount(env, usd(6));
            env.require(Balance(alice, usd(2)));
            env.require(Balance(bob, usd(6)));
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 1);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 1);
            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(ownerCount(env, bob) == 1);

            // bob cashes the last remaining check setting a DeliverMin.
            // of exactly alice's remaining USD.
            env(check::cash(bob, chkId8, check::DeliverMin(usd(2))));
            verifyDeliveredAmount(env, usd(2));
            env.require(Balance(alice, usd(0)));
            env.require(Balance(bob, usd(8)));
            BEAST_EXPECT(checksOnAccount(env, alice).empty());
            BEAST_EXPECT(checksOnAccount(env, bob).empty());
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 1);
        }
        {
            // Examine the effects of the asfRequireAuth flag.
            Env env(*this, features);

            env.fund(XRP(1000), gw, alice, bob);
            auto usdm = MPTTester(
                {.env = env,
                 .issuer = gw,
                 .holders = {alice},
                 .flags = kMptDexFlags | tfMPTRequireAuth,
                 .maxAmt = 20});
            MPT const usd = usdm;
            usdm.authorize({.holder = alice});
            env.close();
            env(pay(gw, alice, usd(8)));
            env.close();

            // alice writes a check to bob for USD.  bob can't cash it
            // because he is not authorized to hold gw["USD"].
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, usd(7)));
            env.close();

            env(check::cash(bob, chkId, usd(7)), Ter(tecNO_AUTH));
            env.close();

            // Now give bob MPT for USD.  bob still can't cash the
            // check because he is not authorized.
            usdm.authorize({.account = bob});
            env.close();

            env(check::cash(bob, chkId, usd(7)), Ter(tecNO_AUTH));
            env.close();

            // bob gets authorization to hold USD.
            usdm.authorize({.holder = bob});
            env.close();

            env(check::cash(bob, chkId, check::DeliverMin(usd(4))));
            STAmount const bobGot = usd(7);
            verifyDeliveredAmount(env, bobGot);
            env.require(Balance(alice, usd(8) - bobGot));
            env.require(Balance(bob, bobGot));

            BEAST_EXPECT(checksOnAccount(env, alice).empty());
            BEAST_EXPECT(checksOnAccount(env, bob).empty());
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 1);
        }

        {
            Env env{*this, features};

            env.fund(XRP(1'000), gw, alice, bob);

            MPT const usd =
                MPTTester({.env = env, .issuer = gw, .holders = {alice, bob}, .maxAmt = 20});

            // alice creates her checks ahead of time.
            uint256 const chkId1{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, usd(1)));
            env.close();

            uint256 const chkId2{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, usd(2)));
            env.close();

            env(pay(gw, alice, usd(8)));
            env.close();

            // Give bob a regular key and signers
            Account const bobby{"bobby", KeyType::Secp256k1};
            env(regkey(bob, bobby));
            env.close();

            Account const bogie{"bogie", KeyType::Secp256k1};
            Account const demon{"demon", KeyType::Ed25519};
            env(signers(bob, 2, {{bogie, 1}, {demon, 1}}), Sig(bobby));
            env.close();

            int const signersCount = 1;
            BEAST_EXPECT(ownerCount(env, bob) == signersCount + 1);

            // bob uses his regular key to cash a check.
            env(check::cash(bob, chkId1, (usd(1))), Sig(bobby));
            env.close();
            env.require(Balance(alice, usd(7)));
            env.require(Balance(bob, usd(1)));
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 1);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 1);
            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(ownerCount(env, bob) == signersCount + 1);

            // bob uses multisigning to cash a check.
            XRPAmount const baseFeeDrops{env.current()->fees().base};
            env(check::cash(bob, chkId2, (usd(2))), Msig(bogie, demon), Fee(3 * baseFeeDrops));
            env.close();
            env.require(Balance(alice, usd(5)));
            env.require(Balance(bob, usd(3)));
            BEAST_EXPECT(checksOnAccount(env, alice).empty());
            BEAST_EXPECT(checksOnAccount(env, bob).empty());
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
        MPT const usd = MPTTester(
            {.env = env,
             .issuer = gw,
             .holders = {alice, bob},
             .transferFee = 25'000,
             .maxAmt = 1'000});

        env.close();
        env(pay(gw, alice, usd(1'000)));
        env.close();

        // alice writes a check with a SendMax of USD(125).  The most bob
        // can get is USD(100) because of the transfer rate.
        uint256 const chkId125{getCheckIndex(alice, env.seq(alice))};
        env(check::create(alice, bob, usd(125)));
        env.close();

        // alice writes another check that won't get cashed until the transfer
        // rate changes so we can see the rate applies when the check is
        // cashed, not when it is created.
#if 0
        uint256 const chkId120{getCheckIndex(alice, env.Seq(alice))};
        env(check::create(alice, bob, USD(120)));
        env.close();
#endif

        // bob attempts to cash the check for face value.  Should fail.
        env(check::cash(bob, chkId125, usd(125)), Ter(tecPATH_PARTIAL));
        env.close();
        env(check::cash(bob, chkId125, check::DeliverMin(usd(101))), Ter(tecPATH_PARTIAL));
        env.close();

        // bob decides that he'll accept anything USD(75) or up.
        // He gets USD(100).
        env(check::cash(bob, chkId125, check::DeliverMin(usd(75))));
        verifyDeliveredAmount(env, usd(100));
        env.require(Balance(alice, usd(1'000 - 125)));
        env.require(Balance(bob, usd(0 + 100)));
        BEAST_EXPECT(checksOnAccount(env, alice).empty());
        BEAST_EXPECT(checksOnAccount(env, bob).empty());

#if 0
        // Adjust gw's rate...
        env(rate(gw, 1.2));
        env.close();

        // bob cashes the second check for less than the face value.  The new
        // rate applies to the actual value transferred.
        env(check::cash(bob, chkId120, USD(50)));
        env.close();
        env.Require(Balance(alice, USD(1000 - 125 - 60)));
        env.Require(Balance(bob, USD(0 + 100 + 50)));
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
        std::int64_t maxAmt{20};

        Env env(*this, features);

        env.fund(XRP(1000), gw, alice, bob, zoe);

        auto usdm = MPTTester(
            {.env = env,
             .issuer = gw,
             .holders = {alice},
             .flags = kMptDexFlags | tfMPTCanLock,
             .maxAmt = maxAmt});
        MPT const usd = usdm;

        env(pay(gw, alice, usd(20)));
        env.close();

        usdm.authorize({.account = bob});

        // bob tries to cash a non-existent check from alice.
        {
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::cash(bob, chkId, usd(20)), Ter(tecNO_ENTRY));
            env.close();
        }

        // alice creates her checks ahead of time.
        uint256 const chkIdU{getCheckIndex(alice, env.seq(alice))};
        env(check::create(alice, bob, usd(20)));
        env.close();

        uint256 const chkIdX{getCheckIndex(alice, env.seq(alice))};
        env(check::create(alice, bob, XRP(10)));
        env.close();

        using namespace std::chrono_literals;
        uint256 const chkIdExp{getCheckIndex(alice, env.seq(alice))};
        env(check::create(alice, bob, XRP(10)), Expiration(env.now() + 1s));
        env.close();

        uint256 const chkIdFroz1{getCheckIndex(alice, env.seq(alice))};
        env(check::create(alice, bob, usd(1)));
        env.close();

        uint256 const chkIdFroz2{getCheckIndex(alice, env.seq(alice))};
        env(check::create(alice, bob, usd(2)));
        env.close();

        uint256 const chkIdFroz3{getCheckIndex(alice, env.seq(alice))};
        env(check::create(alice, bob, usd(3)));
        env.close();

        uint256 const chkIdNoDest1{getCheckIndex(alice, env.seq(alice))};
        env(check::create(alice, bob, usd(1)));
        env.close();

        uint256 const chkIdHasDest2{getCheckIndex(alice, env.seq(alice))};
        env(check::create(alice, bob, usd(2)), DestTag(7));
        env.close();

        // Same set of failing cases for both MPT and XRP check cashing.
        auto failingCases = [&env, &gw, &alice, &bob](
                                uint256 const& chkId, STAmount const& amount) {
            // Bad fee.
            env(check::cash(bob, chkId, amount), Fee(drops(-10)), Ter(temBAD_FEE));
            env.close();

            // Bad flags.
            env(check::cash(bob, chkId, amount),
                Txflags(tfImmediateOrCancel),
                Ter(temINVALID_FLAG));
            env.close();

            // Missing both Amount and DeliverMin.
            {
                json::Value tx{check::cash(bob, chkId, amount)};
                tx.removeMember(sfAmount.jsonName);
                env(tx, Ter(temMALFORMED));
                env.close();
            }
            // Both Amount and DeliverMin present.
            {
                json::Value tx{check::cash(bob, chkId, amount)};
                tx[sfDeliverMin.jsonName] = amount.getJson(JsonOptions::Values::None);
                env(tx, Ter(temMALFORMED));
                env.close();
            }

            // Negative or zero amount.
            {
                STAmount neg{amount};
                neg.negate();
                env(check::cash(bob, chkId, neg), Ter(temBAD_AMOUNT));
                env.close();
                env(check::cash(bob, chkId, amount.zeroed()), Ter(temBAD_AMOUNT));
                env.close();
            }

            // Bad currency.
            if (!amount.native())
            {
                Issue const badIssue{badCurrency(), amount.getIssuer()};
                STAmount badAmount{amount};
                badAmount.setIssue(Issue{badCurrency(), amount.getIssuer()});
                env(check::cash(bob, chkId, badAmount), Ter(temBAD_CURRENCY));
                env.close();
            }

            // Not destination cashing check.
            env(check::cash(alice, chkId, amount), Ter(tecNO_PERMISSION));
            env.close();
            env(check::cash(gw, chkId, amount), Ter(tecNO_PERMISSION));
            env.close();

            // Currency mismatch.
            {
                MPT const eur = MPTTester({.env = env, .issuer = gw});
                STAmount const badAmount{eur, amount};
                env(check::cash(bob, chkId, badAmount), Ter(temMALFORMED));
                env.close();
            }

            // Issuer mismatch.
            // Every MPT is unique. There is no USD MPT with different issuers.

            // Amount bigger than SendMax.
            env(check::cash(bob, chkId, amount + amount), Ter(tecPATH_PARTIAL));
            env.close();

            // DeliverMin bigger than SendMax.
            env(check::cash(bob, chkId, check::DeliverMin(amount + amount)), Ter(tecPATH_PARTIAL));
            env.close();
        };

        failingCases(chkIdX, XRP(10));
        failingCases(chkIdU, usd(20));

        // Verify that those two checks really were cashable.
        env(check::cash(bob, chkIdU, usd(20)));
        env.close();
        env(check::cash(bob, chkIdX, check::DeliverMin(XRP(10))));
        verifyDeliveredAmount(env, XRP(10));

        // Try to cash an expired check.
        env(check::cash(bob, chkIdExp, XRP(10)), Ter(tecEXPIRED));
        env.close();

        // Cancel the expired check.  Anyone can cancel an expired check.
        env(check::cancel(zoe, chkIdExp));
        env.close();

        // Can we cash a check with frozen MPT?
        {
            env(pay(bob, alice, usd(20)));
            env.close();
            env.require(Balance(alice, usd(20)));
            env.require(Balance(bob, usd(0)));

            // Global freeze
            usdm.set({.flags = tfMPTLock});

            // MPTLocked flag is set and the account is not the issuer of MPT
            env(check::cash(bob, chkIdFroz1, usd(1)), Ter(tecPATH_PARTIAL));
            env.close();
            env(check::cash(bob, chkIdFroz1, check::DeliverMin(usd(1))), Ter(tecPATH_PARTIAL));
            env.close();

            usdm.set({.flags = tfMPTUnlock});

            // No longer frozen.  Success.
            env(check::cash(bob, chkIdFroz1, usd(1)));
            env.close();
            env.require(Balance(alice, usd(19)));
            env.require(Balance(bob, usd(1)));

            // Freeze individual MPT.
            usdm.set({.holder = alice, .flags = tfMPTLock});
            env(check::cash(bob, chkIdFroz2, usd(2)), Ter(tecPATH_PARTIAL));
            env.close();
            env(check::cash(bob, chkIdFroz2, check::DeliverMin(usd(1))), Ter(tecPATH_PARTIAL));
            env.close();

            // Clear that freeze.  Now check cashing works.
            usdm.set({.holder = alice, .flags = tfMPTUnlock});
            env(check::cash(bob, chkIdFroz2, usd(2)));
            env.close();
            env.require(Balance(alice, usd(17)));
            env.require(Balance(bob, usd(3)));

            // Freeze bob's MPT.  bob can't cash the check.
            usdm.set({.holder = bob, .flags = tfMPTLock});
            env(check::cash(bob, chkIdFroz3, usd(3)), Ter(tecLOCKED));
            env.close();
            env(check::cash(bob, chkIdFroz3, check::DeliverMin(usd(1))), Ter(tecLOCKED));
            env.close();

            // Clear that freeze.  Now check cashing works again.
            usdm.set({.holder = bob, .flags = tfMPTUnlock});
            env.close();
            env(check::cash(bob, chkIdFroz3, check::DeliverMin(usd(1))));
            verifyDeliveredAmount(env, usd(3));
            env.require(Balance(alice, usd(14)));
            env.require(Balance(bob, usd(6)));
        }
        {
            // Set the RequireDest flag on bob's account (after the check
            // was created) then cash a check without a destination tag.
            env(fset(bob, asfRequireDest));
            env.close();
            env(check::cash(bob, chkIdNoDest1, usd(1)), Ter(tecDST_TAG_NEEDED));
            env.close();
            env(check::cash(bob, chkIdNoDest1, check::DeliverMin(usd(1))), Ter(tecDST_TAG_NEEDED));
            env.close();

            // bob can cash a check with a destination tag.
            env(check::cash(bob, chkIdHasDest2, usd(2)));
            env.close();

            env.require(Balance(alice, usd(12)));
            env.require(Balance(bob, usd(8)));

            // Clear the RequireDest flag on bob's account so he can
            // cash the check with no DestinationTag.
            env(fclear(bob, asfRequireDest));
            env.close();
            env(check::cash(bob, chkIdNoDest1, usd(1)));
            env.close();
            env.require(Balance(alice, usd(11)));
            env.require(Balance(bob, usd(9)));
        }

        // OutstandingAmount exceeds MaximumAmount
        {
            // Already at maximum
            BEAST_EXPECT(env.balance(gw, usdm) == usdm(-maxAmt));

            uint256 const chkId{getCheckIndex(gw, env.seq(gw))};
            env(check::create(gw, bob, usdm(10)));
            env.close();

            // Exceeds MaximumAmount (20 + 10) = 30 > 20
            env(check::cash(bob, chkId, usdm(10)), Ter(tecPATH_PARTIAL));
            env.close();

            // Redeem some tokens (20 - 9) = 11
            env(pay(alice, gw, usdm(9)));
            env.close();

            // Still exceeds MaximumAmount (11 + 10) = 21 > 20
            env(check::cash(bob, chkId, usdm(10)), Ter(tecPATH_PARTIAL));
            env.close();
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

            MPT const usd = MPTTester({.env = env, .issuer = gw});

            // alice creates her checks ahead of time.
            // Three ordinary checks with no expiration.
            uint256 const chkId1{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, usd(10)));
            env.close();

            uint256 const chkId2{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, XRP(10)));
            env.close();

            uint256 const chkId3{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, usd(10)));
            env.close();

            // Three checks that expire in 10 minutes.
            using namespace std::chrono_literals;
            uint256 const chkIdNotExp1{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, XRP(10)), Expiration(env.now() + 600s));
            env.close();

            uint256 const chkIdNotExp2{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, usd(10)), Expiration(env.now() + 600s));
            env.close();

            uint256 const chkIdNotExp3{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, XRP(10)), Expiration(env.now() + 600s));
            env.close();

            // Three checks that expire in one second.
            uint256 const chkIdExp1{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, usd(10)), Expiration(env.now() + 1s));
            env.close();

            uint256 const chkIdExp2{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, XRP(10)), Expiration(env.now() + 1s));
            env.close();

            uint256 const chkIdExp3{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, usd(10)), Expiration(env.now() + 1s));
            env.close();

            // Two checks to cancel using a regular key and using multisigning.
            uint256 const chkIdReg{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, usd(10)));
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

            env(check::cancel(zoe, chkId3), Ter(tecNO_PERMISSION));
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

            env(check::cancel(zoe, chkIdNotExp3), Ter(tecNO_PERMISSION));
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
            Account const alie{"alie", KeyType::Ed25519};
            env(regkey(alice, alie));
            env.close();

            Account const bogie{"bogie", KeyType::Secp256k1};
            Account const demon{"demon", KeyType::Ed25519};
            env(signers(alice, 2, {{bogie, 1}, {demon, 1}}), Sig(alie));
            env.close();

            int const signersCount{1};

            // alice uses her regular key to cancel a check.
            env(check::cancel(alice, chkIdReg), Sig(alie));
            env.close();
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 3);
            BEAST_EXPECT(ownerCount(env, alice) == signersCount + 3);

            // alice uses multisigning to cancel a check.
            XRPAmount const baseFeeDrops{env.current()->fees().base};
            env(check::cancel(alice, chkIdMSig), Msig(bogie, demon), Fee(3 * baseFeeDrops));
            env.close();
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 2);
            BEAST_EXPECT(ownerCount(env, alice) == signersCount + 2);

            // Creator and destination cancel the remaining unexpired checks.
            env(check::cancel(alice, chkId3), Sig(alice));
            env.close();
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 1);
            BEAST_EXPECT(ownerCount(env, alice) == signersCount + 1);

            env(check::cancel(bob, chkIdNotExp3));
            env.close();
            BEAST_EXPECT(checksOnAccount(env, alice).empty());
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

        MPT const usd =
            MPTTester({.env = env, .issuer = gw, .holders = {alice, bob}, .maxAmt = 1'000});

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
        env.require(Owners(alice, 11));
        env.require(Owners(bob, 11));

        env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        env.require(tickets(bob, env.seq(bob) - bobTicketSeq));
        BEAST_EXPECT(env.seq(bob) == bobSeq);

        env(pay(gw, alice, usd(900)));
        env.close();

        // alice creates four checks; two XRP, two MPT.  Bob will cash
        // one of each and cancel one of each.
        uint256 const chkIdXrp1{getCheckIndex(alice, aliceTicketSeq)};
        env(check::create(alice, bob, XRP(200)), ticket::Use(aliceTicketSeq++));

        uint256 const chkIdXrp2{getCheckIndex(alice, aliceTicketSeq)};
        env(check::create(alice, bob, XRP(300)), ticket::Use(aliceTicketSeq++));

        uint256 const chkIdUsd1{getCheckIndex(alice, aliceTicketSeq)};
        env(check::create(alice, bob, usd(200)), ticket::Use(aliceTicketSeq++));

        uint256 const chkIdUsd2{getCheckIndex(alice, aliceTicketSeq)};
        env(check::create(alice, bob, usd(300)), ticket::Use(aliceTicketSeq++));

        env.close();
        // Alice used four tickets but created four checks.
        env.require(Owners(alice, 11));
        env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
        BEAST_EXPECT(checksOnAccount(env, alice).size() == 4);
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        env.require(Owners(bob, 11));
        BEAST_EXPECT(env.seq(bob) == bobSeq);

        // Bob cancels two of alice's checks.
        env(check::cancel(bob, chkIdXrp1), ticket::Use(bobTicketSeq++));
        env(check::cancel(bob, chkIdUsd2), ticket::Use(bobTicketSeq++));
        env.close();

        env.require(Owners(alice, 9));
        env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
        BEAST_EXPECT(checksOnAccount(env, alice).size() == 2);
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        env.require(Owners(bob, 9));
        BEAST_EXPECT(env.seq(bob) == bobSeq);

        // Bob cashes alice's two remaining checks.
        env(check::cash(bob, chkIdXrp2, XRP(300)), ticket::Use(bobTicketSeq++));
        env(check::cash(bob, chkIdUsd1, usd(200)), ticket::Use(bobTicketSeq++));
        env.close();

        auto const baseFee = env.current()->fees().base;
        env.require(Owners(alice, 7));
        env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
        BEAST_EXPECT(checksOnAccount(env, alice).empty());
        BEAST_EXPECT(env.seq(alice) == aliceSeq);
        env.require(Balance(alice, usd(700)));
        env.require(Balance(alice, XRP(700) - 6 * baseFee));
        env.require(Owners(bob, 7));
        BEAST_EXPECT(env.seq(bob) == bobSeq);
        env.require(Balance(bob, usd(200)));
        env.require(Balance(bob, XRP(1'300) - 6 * baseFee));
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
            beast::unit_test::Suite& suite;
            Env& env;
            Account const acct;
            std::size_t owners{0};
            hash_map<std::string, MPTTester> mpts;
            bool const isIssuer;
            bool const requireAuth;

            AccountOwns(
                beast::unit_test::Suite& s,
                Env& e,
                Account a,
                bool isIssuer,
                bool requireAuth = false)
                : suite(s), env(e), acct(std::move(a)), isIssuer(isIssuer), requireAuth(requireAuth)
            {
            }

            void
            verifyOwners(std::uint32_t line, bool print = false) const
            {
                if (print)
                {
                    std::cout << acct.name() << " " << ownerCount(env, acct) << " " << owners
                              << std::endl;
                }
                suite.expect(
                    ownerCount(env, acct) == owners, "Owner count mismatch", __FILE__, line);
            }

            // Operators to make using the class more convenient.
            operator Account() const
            {
                return acct;
            }

            operator xrpl::AccountID() const
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
                auto flags = kMptDexFlags | tfMPTCanLock;
                if (requireAuth)
                    flags |= tfMPTRequireAuth;
                auto [it, _] =
                    mpts.emplace(s, MPTTester({.env = env, .issuer = acct, .flags = flags}));
                (void)_;
                ++owners;

                return it->second[s];
            }

            iterator
            getIt(MPT const& mpt)
            {
                if (!isIssuer)
                    Throw<std::runtime_error>("AccountOwns::set must be issuer");
                auto it = mpts.find(mpt.name);
                if (it == mpts.end())
                    Throw<std::runtime_error>("AccountOwns::set mpt doesn't exist");
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
                it->second.authorize({.account = id, .flags = tfMPTUnauthorize});
                --id.owners;
            }

            void
            pay(iterator& it, Account const& src, Account const& dst, std::uint64_t amount)
            {
                if (env.le(keylet::account(dst))->isFlag(lsfDepositAuth))
                {
                    env(fclear(dst, asfDepositAuth));
                    it->second.pay(src, dst, amount);
                    env(fset(dst, asfDepositAuth));
                }
                else
                {
                    it->second.pay(src, dst, amount);
                }
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

            MPT const cK8 = gw1["CK8"];
            gw1.verifyOwners(__LINE__);

            Account const yui{"yui"};

            // Note the reserve in unit tests is 200 XRP, not 20.  So here
            // we're just barely giving yui enough XRP to meet the
            // account reserve.
            env.fund(XRP(200), yui);
            env.close();

            uint256 const chkId{getCheckIndex(gw1, env.seq(gw1))};
            env(check::create(gw1, yui, cK8(99)));
            env.close();

            env(check::cash(yui, chkId, cK8(99)), Ter(tecINSUFFICIENT_RESERVE));
            env.close();
            alice.verifyOwners(__LINE__);

            // Give yui enough XRP to meet the trust line's reserve.  Cashing
            // the check succeeds and creates the trust line.
            env(pay(env.master, yui, XRP(51)));
            env.close();
            env(check::cash(yui, chkId, cK8(99)));
            verifyDeliveredAmount(env, cK8(99));
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
            MPT const oF1 = gw1["OF1"];
            env(offer(gw1, XRP(98), oF1(98)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(oF1.issuanceID, alice)) == nullptr);
            env(offer(alice, oF1(98), XRP(98)));
            ++alice.owners;
            env.close();

            // Both offers should be consumed.
            // Since gw1's offer was consumed and the trust line was not
            // created by gw1, gw1's owner count should be 0.
            gw1.verifyOwners(__LINE__);

            // alice's automatically created MPT bumps her owner count.
            alice.verifyOwners(__LINE__);

            // Use check cashing to automatically create the trust line.
            MPT const cK1 = gw1["CK1"];
            uint256 const chkId{getCheckIndex(gw1, env.seq(gw1))};
            env(check::create(gw1, alice, cK1(98)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(cK1.issuanceID, alice)) == nullptr);
            env(check::cash(alice, chkId, cK1(98)));
            ++alice.owners;
            verifyDeliveredAmount(env, cK1(98));
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
            MPT const oF1 = gw1["OF1"];
            env(offer(alice, XRP(97), oF1(97)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(oF1, bob)) == nullptr);
            env(offer(bob, oF1(97), XRP(97)));
            ++bob.owners;
            env.close();

            // Both offers should be consumed.
            env.require(Balance(alice, oF1(1)));
            env.require(Balance(bob, oF1(97)));

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
            MPT const cK1 = gw1["CK1"];
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, cK1(97)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(cK1, bob)) == nullptr);
            env(check::cash(bob, chkId, cK1(97)));
            ++bob.owners;
            env.close();

            BEAST_EXPECT(env.le(keylet::mptoken(oF1, bob)) != nullptr);

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
            MPT const oF2 = gw1["OF2"];
            env(offer(gw1, XRP(96), oF2(96)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(oF2, alice)) == nullptr);
            env(offer(alice, oF2(96), XRP(96)));
            ++alice.owners;
            env.close();

            // Both offers should be consumed.
            // Since gw1's offer was consumed, gw1 owner count doesn't change.
            gw1.verifyOwners(__LINE__);

            // alice's automatically created MPT bumps her owner count.
            alice.verifyOwners(__LINE__);

            // Use check cashing to automatically create MPT.
            MPT const cK2 = gw1["CK2"];
            uint256 const chkId{getCheckIndex(gw1, env.seq(gw1))};
            env(check::create(gw1, alice, cK2(96)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(cK2, alice)) == nullptr);
            env(check::cash(alice, chkId, cK2(96)));
            ++alice.owners;
            verifyDeliveredAmount(env, cK2(96));
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
            MPT const oF2 = gw1["OF2"];
            env(offer(alice, XRP(95), oF2(95)));
            env.close();
            // alice already has OF2 MPT
            BEAST_EXPECT(env.le(keylet::mptoken(oF2, alice)) != nullptr);
            env(offer(bob, oF2(95), XRP(95)));
            ++bob.owners;
            env.close();

            // bob's owner count should increase due to the new MPT.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Use check cashing to automatically create MPT.
            MPT const cK2 = gw1["CK2"];
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, cK2(95)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(cK2, bob)) == nullptr);
            env(check::cash(bob, chkId, cK2(95)));
            ++bob.owners;
            verifyDeliveredAmount(env, cK2(95));
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
            MPT const oF3 = gw1["OF3"];
            env(offer(gw1, XRP(94), oF3(94)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(oF3, alice)) == nullptr);
            env(offer(alice, oF3(94), XRP(94)));
            ++alice.owners;
            env.close();

            // Both offers should be consumed.
            // Since gw1's offer was consumed and MPT was not
            // created by gw1, gw1's owner count doesn't change.
            gw1.verifyOwners(__LINE__);

            // alice's automatically created MPT bumps her owner count.
            alice.verifyOwners(__LINE__);

            // Use check cashing to automatically create MPT.
            MPT const cK3 = gw1["CK3"];
            uint256 const chkId{getCheckIndex(gw1, env.seq(gw1))};
            env(check::create(gw1, alice, cK3(94)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(cK3, alice)) == nullptr);
            env(check::cash(alice, chkId, cK3(94)));
            ++alice.owners;
            verifyDeliveredAmount(env, cK3(94));
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
            MPT const oF3 = gw1["OF3"];
            env(offer(alice, XRP(93), oF3(93)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(oF3, alice)) != nullptr);
            env(offer(bob, oF3(93), XRP(93)));
            ++bob.owners;
            env.close();

            // bob's owner count should increase due to the new MPT.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Use check cashing to automatically create MPT.
            MPT const cK3 = gw1["CK3"];
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, cK3(93)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(cK3, bob)) == nullptr);
            env(check::cash(bob, chkId, cK3(93)));
            ++bob.owners;
            verifyDeliveredAmount(env, cK3(93));
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
            MPT const oF4 = gw1["OF4"];
            env(offer(gw1, XRP(92), oF4(92)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(oF4, alice)) == nullptr);
            env(offer(alice, oF4(92), XRP(92)));
            ++alice.owners;
            env.close();

            // alice's owner count should increase do to the new MPT.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Use check cashing to automatically create MPT.
            MPT const cK4 = gw1["CK4"];
            uint256 const chkId{getCheckIndex(gw1, env.seq(gw1))};
            env(check::create(gw1, bob, cK4(92)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(cK4, bob)) == nullptr);
            env(check::cash(bob, chkId, cK4(92)));
            verifyDeliveredAmount(env, cK4(92));
            ++bob.owners;
            env.close();

            // bob's owner count should increase due to the new MPT.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // clean up
            gw1.cleanup(oF4, alice);
            gw1.cleanup(cK4, bob);
        }

        //-------------- lsfMPTLock, check written by issuer --------------
        {
            // Set lsfMPTLock on gw1.  That should stop any automatic
            // MPT from being created.

            // Use offers to automatically create MPT.
            MPT const oF4 = gw1["OF4"];
            gw1.set(oF4, tfMPTLock);
            env(offer(gw1, XRP(92), oF4(92)), Ter(tecFROZEN));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(oF4, alice)) == nullptr);
            env(offer(alice, oF4(92), XRP(92)), Ter(tecFROZEN));
            env.close();

            // No one's owner count should have changed.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Use check cashing to automatically create MPT.
            MPT const cK4 = gw1["CK4"];
            gw1.set(cK4, tfMPTLock);
            uint256 const chkId{getCheckIndex(gw1, env.seq(gw1))};
            env(check::create(gw1, alice, cK4(92)), Ter(tecLOCKED));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(cK4, alice)) == nullptr);
            env(check::cash(alice, chkId, cK4(92)), Ter(tecNO_ENTRY));
            env.close();

            // No one's owner count should have changed.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Because gw1 has set tfMPTLock, neither MPT
            // is created.
            BEAST_EXPECT(env.le(keylet::mptoken(oF4, alice)) == nullptr);
            BEAST_EXPECT(env.le(keylet::mptoken(cK4, alice)) == nullptr);

            // clear global freeze
            gw1.set(oF4, tfMPTUnlock);
            gw1.set(cK4, tfMPTUnlock);
        }

        //------------ lsfGlobalFreeze, check written by non-issuer ------------
        {
            // lsfGlobalFreeze flag set on gw1 should not stop
            // automatic MPT creation between non-issuers.

            // Use offers to automatically create MPT.
            MPT const oF4 = gw1["OF4"];
            gw1.authorize(oF4, alice);
            gw1.pay(gw1, alice, oF4(91));
            env(offer(alice, XRP(91), oF4(91)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(oF4, alice)) != nullptr);
            env(offer(bob, oF4(91), XRP(91)));
            ++bob.owners;
            env.close();

            // alice's owner count should increase since it created MPT.
            // bob's owner count should increase due to the new MPT.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Use check cashing to automatically create the trust line.
            MPT const cK4 = gw1["CK4"];
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, cK4(91)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(cK4, bob)) == nullptr);
            gw1.authorize(cK4, alice);
            gw1.pay(gw1, alice, cK4(91));
            env(check::cash(bob, chkId, cK4(91)));
            ++bob.owners;
            env.close();

            // alice's owner count should increase since it created MPT.
            // bob's owner count should increase due to the new MPT.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // cleanup
            gw1.cleanup(oF4, alice);
            gw1.cleanup(cK4, alice);
            gw1.cleanup(oF4, bob);
            gw1.cleanup(cK4, bob);
        }

        //------------ lsfMPTLock, check written by non-issuer ------------
        {
            // Since gw1 has the lsfMPTLock flag set, there should be
            // no automatic MPT creation between non-issuers.

            // Use offers to automatically create MPT.
            MPT const oF4 = gw1["OF4"];
            gw1.set(oF4, tfMPTLock);
            env(offer(alice, XRP(91), oF4(91)), Ter(tecFROZEN));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(oF4, alice)) == nullptr);
            env(offer(bob, oF4(91), XRP(91)), Ter(tecFROZEN));
            env.close();

            // No one's owner count should have changed.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Use check cashing to automatically create the trust line.
            MPT const cK4 = gw1["CK4"];
            gw1.set(cK4, tfMPTLock);
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, cK4(91)), Ter(tecLOCKED));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(cK4, bob)) == nullptr);
            env(check::cash(bob, chkId, cK4(91)), Ter(tecNO_ENTRY));
            env.close();

            // No one's owner count should have changed.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Because gw1 has set lsfGlobalFreeze, neither trust line
            // is created.
            BEAST_EXPECT(env.le(keylet::mptoken(oF4, bob)) == nullptr);
            BEAST_EXPECT(env.le(keylet::mptoken(cK4, bob)) == nullptr);

            gw1.set(oF4, tfMPTUnlock);
            gw1.set(cK4, tfMPTUnlock);
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
            MPT const oF5 = gw2["OF5"];
            env(offer(gw2, XRP(92), oF5(92)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(oF5, alice)) == nullptr);
            env(offer(alice, oF5(92), XRP(92)));
            ++alice.owners;
            env.close();

            // alice's owner count should increase due to the new MPT.
            gw2.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Use check cashing to automatically create MPT.
            MPT const cK5 = gw2["CK5"];
            uint256 const chkId{getCheckIndex(gw2, env.seq(gw2))};
            env(check::create(gw2, alice, cK5(92)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(cK5, alice)) == nullptr);
            env(check::cash(alice, chkId, cK5(92)));
            verifyDeliveredAmount(env, cK5(92));
            ++alice.owners;
            env.close();

            // alice's owner count should increase due to the new MPT.
            gw2.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // cleanup
            gw2.cleanup(oF5, alice);
            gw2.cleanup(cK5, alice);
        }

        // Fund new gw to test since gw2 has MPTokenIssuance already created.
        // Set RequireAuth flag.
        AccountOwns gw3{*this, env, "gw3", true, true};
        {
            env.fund(XRP(5'000), gw3);
            env.close();
            // Use offers to automatically create the trust line.
            MPT const oF5 = gw3["OF5"];
            std::uint32_t const gw3OfferSeq = {env.seq(gw3)};
            env(offer(gw3, XRP(92), oF5(92)));
            ++gw3.owners;
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(oF5, alice)) == nullptr);
            env(offer(alice, oF5(92), XRP(92)), Ter(tecNO_AUTH));
            env.close();

            // gw3 should still own the offer, but no one else's owner
            // count should have changed.
            gw3.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Since we don't need it anymore, remove gw3's offer.
            env(offerCancel(gw3, gw3OfferSeq));
            --gw3.owners;
            env.close();
            gw3.verifyOwners(__LINE__);

            // Use check cashing to automatically create the trust line.
            MPT const cK5 = gw3["CK5"];
            uint256 const chkId{getCheckIndex(gw3, env.seq(gw3))};
            env(check::create(gw3, alice, cK5(92)));
            ++gw3.owners;
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(cK5, alice)) == nullptr);
            env(check::cash(alice, chkId, cK5(92)), Ter(tecNO_AUTH));
            env.close();

            // gw3 should still own the check, but no one else's owner
            // count should have changed.
            gw3.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Because gw3 has set lsfRequireAuth, neither trust line
            // is created.
            BEAST_EXPECT(env.le(keylet::mptoken(oF5, alice)) == nullptr);
            BEAST_EXPECT(env.le(keylet::mptoken(cK5, alice)) == nullptr);

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
            MPT const oF5 = gw2["OF5"];
            gw2.authorize(oF5, alice);
            gw2.pay(gw2, alice, oF5(91));
            env(offer(alice, XRP(91), oF5(91)));
            env.close();
            env(offer(bob, oF5(91), XRP(91)));
            ++bob.owners;
            env.close();

            // bob's owner count should increase due to the new MPT.
            gw2.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Use check cashing to automatically create the trust line.
            MPT const cK5 = gw2["CK5"];
            gw2.authorize(cK5, alice);
            gw2.pay(gw2, alice, cK5(91));
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, cK5(91)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(cK5, bob)) == nullptr);
            env(check::cash(bob, chkId, cK5(91)));
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
            MPT const oF5 = gw3["OF5"];
            env(offer(alice, XRP(91), oF5(91)), Ter(tecUNFUNDED_OFFER));
            env.close();
            env(offer(bob, oF5(91), XRP(91)), Ter(tecNO_AUTH));
            BEAST_EXPECT(env.le(keylet::mptoken(oF5, bob)) == nullptr);
            env.close();

            gw3.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Use check cashing to automatically create the trust line.
            MPT const cK5 = gw3["CK5"];
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, cK5(91)));
            env.close();
            BEAST_EXPECT(env.le(keylet::mptoken(cK5, bob)) == nullptr);
            env(check::cash(bob, chkId, cK5(91)), Ter(tecPATH_PARTIAL));
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
            BEAST_EXPECT(env.le(keylet::mptoken(oF5, bob)) == nullptr);
            BEAST_EXPECT(env.le(keylet::mptoken(cK5, bob)) == nullptr);
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
        auto const sa = testableAmendments();
        testWithFeats(sa);

        testMPTCreation(sa);
    }
};

BEAST_DEFINE_TESTSUITE(CheckMPT, tx, xrpl);

}  // namespace xrpl

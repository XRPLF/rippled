
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>
#include <test/jtx/check.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/invoice_id.h>
#include <test/jtx/multisign.h>
#include <test/jtx/offer.h>
#include <test/jtx/owners.h>
#include <test/jtx/pay.h>
#include <test/jtx/quality.h>
#include <test/jtx/rate.h>
#include <test/jtx/regkey.h>
#include <test/jtx/sendmax.h>
#include <test/jtx/sig.h>
#include <test/jtx/ter.h>
#include <test/jtx/ticket.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
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
#include <string>
#include <vector>

namespace xrpl {

class Check_test : public beast::unit_test::Suite
{
    static uint256
    getCheckIndex(AccountID const& account, std::uint32_t uSequence)
    {
        return keylet::check(account, uSequence).key;
    }

    // Helper function that returns the Checks on an account.
    static std::vector<SLE::const_pointer>
    checksOnAccount(test::jtx::Env& env, test::jtx::Account account)
    {
        std::vector<SLE::const_pointer> result;
        forEachItem(*env.current(), account, [&result](SLE::const_ref sle) {
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
    testEnabled(FeatureBitset features)
    {
        testcase("Enabled");

        using namespace test::jtx;
        Account const alice{"alice"};
        {
            // If the Checks amendment is enabled all check-related
            // facilities should be available.
            Env env{*this, features};

            env.fund(XRP(1000), alice);
            env.close();

            uint256 const checkId1{getCheckIndex(env.master, env.seq(env.master))};
            env(check::create(env.master, alice, XRP(100)));
            env.close();

            env(check::cash(alice, checkId1, XRP(100)));
            env.close();

            uint256 const checkId2{getCheckIndex(env.master, env.seq(env.master))};
            env(check::create(env.master, alice, XRP(100)));
            env.close();

            env(check::cancel(alice, checkId2));
            env.close();
        }
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
        IOU const usd{gw["USD"]};

        Env env{*this, features};

        STAmount const startBalance{XRP(1000).value()};
        env.fund(startBalance, gw, alice, bob);
        env.close();

        // Note that no trust line has been set up for alice, but alice can
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
        IOU const usd{gw["USD"]};

        Env env{*this, features};

        STAmount const startBalance{XRP(1000).value()};
        env.fund(startBalance, gw, alice, bob);
        env.close();

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

            if (isTesSuccess(expected))
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
        IOU const usd{gw1["USD"]};

        Env env{*this, features};

        STAmount const startBalance{XRP(1000).value()};
        env.fund(startBalance, gw1, gwF, alice, bob);
        env.close();

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
            IOU const bad{gw1, badCurrency()};
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
            IOU const usf{gwF["USF"]};
            env(fset(gwF, asfGlobalFreeze));
            env.close();

            env(check::create(alice, bob, usf(50)), Ter(tecFROZEN));
            env.close();

            env(check::create(gwF, bob, usf(50)), Ter(tecFROZEN));
            env.close();

            env(fclear(gwF, asfGlobalFreeze));
            env.close();

            env(check::create(alice, bob, usf(50)));
            env.close();

            env(check::create(gwF, bob, usf(50)));
            env.close();
        }
        {
            // Frozen trust line.  Check creation should be similar to payment
            // behavior in the face of frozen trust lines.
            env.trust(usd(1000), alice);
            env.trust(usd(1000), bob);
            env.close();
            env(pay(gw1, alice, usd(25)));
            env(pay(gw1, bob, usd(25)));
            env.close();

            // Setting trustline freeze in one direction prevents alice from
            // creating a check for USD.  But bob and gw1 should still be able
            // to create a check for USD to alice.
            env(trust(gw1, alice["USD"](0), tfSetFreeze));
            env.close();
            env(check::create(alice, bob, usd(50)), Ter(tecFROZEN));
            env.close();
            env(pay(alice, bob, usd(1)), Ter(tecPATH_DRY));
            env.close();
            env(check::create(bob, alice, usd(50)));
            env.close();
            env(pay(bob, alice, usd(1)));
            env.close();
            env(check::create(gw1, alice, usd(50)));
            env.close();
            env(pay(gw1, alice, usd(1)));
            env.close();

            // Clear that freeze.  Now check creation works.
            env(trust(gw1, alice["USD"](0), tfClearFreeze));
            env.close();
            env(check::create(alice, bob, usd(50)));
            env.close();
            env(check::create(bob, alice, usd(50)));
            env.close();
            env(check::create(gw1, alice, usd(50)));
            env.close();

            // Freezing in the other direction does not effect alice's USD
            // check creation, but prevents bob and gw1 from writing a check
            // for USD to alice.
            env(trust(alice, usd(0), tfSetFreeze));
            env.close();
            env(check::create(alice, bob, usd(50)));
            env.close();
            env(pay(alice, bob, usd(1)));
            env.close();
            env(check::create(bob, alice, usd(50)), Ter(tecFROZEN));
            env.close();
            env(pay(bob, alice, usd(1)), Ter(tecPATH_DRY));
            env.close();
            env(check::create(gw1, alice, usd(50)), Ter(tecFROZEN));
            env.close();
            env(pay(gw1, alice, usd(1)), Ter(tecPATH_DRY));
            env.close();

            // Clear that freeze.
            env(trust(alice, usd(0), tfClearFreeze));
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
        env.close();

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
    testCashXRP(FeatureBitset features)
    {
        // Explore many of the valid ways to cash a check for XRP.
        testcase("Cash XRP");

        using namespace test::jtx;

        Account const alice{"alice"};
        Account const bob{"bob"};

        Env env{*this, features};

        XRPAmount const baseFeeDrops{env.current()->fees().base};
        STAmount const startBalance{XRP(300).value()};
        env.fund(startBalance, alice, bob);
        env.close();
        {
            // Basic XRP check.
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, XRP(10)));
            env.close();
            env.require(Balance(alice, startBalance - drops(baseFeeDrops)));
            env.require(Balance(bob, startBalance));
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 1);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 1);
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 0);

            env(check::cash(bob, chkId, XRP(10)));
            env.close();
            env.require(Balance(alice, startBalance - XRP(10) - drops(baseFeeDrops)));
            env.require(Balance(bob, startBalance + XRP(10) - drops(baseFeeDrops)));
            BEAST_EXPECT(checksOnAccount(env, alice).empty());
            BEAST_EXPECT(checksOnAccount(env, bob).empty());
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, bob) == 0);

            // Make alice's and bob's balances easy to think about.
            env(pay(env.master, alice, XRP(10) + drops(baseFeeDrops)));
            env(pay(bob, env.master, XRP(10) - drops(baseFeeDrops * 2)));
            env.close();
            env.require(Balance(alice, startBalance));
            env.require(Balance(bob, startBalance));
        }
        {
            // Write a check that chews into alice's reserve.
            STAmount const reserve{env.current()->fees().reserve};
            STAmount const checkAmount{startBalance - reserve - drops(baseFeeDrops)};
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, checkAmount));
            env.close();

            // bob tries to cash for more than the check amount.
            env(check::cash(bob, chkId, checkAmount + drops(1)), Ter(tecPATH_PARTIAL));
            env.close();
            env(check::cash(bob, chkId, check::DeliverMin(checkAmount + drops(1))),
                Ter(tecPATH_PARTIAL));
            env.close();

            // bob cashes exactly the check amount.  This is successful
            // because one unit of alice's reserve is released when the
            // check is consumed.
            env(check::cash(bob, chkId, check::DeliverMin(checkAmount)));
            verifyDeliveredAmount(env, drops(checkAmount.mantissa()));
            env.require(Balance(alice, reserve));
            env.require(Balance(bob, startBalance + checkAmount - drops(baseFeeDrops * 3)));
            BEAST_EXPECT(checksOnAccount(env, alice).empty());
            BEAST_EXPECT(checksOnAccount(env, bob).empty());
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, bob) == 0);

            // Make alice's and bob's balances easy to think about.
            env(pay(env.master, alice, checkAmount + drops(baseFeeDrops)));
            env(pay(bob, env.master, checkAmount - drops(baseFeeDrops * 4)));
            env.close();
            env.require(Balance(alice, startBalance));
            env.require(Balance(bob, startBalance));
        }
        {
            // Write a check that goes one drop past what alice can pay.
            STAmount const reserve{env.current()->fees().reserve};
            STAmount const checkAmount{startBalance - reserve - drops(baseFeeDrops - 1)};
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, checkAmount));
            env.close();

            // bob tries to cash for exactly the check amount.  Fails because
            // alice is one drop shy of funding the check.
            env(check::cash(bob, chkId, checkAmount), Ter(tecPATH_PARTIAL));
            env.close();

            // bob decides to get what he can from the bounced check.
            env(check::cash(bob, chkId, check::DeliverMin(drops(1))));
            verifyDeliveredAmount(env, drops(checkAmount.mantissa() - 1));
            env.require(Balance(alice, reserve));
            env.require(Balance(bob, startBalance + checkAmount - drops(baseFeeDrops * 2 + 1)));
            BEAST_EXPECT(checksOnAccount(env, alice).empty());
            BEAST_EXPECT(checksOnAccount(env, bob).empty());
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, bob) == 0);

            // Make alice's and bob's balances easy to think about.
            env(pay(env.master, alice, checkAmount + drops(baseFeeDrops - 1)));
            env(pay(bob, env.master, checkAmount - drops(baseFeeDrops * 3 + 1)));
            env.close();
            env.require(Balance(alice, startBalance));
            env.require(Balance(bob, startBalance));
        }
    }

    void
    testCashIOU(FeatureBitset features)
    {
        // Explore many of the valid ways to cash a check for an IOU.
        testcase("Cash IOU");

        using namespace test::jtx;

        Account const gw{"gateway"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        IOU const usd{gw["USD"]};
        {
            // Simple IOU check cashed with Amount (with failures).
            Env env{*this, features};

            env.fund(XRP(1000), gw, alice, bob);
            env.close();

            // alice writes the check before she gets the funds.
            uint256 const chkId1{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, usd(10)));
            env.close();

            // bob attempts to cash the check.  Should fail.
            env(check::cash(bob, chkId1, usd(10)), Ter(tecPATH_PARTIAL));
            env.close();

            // alice gets almost enough funds.  bob tries and fails again.
            env(trust(alice, usd(20)));
            env.close();
            env(pay(gw, alice, usd(9.5)));
            env.close();
            env(check::cash(bob, chkId1, usd(10)), Ter(tecPATH_PARTIAL));
            env.close();

            // alice gets the last of the necessary funds.  bob tries again
            // and fails because he hasn't got a trust line for USD.
            env(pay(gw, alice, usd(0.5)));
            env.close();

            // bob sets up the trust line, but not at a high enough limit.
            env(trust(bob, usd(9.5)));
            env.close();

            // bob sets the trust line limit high enough but asks for more
            // than the check's SendMax.
            env(trust(bob, usd(10.5)));
            env.close();
            env(check::cash(bob, chkId1, usd(10.5)), Ter(tecPATH_PARTIAL));
            env.close();

            // bob asks for exactly the check amount and the check clears.
            env(check::cash(bob, chkId1, usd(10)));
            env.close();
            env.require(Balance(alice, usd(0)));
            env.require(Balance(bob, usd(10)));

            BEAST_EXPECT(checksOnAccount(env, alice).empty());
            BEAST_EXPECT(checksOnAccount(env, bob).empty());
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 1);

            // bob tries to cash the same check again, which fails.
            env(check::cash(bob, chkId1, usd(10)), Ter(tecNO_ENTRY));
            env.close();

            // bob pays alice USD(7) so he can try another case.
            env(pay(bob, alice, usd(7)));
            env.close();

            uint256 const chkId2{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, usd(7)));
            env.close();
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 1);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 1);

            // bob cashes the check for less than the face amount.  That works,
            // consumes the check, and bob receives as much as he asked for.
            env(check::cash(bob, chkId2, usd(5)));
            env.close();
            env.require(Balance(alice, usd(2)));
            env.require(Balance(bob, usd(8)));
            BEAST_EXPECT(checksOnAccount(env, alice).empty());
            BEAST_EXPECT(checksOnAccount(env, bob).empty());
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 1);

            // alice writes two checks for USD(2), although she only has USD(2).
            uint256 const chkId3{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, usd(2)));
            env.close();
            uint256 const chkId4{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, usd(2)));
            env.close();
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 2);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 2);

            // bob cashes the second check for the face amount.
            env(check::cash(bob, chkId4, usd(2)));
            env.close();
            env.require(Balance(alice, usd(0)));
            env.require(Balance(bob, usd(10)));
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 1);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 1);
            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(ownerCount(env, bob) == 1);

            // bob is not allowed to cash the last check for USD(0), he must
            // use check::cancel instead.
            env(check::cash(bob, chkId3, usd(0)), Ter(temBAD_AMOUNT));
            env.close();
            env.require(Balance(alice, usd(0)));
            env.require(Balance(bob, usd(10)));
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 1);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 1);
            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(ownerCount(env, bob) == 1);

            // Automatic trust lines are enabled.  But one aspect of
            // automatic trust lines is that they allow the account
            // cashing a check to exceed their trust line limit.  Show
            // that at work.
            //
            // bob's trust line limit is currently USD(10.5).  Show that
            // a payment to bob cannot exceed that trust line, but cashing
            // a check can.

            // Payment of 20 USD fails.
            env(pay(gw, bob, usd(20)), Ter(tecPATH_PARTIAL));
            env.close();

            uint256 const chkId20{getCheckIndex(gw, env.seq(gw))};
            env(check::create(gw, bob, usd(20)));
            env.close();

            // However cashing a check for 20 USD succeeds.
            env(check::cash(bob, chkId20, usd(20)));
            env.close();
            env.require(Balance(bob, usd(30)));

            // Clean up this most recent experiment so the rest of the
            // tests work.
            env(pay(bob, gw, usd(20)));

            // ... so bob cancels alice's remaining check.
            env(check::cancel(bob, chkId3));
            env.close();
            env.require(Balance(alice, usd(0)));
            env.require(Balance(bob, usd(10)));
            BEAST_EXPECT(checksOnAccount(env, alice).empty());
            BEAST_EXPECT(checksOnAccount(env, bob).empty());
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 1);
        }
        {
            // Simple IOU check cashed with DeliverMin (with failures).
            Env env{*this, features};

            env.fund(XRP(1000), gw, alice, bob);
            env.close();

            env(trust(alice, usd(20)));
            env(trust(bob, usd(20)));
            env.close();
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
            env(fset(gw, asfRequireAuth));
            env.close();
            env(trust(gw, alice["USD"](100)), Txflags(tfSetfAuth));
            env(trust(alice, usd(20)));
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

            // Now give bob a trustline for USD.  bob still can't cash the
            // check because he is not authorized.
            env(trust(bob, usd(5)));
            env.close();

            env(check::cash(bob, chkId, usd(7)), Ter(tecNO_AUTH));
            env.close();

            // bob gets authorization to hold gw["USD"].
            env(trust(gw, bob["USD"](1)), Txflags(tfSetfAuth));
            env.close();

            // Two possible outcomes here depending on whether cashing a
            // check can build a trust line:
            //   o If it can't build a trust line, then since bob set his
            //     limit low, he cashes the check with a DeliverMin and hits
            //     his trust limit.
            //  o If it can build a trust line, then the check is allowed to
            //    exceed the trust limit and bob gets the full transfer.
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

        // Use a regular key and also multisign to cash a check.
        {
            Env env{*this, features};
            env.fund(XRP(1000), gw, alice, bob);
            env.close();

            // alice creates her checks ahead of time.
            uint256 const chkId1{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, usd(1)));
            env.close();

            uint256 const chkId2{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, usd(2)));
            env.close();

            env(trust(alice, usd(20)));
            env(trust(bob, usd(20)));
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

            BEAST_EXPECT(ownerCount(env, bob) == 2);

            // bob uses his regular key to cash a check.
            env(check::cash(bob, chkId1, (usd(1))), Sig(bobby));
            env.close();
            env.require(Balance(alice, usd(7)));
            env.require(Balance(bob, usd(1)));
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 1);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 1);
            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(ownerCount(env, bob) == 2);

            // bob uses multisigning to cash a check.
            XRPAmount const baseFeeDrops{env.current()->fees().base};
            env(check::cash(bob, chkId2, (usd(2))), Msig(bogie, demon), Fee(3 * baseFeeDrops));
            env.close();
            env.require(Balance(alice, usd(5)));
            env.require(Balance(bob, usd(3)));
            BEAST_EXPECT(checksOnAccount(env, alice).empty());
            BEAST_EXPECT(checksOnAccount(env, bob).empty());
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 2);
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
        IOU const usd{gw["USD"]};

        Env env{*this, features};

        env.fund(XRP(1000), gw, alice, bob);
        env.close();

        env(trust(alice, usd(1000)));
        env(trust(bob, usd(1000)));
        env.close();
        env(pay(gw, alice, usd(1000)));
        env.close();

        // Set gw's transfer rate and see the consequences when cashing a check.
        env(rate(gw, 1.25));
        env.close();

        // alice writes a check with a SendMax of USD(125).  The most bob
        // can get is USD(100) because of the transfer rate.
        uint256 const chkId125{getCheckIndex(alice, env.seq(alice))};
        env(check::create(alice, bob, usd(125)));
        env.close();

        // alice writes another check that won't get cashed until the transfer
        // rate changes so we can see the rate applies when the check is
        // cashed, not when it is created.
        uint256 const chkId120{getCheckIndex(alice, env.seq(alice))};
        env(check::create(alice, bob, usd(120)));
        env.close();

        // bob attempts to cash the check for face value.  Should fail.
        env(check::cash(bob, chkId125, usd(125)), Ter(tecPATH_PARTIAL));
        env.close();
        env(check::cash(bob, chkId125, check::DeliverMin(usd(101))), Ter(tecPATH_PARTIAL));
        env.close();

        // bob decides that he'll accept anything USD(75) or up.
        // He gets USD(100).
        env(check::cash(bob, chkId125, check::DeliverMin(usd(75))));
        verifyDeliveredAmount(env, usd(100));
        env.require(Balance(alice, usd(1000 - 125)));
        env.require(Balance(bob, usd(0 + 100)));
        BEAST_EXPECT(checksOnAccount(env, alice).size() == 1);
        BEAST_EXPECT(checksOnAccount(env, bob).size() == 1);

        // Adjust gw's rate...
        env(rate(gw, 1.2));
        env.close();

        // bob cashes the second check for less than the face value.  The new
        // rate applies to the actual value transferred.
        env(check::cash(bob, chkId120, usd(50)));
        env.close();
        env.require(Balance(alice, usd(1000 - 125 - 60)));
        env.require(Balance(bob, usd(0 + 100 + 50)));
        BEAST_EXPECT(checksOnAccount(env, alice).empty());
        BEAST_EXPECT(checksOnAccount(env, bob).empty());
    }

    void
    testCashQuality(FeatureBitset features)
    {
        // Look at the eight possible cases for Quality In/Out.
        testcase("Cash quality");

        using namespace test::jtx;

        Account const gw{"gateway"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        IOU const usd{gw["USD"]};

        Env env{*this, features};

        env.fund(XRP(1000), gw, alice, bob);
        env.close();

        env(trust(alice, usd(1000)));
        env(trust(bob, usd(1000)));
        env.close();
        env(pay(gw, alice, usd(1000)));
        env.close();

        //
        // Quality effects on transfers between two non-issuers.
        //

        // Provide lambdas that return a qualityInPercent and qualityOutPercent.
        auto qIn = [](double percent) { return QualityInPercent(percent); };
        auto qOut = [](double percent) { return QualityOutPercent(percent); };

        // There are two test lambdas: one for a Payment and one for a Check.
        // This shows whether a Payment and a Check behave the same.
        auto testNonIssuerQPay = [&env, &alice, &bob, &usd](
                                     Account const& truster,
                                     IOU const& iou,
                                     auto const& inOrOut,
                                     double pct,
                                     double amount) {
            // Capture bob's and alice's balances so we can test at the end.
            STAmount const aliceStart{env.balance(alice, usd).value()};
            STAmount const bobStart{env.balance(bob, usd).value()};

            // Set the modified quality.
            env(trust(truster, iou(1000)), inOrOut(pct));
            env.close();

            env(pay(alice, bob, usd(amount)), Sendmax(usd(10)));
            env.close();
            env.require(Balance(alice, aliceStart - usd(10)));
            env.require(Balance(bob, bobStart + usd(10)));

            // Return the quality to the unmodified state so it doesn't
            // interfere with upcoming tests.
            env(trust(truster, iou(1000)), inOrOut(0));
            env.close();
        };

        auto testNonIssuerQCheck = [&env, &alice, &bob, &usd](
                                       Account const& truster,
                                       IOU const& iou,
                                       auto const& inOrOut,
                                       double pct,
                                       double amount) {
            // Capture bob's and alice's balances so we can test at the end.
            STAmount const aliceStart{env.balance(alice, usd).value()};
            STAmount const bobStart{env.balance(bob, usd).value()};

            // Set the modified quality.
            env(trust(truster, iou(1000)), inOrOut(pct));
            env.close();

            uint256 const chkId = getCheckIndex(alice, env.seq(alice));
            env(check::create(alice, bob, usd(10)));
            env.close();

            env(check::cash(bob, chkId, usd(amount)));
            env.close();
            env.require(Balance(alice, aliceStart - usd(10)));
            env.require(Balance(bob, bobStart + usd(10)));

            // Return the quality to the unmodified state so it doesn't
            // interfere with upcoming tests.
            env(trust(truster, iou(1000)), inOrOut(0));
            env.close();
        };

        //                                           pct  amount
        testNonIssuerQPay(alice, gw["USD"], qIn, 50, 10);
        testNonIssuerQCheck(alice, gw["USD"], qIn, 50, 10);

        // This is the only case where the Quality affects the outcome.
        testNonIssuerQPay(bob, gw["USD"], qIn, 50, 5);
        testNonIssuerQCheck(bob, gw["USD"], qIn, 50, 5);

        testNonIssuerQPay(gw, alice["USD"], qIn, 50, 10);
        testNonIssuerQCheck(gw, alice["USD"], qIn, 50, 10);

        testNonIssuerQPay(gw, bob["USD"], qIn, 50, 10);
        testNonIssuerQCheck(gw, bob["USD"], qIn, 50, 10);

        testNonIssuerQPay(alice, gw["USD"], qOut, 200, 10);
        testNonIssuerQCheck(alice, gw["USD"], qOut, 200, 10);

        testNonIssuerQPay(bob, gw["USD"], qOut, 200, 10);
        testNonIssuerQCheck(bob, gw["USD"], qOut, 200, 10);

        testNonIssuerQPay(gw, alice["USD"], qOut, 200, 10);
        testNonIssuerQCheck(gw, alice["USD"], qOut, 200, 10);

        testNonIssuerQPay(gw, bob["USD"], qOut, 200, 10);
        testNonIssuerQCheck(gw, bob["USD"], qOut, 200, 10);

        //
        // Quality effects on transfers between an issuer and a non-issuer.
        //

        // There are two test lambdas for the same reason as before.
        auto testIssuerQPay = [&env, &gw, &alice, &usd](
                                  Account const& truster,
                                  IOU const& iou,
                                  auto const& inOrOut,
                                  double pct,
                                  double amt1,
                                  double max1,
                                  double amt2,
                                  double max2) {
            // Capture alice's balance so we can test at the end.  It doesn't
            // make any sense to look at the balance of a gateway.
            STAmount const aliceStart{env.balance(alice, usd).value()};

            // Set the modified quality.
            env(trust(truster, iou(1000)), inOrOut(pct));
            env.close();

            // alice pays gw.
            env(pay(alice, gw, usd(amt1)), Sendmax(usd(max1)));
            env.close();
            env.require(Balance(alice, aliceStart - usd(10)));

            // gw pays alice.
            env(pay(gw, alice, usd(amt2)), Sendmax(usd(max2)));
            env.close();
            env.require(Balance(alice, aliceStart));

            // Return the quality to the unmodified state so it doesn't
            // interfere with upcoming tests.
            env(trust(truster, iou(1000)), inOrOut(0));
            env.close();
        };

        auto testIssuerQCheck = [&env, &gw, &alice, &usd](
                                    Account const& truster,
                                    IOU const& iou,
                                    auto const& inOrOut,
                                    double pct,
                                    double amt1,
                                    double max1,
                                    double amt2,
                                    double max2) {
            // Capture alice's balance so we can test at the end.  It doesn't
            // make any sense to look at the balance of the issuer.
            STAmount const aliceStart{env.balance(alice, usd).value()};

            // Set the modified quality.
            env(trust(truster, iou(1000)), inOrOut(pct));
            env.close();

            // alice writes check to gw.  gw cashes.
            uint256 const chkAliceId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, gw, usd(max1)));
            env.close();

            env(check::cash(gw, chkAliceId, usd(amt1)));
            env.close();
            env.require(Balance(alice, aliceStart - usd(10)));

            // gw writes check to alice.  alice cashes.
            uint256 const chkGwId{getCheckIndex(gw, env.seq(gw))};
            env(check::create(gw, alice, usd(max2)));
            env.close();

            env(check::cash(alice, chkGwId, usd(amt2)));
            env.close();
            env.require(Balance(alice, aliceStart));

            // Return the quality to the unmodified state so it doesn't
            // interfere with upcoming tests.
            env(trust(truster, iou(1000)), inOrOut(0));
            env.close();
        };

        // The first case is the only one where the quality affects the outcome.
        //                                        pct  amt1 max1 amt2 max2
        testIssuerQPay(alice, gw["USD"], qIn, 50, 10, 10, 5, 10);
        testIssuerQCheck(alice, gw["USD"], qIn, 50, 10, 10, 5, 10);

        testIssuerQPay(gw, alice["USD"], qIn, 50, 10, 10, 10, 10);
        testIssuerQCheck(gw, alice["USD"], qIn, 50, 10, 10, 10, 10);

        testIssuerQPay(alice, gw["USD"], qOut, 200, 10, 10, 10, 10);
        testIssuerQCheck(alice, gw["USD"], qOut, 200, 10, 10, 10, 10);

        testIssuerQPay(gw, alice["USD"], qOut, 200, 10, 10, 10, 10);
        testIssuerQCheck(gw, alice["USD"], qOut, 200, 10, 10, 10, 10);
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
        IOU const usd{gw["USD"]};

        Env env(*this, features);

        env.fund(XRP(1000), gw, alice, bob, zoe);
        env.close();

        // Now set up alice's trustline.
        env(trust(alice, usd(20)));
        env.close();
        env(pay(gw, alice, usd(20)));
        env.close();

        // Now set up bob's trustline.
        env(trust(bob, usd(20)));
        env.close();

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

        uint256 const chkIdFroz4{getCheckIndex(alice, env.seq(alice))};
        env(check::create(alice, bob, usd(4)));
        env.close();

        uint256 const chkIdFroz4ToIssuer{getCheckIndex(alice, env.seq(alice))};
        env(check::create(alice, gw, usd(4)));
        env.close();

        uint256 const chkIdFroz4Issuer{getCheckIndex(gw, env.seq(gw))};
        env(check::create(gw, alice, usd(4)));
        env.close();

        uint256 const chkIdNoDest1{getCheckIndex(alice, env.seq(alice))};
        env(check::create(alice, bob, usd(1)));
        env.close();

        uint256 const chkIdHasDest2{getCheckIndex(alice, env.seq(alice))};
        env(check::create(alice, bob, usd(2)), DestTag(7));
        env.close();

        // Same set of failing cases for both IOU and XRP check cashing.
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
                IOU const wrongCurrency{gw["EUR"]};
                STAmount badAmount{amount};
                badAmount.setIssue(wrongCurrency);
                env(check::cash(bob, chkId, badAmount), Ter(temMALFORMED));
                env.close();
            }

            // Issuer mismatch.
            {
                IOU const wrongIssuer{alice["USD"]};
                STAmount badAmount{amount};
                badAmount.setIssue(wrongIssuer);
                env(check::cash(bob, chkId, badAmount), Ter(temMALFORMED));
                env.close();
            }

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

        // Can we cash a check with frozen currency?
        {
            env(pay(bob, alice, usd(20)));
            env.close();
            env.require(Balance(alice, usd(20)));
            env.require(Balance(bob, usd(0)));

            // Global freeze
            env(fset(gw, asfGlobalFreeze));
            env.close();

            env(check::cash(bob, chkIdFroz1, usd(1)), Ter(tecPATH_PARTIAL));
            env.close();
            env(check::cash(bob, chkIdFroz1, check::DeliverMin(usd(0.5))), Ter(tecPATH_PARTIAL));
            env.close();

            env(check::cash(gw, chkIdFroz4ToIssuer, usd(1)), Ter(tecPATH_PARTIAL));
            env.close();
            env(check::cash(gw, chkIdFroz4ToIssuer, check::DeliverMin(usd(0.5))),
                Ter(tecPATH_PARTIAL));
            env.close();

            env(check::cash(alice, chkIdFroz4Issuer, usd(1)), Ter(tecFROZEN));
            env.close();
            env(check::cash(alice, chkIdFroz4Issuer, check::DeliverMin(usd(0.5))), Ter(tecFROZEN));
            env.close();

            env(fclear(gw, asfGlobalFreeze));
            env.close();

            // No longer frozen.  Success.
            env(check::cash(bob, chkIdFroz1, usd(1)));
            env.close();
            env.require(Balance(alice, usd(19)));
            env.require(Balance(bob, usd(1)));

            env(check::cash(gw, chkIdFroz4ToIssuer, usd(1)));
            env.close();

            // Freeze individual trustlines.
            env(trust(gw, alice["USD"](0), tfSetFreeze));
            env.close();
            env(check::cash(bob, chkIdFroz2, usd(2)), Ter(tecPATH_PARTIAL));
            env.close();
            env(check::cash(bob, chkIdFroz2, check::DeliverMin(usd(1))), Ter(tecPATH_PARTIAL));
            env.close();

            // Clear that freeze.  Now check cashing works.
            env(trust(gw, alice["USD"](0), tfClearFreeze));
            env.close();
            env(check::cash(bob, chkIdFroz2, usd(2)));
            env.close();
            env.require(Balance(alice, usd(16)));
            env.require(Balance(bob, usd(3)));

            // Freeze bob's trustline.  bob can't cash the check.
            env(trust(gw, bob["USD"](0), tfSetFreeze));
            env.close();
            env(check::cash(bob, chkIdFroz3, usd(3)), Ter(tecFROZEN));
            env.close();
            env(check::cash(bob, chkIdFroz3, check::DeliverMin(usd(1))), Ter(tecFROZEN));
            env.close();

            // Clear that freeze.  Now check cashing works again.
            env(trust(gw, bob["USD"](0), tfClearFreeze));
            env.close();
            env(check::cash(bob, chkIdFroz3, check::DeliverMin(usd(1))));
            verifyDeliveredAmount(env, usd(3));
            env.require(Balance(alice, usd(13)));
            env.require(Balance(bob, usd(6)));

            // Set bob's freeze bit in the other direction.  Check
            // cashing fails.
            env(trust(bob, usd(20), tfSetFreeze));
            env.close();
            env(check::cash(bob, chkIdFroz4, usd(4)), Ter(terNO_LINE));
            env.close();
            env(check::cash(bob, chkIdFroz4, check::DeliverMin(usd(1))), Ter(terNO_LINE));
            env.close();

            // Clear bob's freeze bit and the check should be cashable.
            env(trust(bob, usd(20), tfClearFreeze));
            env.close();
            env(check::cash(bob, chkIdFroz4, usd(4)));
            env.close();
            env.require(Balance(alice, usd(9)));
            env.require(Balance(bob, usd(10)));
        }
        {
            // Set the RequireDest flag on bob's account (after the check
            // was created) then cash a check without a destination tag.
            env(fset(bob, asfRequireDest));
            env.close();
            env(check::cash(bob, chkIdNoDest1, usd(1)), Ter(tecDST_TAG_NEEDED));
            env.close();
            env(check::cash(bob, chkIdNoDest1, check::DeliverMin(usd(0.5))),
                Ter(tecDST_TAG_NEEDED));
            env.close();

            // bob can cash a check with a destination tag.
            env(check::cash(bob, chkIdHasDest2, usd(2)));
            env.close();
            env.require(Balance(alice, usd(7)));
            env.require(Balance(bob, usd(12)));

            // Clear the RequireDest flag on bob's account so he can
            // cash the check with no DestinationTag.
            env(fclear(bob, asfRequireDest));
            env.close();
            env(check::cash(bob, chkIdNoDest1, usd(1)));
            env.close();
            env.require(Balance(alice, usd(6)));
            env.require(Balance(bob, usd(13)));
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
        IOU const usd{gw["USD"]};

        {
            Env env{*this, features};

            env.fund(XRP(1000), gw, alice, bob, zoe);
            env.close();

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

            // alice uses her regular key to cancel a check.
            env(check::cancel(alice, chkIdReg), Sig(alie));
            env.close();
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 3);
            BEAST_EXPECT(ownerCount(env, alice) == 4);

            // alice uses multisigning to cancel a check.
            XRPAmount const baseFeeDrops{env.current()->fees().base};
            env(check::cancel(alice, chkIdMSig), Msig(bogie, demon), Fee(3 * baseFeeDrops));
            env.close();
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 2);
            BEAST_EXPECT(ownerCount(env, alice) == 3);

            // Creator and destination cancel the remaining unexpired checks.
            env(check::cancel(alice, chkId3), Sig(alice));
            env.close();
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 1);
            BEAST_EXPECT(ownerCount(env, alice) == 2);

            env(check::cancel(bob, chkIdNotExp3));
            env.close();
            BEAST_EXPECT(checksOnAccount(env, alice).empty());
            BEAST_EXPECT(ownerCount(env, alice) == 1);
        }
    }

    void
    testCancelInvalid(FeatureBitset features)
    {
        // Explore many of the ways to fail at canceling a check.
        testcase("Cancel invalid");

        using namespace test::jtx;

        Account const alice{"alice"};
        Account const bob{"bob"};

        Env env{*this, features};

        env.fund(XRP(1000), alice, bob);
        env.close();

        // Bad fee.
        env(check::cancel(bob, getCheckIndex(alice, env.seq(alice))),
            Fee(drops(-10)),
            Ter(temBAD_FEE));
        env.close();

        // Bad flags.
        env(check::cancel(bob, getCheckIndex(alice, env.seq(alice))),
            Txflags(tfImmediateOrCancel),
            Ter(temINVALID_FLAG));
        env.close();

        // Non-existent check.
        env(check::cancel(bob, getCheckIndex(alice, env.seq(alice))), Ter(tecNO_ENTRY));
        env.close();
    }

    void
    testDeliveredAmountForCheckCashTxn(FeatureBitset features)
    {
        testcase("DeliveredAmount For CheckCash Txn");

        using namespace test::jtx;
        Account const alice{"alice"};
        Account const bob{"bob"};

        Env env{*this, features};

        env.fund(XRP(1000), alice, bob);
        env.close();

        uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
        env(check::create(alice, bob, XRP(200)));
        env.close();

        env(check::cash(bob, chkId, check::DeliverMin(XRP(100))));

        // Get the hash for the most recent transaction.
        std::string const txHash{
            env.tx()->getJson(JsonOptions::Values::None)[jss::hash].asString()};

        env.close();
        json::Value const meta = env.rpc("tx", txHash)[jss::result][jss::meta];

        // DeliveredAmount and delivered_amount are present.
        BEAST_EXPECT(meta.isMember(sfDeliveredAmount.jsonName));
        BEAST_EXPECT(meta.isMember(jss::delivered_amount));
    }

    void
    testWithTickets(FeatureBitset features)
    {
        testcase("With Tickets");

        using namespace test::jtx;

        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        IOU const usd{gw["USD"]};

        Env env{*this, features};
        env.fund(XRP(1000), gw, alice, bob);
        env.close();

        // alice and bob grab enough tickets for all of the following
        // transactions.  Note that once the tickets are acquired alice's
        // and bob's account sequence numbers should not advance.
        std::uint32_t aliceTicketSeq{env.seq(alice) + 1};
        env(ticket::create(alice, 10));
        std::uint32_t const aliceSeq{env.seq(alice)};

        std::uint32_t bobTicketSeq{env.seq(bob) + 1};
        env(ticket::create(bob, 10));
        std::uint32_t const bobSeq{env.seq(bob)};

        env.close();
        env.require(Owners(alice, 10));
        env.require(Owners(bob, 10));

        // alice gets enough USD to write a few checks.
        env(trust(alice, usd(1000)), ticket::Use(aliceTicketSeq++));
        env(trust(bob, usd(1000)), ticket::Use(bobTicketSeq++));
        env.close();
        env.require(Owners(alice, 10));
        env.require(Owners(bob, 10));

        env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        env.require(tickets(bob, env.seq(bob) - bobTicketSeq));
        BEAST_EXPECT(env.seq(bob) == bobSeq);

        env(pay(gw, alice, usd(900)));
        env.close();

        // alice creates four checks; two XRP, two IOU.  Bob will cash
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
        env.require(Owners(alice, 10));
        env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
        BEAST_EXPECT(checksOnAccount(env, alice).size() == 4);
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        env.require(Owners(bob, 10));
        BEAST_EXPECT(env.seq(bob) == bobSeq);

        // Bob cancels two of alice's checks.
        env(check::cancel(bob, chkIdXrp1), ticket::Use(bobTicketSeq++));
        env(check::cancel(bob, chkIdUsd2), ticket::Use(bobTicketSeq++));
        env.close();

        env.require(Owners(alice, 8));
        env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
        BEAST_EXPECT(checksOnAccount(env, alice).size() == 2);
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        env.require(Owners(bob, 8));
        BEAST_EXPECT(env.seq(bob) == bobSeq);

        // Bob cashes alice's two remaining checks.
        env(check::cash(bob, chkIdXrp2, XRP(300)), ticket::Use(bobTicketSeq++));
        env(check::cash(bob, chkIdUsd1, usd(200)), ticket::Use(bobTicketSeq++));
        env.close();

        env.require(Owners(alice, 6));
        env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
        BEAST_EXPECT(checksOnAccount(env, alice).empty());
        BEAST_EXPECT(env.seq(alice) == aliceSeq);
        env.require(Balance(alice, usd(700)));

        env.require(Owners(bob, 6));
        BEAST_EXPECT(env.seq(bob) == bobSeq);
        env.require(Balance(bob, usd(200)));
    }

    void
    testTrustLineCreation(FeatureBitset features)
    {
        // Explore automatic trust line creation when a check is cashed.
        //

        testcase("Trust Line Creation");

        using namespace test::jtx;

        Env env{*this, features};

        // An account that independently tracks its owner count.
        struct AccountOwns
        {
            beast::unit_test::Suite& suite;
            Env const& env;
            Account const acct;
            std::size_t owners;

            void
            verifyOwners(std::uint32_t line) const
            {
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

            IOU
            operator[](std::string const& s) const
            {
                return acct[s];
            }
        };

        AccountOwns alice{.suite = *this, .env = env, .acct = "alice", .owners = 0};
        AccountOwns bob{.suite = *this, .env = env, .acct = "bob", .owners = 0};

        // Fund with noripple so the accounts do not have any flags set.
        env.fund(XRP(5000), noripple(alice, bob));
        env.close();

        // Automatic trust line creation should fail if the check destination
        // can't afford the reserve for the trust line.
        {
            AccountOwns const gw1{.suite = *this, .env = env, .acct = "gw1", .owners = 0};

            // Fund gw1 with noripple (even though that's atypical for a
            // gateway) so it does not have any flags set.  We'll set flags
            // on gw1 later.
            env.fund(XRP(5000), noripple(gw1));
            env.close();

            IOU const cK8 = gw1["CK8"];
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

            env(check::cash(yui, chkId, cK8(99)), Ter(tecNO_LINE_INSUF_RESERVE));
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

        // We'll be looking at the effects of various account root flags.

        // Automatically create trust lines using
        //   o Offers and
        //   o Check cashing
        // Compare the resulting trust lines and expect them to be very similar.

        // Lambda that compares two trust lines created by
        //  o Offer crossing and
        //  o Check cashing
        // between the same two accounts but with two different currencies.
        // The lambda expects the two trust lines to be largely similar.
        auto cmpTrustLines = [this, &env](
                                 Account const& acct1,
                                 Account const& acct2,
                                 IOU const& offerIou,
                                 IOU const& checkIou) {
            auto const offerLine = env.le(keylet::line(acct1, acct2, offerIou.currency));
            auto const checkLine = env.le(keylet::line(acct1, acct2, checkIou.currency));
            if (offerLine == nullptr || checkLine == nullptr)
            {
                BEAST_EXPECT(offerLine == nullptr && checkLine == nullptr);
                return;
            }

            {
                // Compare the contents of required fields.
                BEAST_EXPECT(offerLine->at(sfFlags) == checkLine->at(sfFlags));

                // Lambda that compares the contents of required STAmounts
                // without comparing the currency.
                auto cmpReqAmount = [this, offerLine, checkLine](SF_AMOUNT const& sfield) {
                    STAmount const offerAmount = offerLine->at(sfield);
                    STAmount const checkAmount = checkLine->at(sfield);

                    // Neither STAmount should be native.
                    if (!BEAST_EXPECT(!offerAmount.native() && !checkAmount.native()))
                        return;

                    BEAST_EXPECT(offerAmount.getIssuer() == checkAmount.getIssuer());
                    BEAST_EXPECT(offerAmount.negative() == checkAmount.negative());
                    BEAST_EXPECT(offerAmount.mantissa() == checkAmount.mantissa());
                    BEAST_EXPECT(offerAmount.exponent() == checkAmount.exponent());
                };
                cmpReqAmount(sfBalance);
                cmpReqAmount(sfLowLimit);
                cmpReqAmount(sfHighLimit);
            }
            {
                // Lambda that compares the contents of optional fields.
                auto cmpOptField = [this, offerLine, checkLine](auto const& sfield) {
                    // Expect both fields to either be present or absent.
                    if (!BEAST_EXPECT(
                            offerLine->isFieldPresent(sfield) == checkLine->isFieldPresent(sfield)))
                        return;

                    // If both fields are absent then there's nothing
                    // further to check.
                    if (!offerLine->isFieldPresent(sfield))
                        return;

                    // Both optional fields are present so we can compare
                    // them.
                    BEAST_EXPECT(offerLine->at(sfield) == checkLine->at(sfield));
                };
                cmpOptField(sfLowNode);
                cmpOptField(sfLowQualityIn);
                cmpOptField(sfLowQualityOut);

                cmpOptField(sfHighNode);
                cmpOptField(sfHighQualityIn);
                cmpOptField(sfHighQualityOut);
            }
        };

        //----------- No account root flags, check written by issuer -----------
        {
            // No account root flags on any participant.
            // Automatic trust line from issuer to destination.
            AccountOwns const gw1{.suite = *this, .env = env, .acct = "gw1", .owners = 0};

            BEAST_EXPECT((*env.le(gw1))[sfFlags] == 0);
            BEAST_EXPECT((*env.le(alice))[sfFlags] == 0);
            BEAST_EXPECT((*env.le(bob))[sfFlags] == 0);

            // Use offers to automatically create the trust line.
            IOU const oF1 = gw1["OF1"];
            env(offer(gw1, XRP(98), oF1(98)));
            env.close();
            BEAST_EXPECT(env.le(keylet::line(gw1, alice, oF1.currency)) == nullptr);
            env(offer(alice, oF1(98), XRP(98)));
            ++alice.owners;
            env.close();

            // Both offers should be consumed.
            // Since gw1's offer was consumed and the trust line was not
            // created by gw1, gw1's owner count should be 0.
            gw1.verifyOwners(__LINE__);

            // alice's automatically created trust line bumps her owner count.
            alice.verifyOwners(__LINE__);

            // Use check cashing to automatically create the trust line.
            IOU const cK1 = gw1["CK1"];
            uint256 const chkId{getCheckIndex(gw1, env.seq(gw1))};
            env(check::create(gw1, alice, cK1(98)));
            env.close();
            BEAST_EXPECT(env.le(keylet::line(gw1, alice, cK1.currency)) == nullptr);
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

            cmpTrustLines(gw1, alice, oF1, cK1);
        }
        //--------- No account root flags, check written by non-issuer ---------
        {
            // No account root flags on any participant.
            // Automatic trust line from non-issuer to non-issuer.

            // Use offers to automatically create the trust line.
            // Transfer of assets using offers does not require rippling.
            // So bob's offer is successfully crossed which creates the
            // trust line.
            AccountOwns const gw1{.suite = *this, .env = env, .acct = "gw1", .owners = 0};
            IOU const oF1 = gw1["OF1"];
            env(offer(alice, XRP(97), oF1(97)));
            env.close();
            BEAST_EXPECT(env.le(keylet::line(alice, bob, oF1.currency)) == nullptr);
            env(offer(bob, oF1(97), XRP(97)));
            ++bob.owners;
            env.close();

            // Both offers should be consumed.
            env.require(Balance(alice, oF1(1)));
            env.require(Balance(bob, oF1(97)));

            // bob now has an owner count of 1 due to the new trust line.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Use check cashing to automatically create the trust line.
            //
            // However cashing a check (unlike crossing offers) requires
            // rippling through the currency's issuer.  Since gw1 does not
            // have rippling enabled the check cash fails and bob does not
            // have a trust line created.
            IOU const cK1 = gw1["CK1"];
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, cK1(97)));
            env.close();
            BEAST_EXPECT(env.le(keylet::line(alice, bob, cK1.currency)) == nullptr);
            env(check::cash(bob, chkId, cK1(97)), Ter(terNO_RIPPLE));
            env.close();

            BEAST_EXPECT(env.le(keylet::line(gw1, bob, oF1.currency)) != nullptr);
            BEAST_EXPECT(env.le(keylet::line(gw1, bob, cK1.currency)) == nullptr);

            // Delete alice's check since it is no longer needed.
            env(check::cancel(alice, chkId));
            env.close();

            // No one's owner count should have changed.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);
        }

        //------------- lsfDefaultRipple, check written by issuer --------------
        {
            // gw1 enables rippling.
            // Automatic trust line from issuer to non-issuer should still work.
            AccountOwns const gw1{.suite = *this, .env = env, .acct = "gw1", .owners = 0};
            env(fset(gw1, asfDefaultRipple));
            env.close();

            // Use offers to automatically create the trust line.
            IOU const oF2 = gw1["OF2"];
            env(offer(gw1, XRP(96), oF2(96)));
            env.close();
            BEAST_EXPECT(env.le(keylet::line(gw1, alice, oF2.currency)) == nullptr);
            env(offer(alice, oF2(96), XRP(96)));
            ++alice.owners;
            env.close();

            // Both offers should be consumed.
            // Since gw1's offer was consumed and the trust line was not
            // created by gw1, gw1's owner count should still be 0.
            gw1.verifyOwners(__LINE__);

            // alice's automatically created trust line bumps her owner count.
            alice.verifyOwners(__LINE__);

            // Use check cashing to automatically create the trust line.
            IOU const cK2 = gw1["CK2"];
            uint256 const chkId{getCheckIndex(gw1, env.seq(gw1))};
            env(check::create(gw1, alice, cK2(96)));
            env.close();
            BEAST_EXPECT(env.le(keylet::line(gw1, alice, cK2.currency)) == nullptr);
            env(check::cash(alice, chkId, cK2(96)));
            ++alice.owners;
            verifyDeliveredAmount(env, cK2(96));
            env.close();

            // gw1's check should be consumed.
            // Since gw1's check was consumed and the trust line was not
            // created by gw1, gw1's owner count should still be 0.
            gw1.verifyOwners(__LINE__);

            // alice's automatically created trust line bumps her owner count.
            alice.verifyOwners(__LINE__);

            cmpTrustLines(gw1, alice, oF2, cK2);
        }
        //----------- lsfDefaultRipple, check written by non-issuer ------------
        {
            // gw1 enabled rippling, so automatic trust line from non-issuer
            // to non-issuer should work.

            // Use offers to automatically create the trust line.
            AccountOwns const gw1{.suite = *this, .env = env, .acct = "gw1", .owners = 0};
            IOU const oF2 = gw1["OF2"];
            env(offer(alice, XRP(95), oF2(95)));
            env.close();
            BEAST_EXPECT(env.le(keylet::line(alice, bob, oF2.currency)) == nullptr);
            env(offer(bob, oF2(95), XRP(95)));
            ++bob.owners;
            env.close();

            // bob's owner count should increase due to the new trust line.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Use check cashing to automatically create the trust line.
            IOU const cK2 = gw1["CK2"];
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, cK2(95)));
            env.close();
            BEAST_EXPECT(env.le(keylet::line(alice, bob, cK2.currency)) == nullptr);
            env(check::cash(bob, chkId, cK2(95)));
            ++bob.owners;
            verifyDeliveredAmount(env, cK2(95));
            env.close();

            // bob's owner count should increase due to the new trust line.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            cmpTrustLines(alice, bob, oF2, cK2);
        }

        //-------------- lsfDepositAuth, check written by issuer ---------------
        {
            // Both offers and checks ignore the lsfDepositAuth flag, since
            // the destination signs the transaction that delivers their funds.
            // So setting lsfDepositAuth on all the participants should not
            // change any outcomes.
            //
            // Automatic trust line from issuer to non-issuer should still work.
            AccountOwns const gw1{.suite = *this, .env = env, .acct = "gw1", .owners = 0};
            env(fset(gw1, asfDepositAuth));
            env(fset(alice, asfDepositAuth));
            env(fset(bob, asfDepositAuth));
            env.close();

            // Use offers to automatically create the trust line.
            IOU const oF3 = gw1["OF3"];
            env(offer(gw1, XRP(94), oF3(94)));
            env.close();
            BEAST_EXPECT(env.le(keylet::line(gw1, alice, oF3.currency)) == nullptr);
            env(offer(alice, oF3(94), XRP(94)));
            ++alice.owners;
            env.close();

            // Both offers should be consumed.
            // Since gw1's offer was consumed and the trust line was not
            // created by gw1, gw1's owner count should still be 0.
            gw1.verifyOwners(__LINE__);

            // alice's automatically created trust line bumps her owner count.
            alice.verifyOwners(__LINE__);

            // Use check cashing to automatically create the trust line.
            IOU const cK3 = gw1["CK3"];
            uint256 const chkId{getCheckIndex(gw1, env.seq(gw1))};
            env(check::create(gw1, alice, cK3(94)));
            env.close();
            BEAST_EXPECT(env.le(keylet::line(gw1, alice, cK3.currency)) == nullptr);
            env(check::cash(alice, chkId, cK3(94)));
            ++alice.owners;
            verifyDeliveredAmount(env, cK3(94));
            env.close();

            // gw1's check should be consumed.
            // Since gw1's check was consumed and the trust line was not
            // created by gw1, gw1's owner count should still be 0.
            gw1.verifyOwners(__LINE__);

            // alice's automatically created trust line bumps her owner count.
            alice.verifyOwners(__LINE__);

            cmpTrustLines(gw1, alice, oF3, cK3);
        }
        //------------ lsfDepositAuth, check written by non-issuer -------------
        {
            // The presence of the lsfDepositAuth flag should not affect
            // automatic trust line creation.

            // Use offers to automatically create the trust line.
            AccountOwns const gw1{.suite = *this, .env = env, .acct = "gw1", .owners = 0};
            IOU const oF3 = gw1["OF3"];
            env(offer(alice, XRP(93), oF3(93)));
            env.close();
            BEAST_EXPECT(env.le(keylet::line(alice, bob, oF3.currency)) == nullptr);
            env(offer(bob, oF3(93), XRP(93)));
            ++bob.owners;
            env.close();

            // bob's owner count should increase due to the new trust line.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Use check cashing to automatically create the trust line.
            IOU const cK3 = gw1["CK3"];
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, cK3(93)));
            env.close();
            BEAST_EXPECT(env.le(keylet::line(alice, bob, cK3.currency)) == nullptr);
            env(check::cash(bob, chkId, cK3(93)));
            ++bob.owners;
            verifyDeliveredAmount(env, cK3(93));
            env.close();

            // bob's owner count should increase due to the new trust line.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            cmpTrustLines(alice, bob, oF3, cK3);
        }

        //-------------- lsfGlobalFreeze, check written by issuer --------------
        {
            // Set lsfGlobalFreeze on gw1.  That should stop any automatic
            // trust lines from being created.
            AccountOwns const gw1{.suite = *this, .env = env, .acct = "gw1", .owners = 0};
            env(fset(gw1, asfGlobalFreeze));
            env.close();

            // Use offers to automatically create the trust line.
            IOU const oF4 = gw1["OF4"];
            env(offer(gw1, XRP(92), oF4(92)), Ter(tecFROZEN));
            env.close();
            BEAST_EXPECT(env.le(keylet::line(gw1, alice, oF4.currency)) == nullptr);
            env(offer(alice, oF4(92), XRP(92)), Ter(tecFROZEN));
            env.close();

            // No one's owner count should have changed.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Use check cashing to automatically create the trust line.
            IOU const cK4 = gw1["CK4"];
            uint256 const chkId{getCheckIndex(gw1, env.seq(gw1))};
            env(check::create(gw1, alice, cK4(92)), Ter(tecFROZEN));
            env.close();
            BEAST_EXPECT(env.le(keylet::line(gw1, alice, cK4.currency)) == nullptr);
            env(check::cash(alice, chkId, cK4(92)), Ter(tecNO_ENTRY));
            env.close();

            // No one's owner count should have changed.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Because gw1 has set lsfGlobalFreeze, neither trust line
            // is created.
            BEAST_EXPECT(env.le(keylet::line(gw1, alice, oF4.currency)) == nullptr);
            BEAST_EXPECT(env.le(keylet::line(gw1, alice, cK4.currency)) == nullptr);
        }
        //------------ lsfGlobalFreeze, check written by non-issuer ------------
        {
            // Since gw1 has the lsfGlobalFreeze flag set, there should be
            // no automatic trust line creation between non-issuers.

            // Use offers to automatically create the trust line.
            AccountOwns const gw1{.suite = *this, .env = env, .acct = "gw1", .owners = 0};
            IOU const oF4 = gw1["OF4"];
            env(offer(alice, XRP(91), oF4(91)), Ter(tecFROZEN));
            env.close();
            BEAST_EXPECT(env.le(keylet::line(alice, bob, oF4.currency)) == nullptr);
            env(offer(bob, oF4(91), XRP(91)), Ter(tecFROZEN));
            env.close();

            // No one's owner count should have changed.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Use check cashing to automatically create the trust line.
            IOU const cK4 = gw1["CK4"];
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, cK4(91)), Ter(tecFROZEN));
            env.close();
            BEAST_EXPECT(env.le(keylet::line(alice, bob, cK4.currency)) == nullptr);
            env(check::cash(bob, chkId, cK4(91)), Ter(tecNO_ENTRY));
            env.close();

            // No one's owner count should have changed.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Because gw1 has set lsfGlobalFreeze, neither trust line
            // is created.
            BEAST_EXPECT(env.le(keylet::line(gw1, bob, oF4.currency)) == nullptr);
            BEAST_EXPECT(env.le(keylet::line(gw1, bob, cK4.currency)) == nullptr);
        }

        //-------------- lsfRequireAuth, check written by issuer ---------------

        // We want to test the lsfRequireAuth flag, but we can't set that
        // flag on an account that already has trust lines.  So we'll fund
        // a new gateway and use that.
        {
            AccountOwns gw2{.suite = *this, .env = env, .acct = "gw2", .owners = 0};
            env.fund(XRP(5000), gw2);
            env.close();

            // Set lsfRequireAuth on gw2.  That should stop any automatic
            // trust lines from being created.
            env(fset(gw2, asfRequireAuth));
            env.close();

            // Use offers to automatically create the trust line.
            IOU const oF5 = gw2["OF5"];
            std::uint32_t const gw2OfferSeq = {env.seq(gw2)};
            env(offer(gw2, XRP(92), oF5(92)));
            ++gw2.owners;
            env.close();
            BEAST_EXPECT(env.le(keylet::line(gw2, alice, oF5.currency)) == nullptr);
            env(offer(alice, oF5(92), XRP(92)), Ter(tecNO_LINE));
            env.close();

            // gw2 should still own the offer, but no one else's owner
            // count should have changed.
            gw2.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Since we don't need it any more, remove gw2's offer.
            env(offerCancel(gw2, gw2OfferSeq));
            --gw2.owners;
            env.close();
            gw2.verifyOwners(__LINE__);

            // Use check cashing to automatically create the trust line.
            IOU const cK5 = gw2["CK5"];
            uint256 const chkId{getCheckIndex(gw2, env.seq(gw2))};
            env(check::create(gw2, alice, cK5(92)));
            ++gw2.owners;
            env.close();
            BEAST_EXPECT(env.le(keylet::line(gw2, alice, cK5.currency)) == nullptr);
            env(check::cash(alice, chkId, cK5(92)), Ter(tecNO_AUTH));
            env.close();

            // gw2 should still own the check, but no one else's owner
            // count should have changed.
            gw2.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Because gw2 has set lsfRequireAuth, neither trust line
            // is created.
            BEAST_EXPECT(env.le(keylet::line(gw2, alice, oF5.currency)) == nullptr);
            BEAST_EXPECT(env.le(keylet::line(gw2, alice, cK5.currency)) == nullptr);

            // Since we don't need it any more, remove gw2's check.
            env(check::cancel(gw2, chkId));
            --gw2.owners;
            env.close();
            gw2.verifyOwners(__LINE__);
        }
        //------------ lsfRequireAuth, check written by non-issuer -------------
        {
            // Since gw2 has the lsfRequireAuth flag set, there should be
            // no automatic trust line creation between non-issuers.

            // Use offers to automatically create the trust line.
            AccountOwns const gw2{.suite = *this, .env = env, .acct = "gw2", .owners = 0};
            IOU const oF5 = gw2["OF5"];
            env(offer(alice, XRP(91), oF5(91)), Ter(tecUNFUNDED_OFFER));
            env.close();
            env(offer(bob, oF5(91), XRP(91)), Ter(tecNO_LINE));
            BEAST_EXPECT(env.le(keylet::line(gw2, bob, oF5.currency)) == nullptr);
            env.close();

            gw2.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Use check cashing to automatically create the trust line.
            IOU const cK5 = gw2["CK5"];
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, cK5(91)));
            env.close();
            BEAST_EXPECT(env.le(keylet::line(alice, bob, cK5.currency)) == nullptr);
            env(check::cash(bob, chkId, cK5(91)), Ter(tecPATH_PARTIAL));
            env.close();

            // Delete alice's check since it is no longer needed.
            env(check::cancel(alice, chkId));
            env.close();

            // No one's owner count should have changed.
            gw2.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Because gw2 has set lsfRequireAuth, neither trust line
            // is created.
            BEAST_EXPECT(env.le(keylet::line(gw2, bob, oF5.currency)) == nullptr);
            BEAST_EXPECT(env.le(keylet::line(gw2, bob, cK5.currency)) == nullptr);
        }
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testEnabled(features);
        testCreateValid(features);
        testCreateDisallowIncoming(features);
        testCreateInvalid(features);
        testCashXRP(features);
        testCashIOU(features);
        testCashXferFee(features);
        testCashQuality(features);
        testCashInvalid(features);
        testCancelValid(features);
        testCancelInvalid(features);
        testDeliveredAmountForCheckCashTxn(features);
        testWithTickets(features);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        auto const sa = testableAmendments();
        testWithFeats(sa);
        testTrustLineCreation(sa);
    }
};

BEAST_DEFINE_TESTSUITE(Check, app, xrpl);

}  // namespace xrpl

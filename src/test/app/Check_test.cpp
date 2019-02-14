//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2012-2017 Ripple Labs Inc.

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
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/jss.h>

namespace ripple {
namespace test {
namespace jtx {

/** Set Expiration on a JTx. */
class expiration
{
private:
    std::uint32_t const expry_;

public:
    explicit expiration (NetClock::time_point const& expiry)
        : expry_{expiry.time_since_epoch().count()}
    {
    }

    void
    operator()(Env&, JTx& jt) const
    {
        jt[sfExpiration.jsonName] = expry_;
    }
};

/** Set SourceTag on a JTx. */
class source_tag
{
private:
    std::uint32_t const tag_;

public:
    explicit source_tag (std::uint32_t tag)
        : tag_{tag}
    {
    }

    void
    operator()(Env&, JTx& jt) const
    {
        jt[sfSourceTag.jsonName] = tag_;
    }
};

/** Set DestinationTag on a JTx. */
class dest_tag
{
private:
    std::uint32_t const tag_;

public:
    explicit dest_tag (std::uint32_t tag)
        : tag_{tag}
    {
    }

    void
    operator()(Env&, JTx& jt) const
    {
        jt[sfDestinationTag.jsonName] = tag_;
    }
};

/** Set InvoiceID on a JTx. */
class invoice_id
{
private:
    uint256 const id_;

public:
    explicit invoice_id (uint256 const& id)
        : id_{id}
    {
    }

    void
    operator()(Env&, JTx& jt) const
    {
        jt[sfInvoiceID.jsonName] = to_string (id_);
    }
};

} // namespace jtx
} // namespace test

class Check_test : public beast::unit_test::suite
{
    // Helper function that returns the Checks on an account.
    static std::vector<std::shared_ptr<SLE const>>
    checksOnAccount (test::jtx::Env& env, test::jtx::Account account)
    {
        std::vector<std::shared_ptr<SLE const>> result;
        forEachItem (*env.current (), account,
            [&result](std::shared_ptr<SLE const> const& sle)
            {
                if (sle->getType() == ltCHECK)
                     result.push_back (sle);
            });
        return result;
    }

    // Helper function that returns the owner count on an account.
    static std::uint32_t
    ownerCount (test::jtx::Env const& env, test::jtx::Account const& account)
    {
        std::uint32_t ret {0};
        if (auto const sleAccount = env.le(account))
            ret = sleAccount->getFieldU32(sfOwnerCount);
        return ret;
    }

    // Helper function that verifies the expected DeliveredAmount is present.
    //
    // NOTE: the function _infers_ the transaction to operate on by calling
    // env.tx(), which returns the result from the most recent transaction.
    void verifyDeliveredAmount (test::jtx::Env& env, STAmount const& amount)
    {
        // Get the hash for the most recent transaction.
        std::string const txHash {
            env.tx()->getJson (JsonOptions::none)[jss::hash].asString()};

        // Verify DeliveredAmount and delivered_amount metadata are correct.
        env.close();
        Json::Value const meta = env.rpc ("tx", txHash)[jss::result][jss::meta];

        // Expect there to be a DeliveredAmount field.
        if (! BEAST_EXPECT(meta.isMember (sfDeliveredAmount.jsonName)))
            return;

        // DeliveredAmount and delivered_amount should both be present and
        // equal amount.
        BEAST_EXPECT (meta[sfDeliveredAmount.jsonName] ==
            amount.getJson (JsonOptions::none));
        BEAST_EXPECT (meta[jss::delivered_amount] ==
            amount.getJson (JsonOptions::none));
    }

    void testEnabled()
    {
        testcase ("Enabled");

        using namespace test::jtx;
        Account const alice {"alice"};
        {
            // If the Checks amendment is not enabled, you should not be able
            // to create, cash, or cancel checks.
            Env env {*this, supported_amendments() - featureChecks};
            auto const closeTime =
                fix1449Time() + 100 * env.closed()->info().closeTimeResolution;
            env.close (closeTime);

            env.fund (XRP(1000), alice);

            uint256 const checkId {
                getCheckIndex (env.master, env.seq (env.master))};
            env (check::create (env.master, alice, XRP(100)),
                ter(temDISABLED));
            env.close();

            env (check::cash (alice, checkId, XRP(100)),
                ter (temDISABLED));
            env.close();

            env (check::cancel (alice, checkId), ter (temDISABLED));
            env.close();
        }
        {
            // If the Checks amendment is enabled all check-related
            // facilities should be available.
            Env env {*this};
            auto const closeTime =
                fix1449Time() + 100 * env.closed()->info().closeTimeResolution;
            env.close (closeTime);

            env.fund (XRP(1000), alice);

            uint256 const checkId1 {
                getCheckIndex (env.master, env.seq (env.master))};
            env (check::create (env.master, alice, XRP(100)));
            env.close();

            env (check::cash (alice, checkId1, XRP(100)));
            env.close();

            uint256 const checkId2 {
                getCheckIndex (env.master, env.seq (env.master))};
            env (check::create (env.master, alice, XRP(100)));
            env.close();

            env (check::cancel (alice, checkId2));
            env.close();
        }
    }

    void testCreateValid()
    {
        // Explore many of the valid ways to create a check.
        testcase ("Create valid");

        using namespace test::jtx;

        Account const gw {"gateway"};
        Account const alice {"alice"};
        Account const bob {"bob"};
        IOU const USD {gw["USD"]};

        Env env {*this};
        auto const closeTime =
            fix1449Time() + 100 * env.closed()->info().closeTimeResolution;
        env.close (closeTime);

        STAmount const startBalance {XRP(1000).value()};
        env.fund (startBalance, gw, alice, bob);

        // Note that no trust line has been set up for alice, but alice can
        // still write a check for USD.  You don't have to have the funds
        // necessary to cover a check in order to write a check.
        auto writeTwoChecks =
            [&env, &USD, this] (Account const& from, Account const& to)
        {
            std::uint32_t const fromOwnerCount {ownerCount (env, from)};
            std::uint32_t const toOwnerCount   {ownerCount (env, to  )};

            std::size_t const fromCkCount {checksOnAccount (env, from).size()};
            std::size_t const toCkCount   {checksOnAccount (env, to  ).size()};

            env (check::create (from, to, XRP(2000)));
            env.close();

            env (check::create (from, to, USD(50)));
            env.close();

            BEAST_EXPECT (
                checksOnAccount (env, from).size() == fromCkCount + 2);
            BEAST_EXPECT (
                checksOnAccount (env, to  ).size() == toCkCount   + 2);

            env.require (owners (from, fromOwnerCount + 2));
            env.require (owners (to,
                to == from ? fromOwnerCount + 2 : toOwnerCount));
        };
                    //  from     to
        writeTwoChecks (alice,   bob);
        writeTwoChecks (gw,    alice);
        writeTwoChecks (alice,    gw);

        // Now try adding the various optional fields.  There's no
        // expected interaction between these optional fields; other than
        // the expiration, they are just plopped into the ledger.  So I'm
        // not looking at interactions.
        using namespace std::chrono_literals;
        std::size_t const aliceCount {checksOnAccount (env, alice).size()};
        std::size_t const bobCount   {checksOnAccount (env,   bob).size()};
        env (check::create (alice, bob, USD(50)), expiration (env.now() + 1s));
        env.close();

        env (check::create (alice, bob, USD(50)), source_tag (2));
        env.close();
        env (check::create (alice, bob, USD(50)), dest_tag (3));
        env.close();
        env (check::create (alice, bob, USD(50)), invoice_id (uint256{4}));
        env.close();
        env (check::create (alice, bob, USD(50)), expiration (env.now() + 1s),
            source_tag (12), dest_tag (13), invoice_id (uint256{4}));
        env.close();

        BEAST_EXPECT (checksOnAccount (env, alice).size() == aliceCount + 5);
        BEAST_EXPECT (checksOnAccount (env, bob  ).size() == bobCount   + 5);

        // Use a regular key and also multisign to create a check.
        Account const alie {"alie", KeyType::ed25519};
        env (regkey (alice, alie));
        env.close();

        Account const bogie {"bogie", KeyType::secp256k1};
        Account const demon {"demon", KeyType::ed25519};
        env (signers (alice, 2, {{bogie, 1}, {demon, 1}}), sig (alie));
        env.close();

        // alice uses her regular key to create a check.
        env (check::create (alice, bob, USD(50)), sig (alie));
        env.close();
        BEAST_EXPECT (checksOnAccount (env, alice).size() == aliceCount + 6);
        BEAST_EXPECT (checksOnAccount (env, bob  ).size() == bobCount   + 6);

        // alice uses multisigning to create a check.
        XRPAmount const baseFeeDrops {env.current()->fees().base};
        env (check::create (alice, bob, USD(50)),
            msig (bogie, demon), fee (3 * baseFeeDrops));
        env.close();
        BEAST_EXPECT (checksOnAccount (env, alice).size() == aliceCount + 7);
        BEAST_EXPECT (checksOnAccount (env, bob  ).size() == bobCount   + 7);
    }

    void testCreateInvalid()
    {
        // Explore many of the invalid ways to create a check.
        testcase ("Create invalid");

        using namespace test::jtx;

        Account const gw1 {"gateway1"};
        Account const gwF {"gatewayFrozen"};
        Account const alice {"alice"};
        Account const bob {"bob"};
        IOU const USD {gw1["USD"]};

        Env env {*this};
        auto const closeTime =
            fix1449Time() + 100 * env.closed()->info().closeTimeResolution;
        env.close (closeTime);

        STAmount const startBalance {XRP(1000).value()};
        env.fund (startBalance, gw1, gwF, alice, bob);

        // Bad fee.
        env (check::create (alice, bob, USD(50)), fee (drops(-10)),
            ter (temBAD_FEE));
        env.close();

        // Bad flags.
        env (check::create (alice, bob, USD(50)),
            txflags (tfImmediateOrCancel), ter (temINVALID_FLAG));
        env.close();

        // Check to self.
        env (check::create (alice, alice, XRP(10)), ter (temREDUNDANT));
        env.close();

        // Bad amount.
        env (check::create (alice, bob, drops(-1)), ter (temBAD_AMOUNT));
        env.close();

        env (check::create (alice, bob, drops(0)), ter (temBAD_AMOUNT));
        env.close();

        env (check::create (alice, bob, drops(1)));
        env.close();

        env (check::create (alice, bob, USD(-1)), ter (temBAD_AMOUNT));
        env.close();

        env (check::create (alice, bob, USD(0)), ter (temBAD_AMOUNT));
        env.close();

        env (check::create (alice, bob, USD(1)));
        env.close();
        {
            IOU const BAD {gw1, badCurrency()};
            env (check::create (alice, bob, BAD(2)), ter (temBAD_CURRENCY));
            env.close();
        }

        // Bad expiration.
        env (check::create (alice, bob, USD(50)),
            expiration (NetClock::time_point{}), ter (temBAD_EXPIRATION));
        env.close();

        // Destination does not exist.
        Account const bogie {"bogie"};
        env (check::create (alice, bogie, USD(50)), ter (tecNO_DST));
        env.close();

        // Require destination tag.
        env (fset (bob, asfRequireDest));
        env.close();

        env (check::create (alice, bob, USD(50)), ter (tecDST_TAG_NEEDED));
        env.close();

        env (check::create (alice, bob, USD(50)), dest_tag(11));
        env.close();

        env (fclear (bob, asfRequireDest));
        env.close();
        {
            // Globally frozen asset.
            IOU const USF {gwF["USF"]};
            env (fset(gwF, asfGlobalFreeze));
            env.close();

            env (check::create (alice, bob, USF(50)), ter (tecFROZEN));
            env.close();

            env (fclear(gwF, asfGlobalFreeze));
            env.close();

            env (check::create (alice, bob, USF(50)));
            env.close();
        }
        {
            // Frozen trust line.  Check creation should be similar to payment
            // behavior in the face of frozen trust lines.
            env.trust (USD(1000), alice);
            env.trust (USD(1000), bob);
            env.close();
            env (pay (gw1, alice, USD(25)));
            env (pay (gw1, bob, USD(25)));
            env.close();

            // Setting trustline freeze in one direction prevents alice from
            // creating a check for USD.  But bob and gw1 should still be able
            // to create a check for USD to alice.
            env (trust(gw1, alice["USD"](0), tfSetFreeze));
            env.close();
            env (check::create (alice, bob, USD(50)), ter (tecFROZEN));
            env.close();
            env (pay (alice, bob, USD(1)), ter (tecPATH_DRY));
            env.close();
            env (check::create (bob, alice, USD(50)));
            env.close();
            env (pay (bob, alice, USD(1)));
            env.close();
            env (check::create (gw1, alice, USD(50)));
            env.close();
            env (pay (gw1, alice, USD(1)));
            env.close();

            // Clear that freeze.  Now check creation works.
            env (trust(gw1, alice["USD"](0), tfClearFreeze));
            env.close();
            env (check::create (alice, bob, USD(50)));
            env.close();
            env (check::create (bob, alice, USD(50)));
            env.close();
            env (check::create (gw1, alice, USD(50)));
            env.close();

            // Freezing in the other direction does not effect alice's USD
            // check creation, but prevents bob and gw1 from writing a check
            // for USD to alice.
            env (trust(alice, USD(0), tfSetFreeze));
            env.close();
            env (check::create (alice, bob, USD(50)));
            env.close();
            env (pay (alice, bob, USD(1)));
            env.close();
            env (check::create (bob, alice, USD(50)), ter (tecFROZEN));
            env.close();
            env (pay (bob, alice, USD(1)), ter (tecPATH_DRY));
            env.close();
            env (check::create (gw1, alice, USD(50)), ter (tecFROZEN));
            env.close();
            env (pay (gw1, alice, USD(1)), ter (tecPATH_DRY));
            env.close();

            // Clear that freeze.
            env(trust(alice, USD(0), tfClearFreeze));
            env.close();
        }

        // Expired expiration.
        env (check::create (alice, bob, USD(50)),
            expiration (env.now()), ter (tecEXPIRED));
        env.close();

        using namespace std::chrono_literals;
        env (check::create (alice, bob, USD(50)), expiration (env.now() + 1s));
        env.close();

        // Insufficient reserve.
        Account const cheri {"cheri"};
        env.fund (
            env.current()->fees().accountReserve(1) - drops(1), cheri);

        env (check::create (cheri, bob, USD(50)),
            fee (drops (env.current()->fees().base)),
            ter (tecINSUFFICIENT_RESERVE));
        env.close();

        env (pay (bob, cheri, drops (env.current()->fees().base + 1)));
        env.close();

        env (check::create (cheri, bob, USD(50)));
        env.close();
    }

    void testCashXRP()
    {
        // Explore many of the valid ways to cash a check for XRP.
        testcase ("Cash XRP");

        using namespace test::jtx;

        Account const alice {"alice"};
        Account const bob {"bob"};

        Env env {*this};
        auto const closeTime =
            fix1449Time() + 100 * env.closed()->info().closeTimeResolution;
        env.close (closeTime);

        XRPAmount const baseFeeDrops {env.current()->fees().base};
        STAmount const startBalance {XRP(300).value()};
        env.fund (startBalance, alice, bob);
        {
            // Basic XRP check.
            uint256 const chkId {getCheckIndex (alice, env.seq (alice))};
            env (check::create (alice, bob, XRP(10)));
            env.close();
            env.require (balance (alice, startBalance - drops (baseFeeDrops)));
            env.require (balance (bob,   startBalance));
            BEAST_EXPECT (checksOnAccount (env, alice).size() == 1);
            BEAST_EXPECT (checksOnAccount (env, bob  ).size() == 1);
            BEAST_EXPECT (ownerCount (env, alice) == 1);
            BEAST_EXPECT (ownerCount (env, bob  ) == 0);

            env (check::cash (bob, chkId, XRP(10)));
            env.close();
            env.require (balance (alice,
                startBalance - XRP(10) - drops (baseFeeDrops)));
            env.require (balance (bob,
                startBalance + XRP(10) - drops (baseFeeDrops)));
            BEAST_EXPECT (checksOnAccount (env, alice).size() == 0);
            BEAST_EXPECT (checksOnAccount (env, bob  ).size() == 0);
            BEAST_EXPECT (ownerCount (env, alice) == 0);
            BEAST_EXPECT (ownerCount (env, bob  ) == 0);

            // Make alice's and bob's balances easy to think about.
            env (pay (env.master, alice, XRP(10) + drops (baseFeeDrops)));
            env (pay (bob, env.master,   XRP(10) - drops (baseFeeDrops * 2)));
            env.close();
            env.require (balance (alice, startBalance));
            env.require (balance (bob,   startBalance));
        }
        {
            // Write a check that chews into alice's reserve.
            STAmount const reserve {env.current()->fees().accountReserve (0)};
            STAmount const checkAmount {
                startBalance - reserve - drops (baseFeeDrops)};
            uint256 const chkId {getCheckIndex (alice, env.seq (alice))};
            env (check::create (alice, bob, checkAmount));
            env.close();

            // bob tries to cash for more than the check amount.
            env (check::cash (bob, chkId, checkAmount + drops(1)),
                ter (tecPATH_PARTIAL));
            env.close();
            env (check::cash (
                bob, chkId, check::DeliverMin (checkAmount + drops(1))),
                ter (tecPATH_PARTIAL));
            env.close();

            // bob cashes exactly the check amount.  This is successful
            // because one unit of alice's reserve is released when the
            // check is consumed.
            env (check::cash (bob, chkId, check::DeliverMin (checkAmount)));
            verifyDeliveredAmount (env, drops(checkAmount.mantissa()));
            env.require (balance (alice, reserve));
            env.require (balance (bob,
                startBalance + checkAmount - drops (baseFeeDrops * 3)));
            BEAST_EXPECT (checksOnAccount (env, alice).size() == 0);
            BEAST_EXPECT (checksOnAccount (env, bob  ).size() == 0);
            BEAST_EXPECT (ownerCount (env, alice) == 0);
            BEAST_EXPECT (ownerCount (env, bob  ) == 0);

            // Make alice's and bob's balances easy to think about.
            env (pay (env.master, alice, checkAmount + drops (baseFeeDrops)));
            env (pay (bob, env.master, checkAmount - drops (baseFeeDrops * 4)));
            env.close();
            env.require (balance (alice, startBalance));
            env.require (balance (bob,   startBalance));
        }
        {
            // Write a check that goes one drop past what alice can pay.
            STAmount const reserve {env.current()->fees().accountReserve (0)};
            STAmount const checkAmount {
                startBalance - reserve - drops (baseFeeDrops - 1)};
            uint256 const chkId {getCheckIndex (alice, env.seq (alice))};
            env (check::create (alice, bob, checkAmount));
            env.close();

            // bob tries to cash for exactly the check amount.  Fails because
            // alice is one drop shy of funding the check.
            env (check::cash (bob, chkId, checkAmount), ter (tecPATH_PARTIAL));
            env.close();

            // bob decides to get what he can from the bounced check.
            env (check::cash (bob, chkId, check::DeliverMin (drops(1))));
            verifyDeliveredAmount (env, drops(checkAmount.mantissa() - 1));
            env.require (balance (alice, reserve));
            env.require (balance (bob,
                startBalance + checkAmount - drops (baseFeeDrops * 2 + 1)));
            BEAST_EXPECT (checksOnAccount (env, alice).size() == 0);
            BEAST_EXPECT (checksOnAccount (env, bob  ).size() == 0);
            BEAST_EXPECT (ownerCount (env, alice) == 0);
            BEAST_EXPECT (ownerCount (env, bob  ) == 0);

            // Make alice's and bob's balances easy to think about.
            env (pay (env.master, alice,
                checkAmount + drops (baseFeeDrops - 1)));
            env (pay (bob, env.master,
                checkAmount - drops (baseFeeDrops * 3 + 1)));
            env.close();
            env.require (balance (alice, startBalance));
            env.require (balance (bob,   startBalance));
        }
    }

    void testCashIOU ()
    {
        // Explore many of the valid ways to cash a check for an IOU.
        testcase ("Cash IOU");

        using namespace test::jtx;

        Account const gw {"gateway"};
        Account const alice {"alice"};
        Account const bob {"bob"};
        IOU const USD {gw["USD"]};
        {
            // Simple IOU check cashed with Amount (with failures).
            Env env {*this};
            auto const closeTime =
                fix1449Time() + 100 * env.closed()->info().closeTimeResolution;
            env.close (closeTime);

            env.fund (XRP(1000), gw, alice, bob);

            // alice writes the check before she gets the funds.
            uint256 const chkId1 {getCheckIndex (alice, env.seq (alice))};
            env (check::create (alice, bob, USD(10)));
            env.close();

            // bob attempts to cash the check.  Should fail.
            env (check::cash (bob, chkId1, USD(10)), ter (tecPATH_PARTIAL));
            env.close();

            // alice gets almost enough funds.  bob tries and fails again.
            env (trust (alice, USD(20)));
            env.close();
            env (pay (gw, alice, USD(9.5)));
            env.close();
            env (check::cash (bob, chkId1, USD(10)), ter (tecPATH_PARTIAL));
            env.close();

            // alice gets the last of the necessary funds.  bob tries again
            // and fails because he hasn't got a trust line for USD.
            env (pay (gw, alice, USD(0.5)));
            env.close();
            env (check::cash (bob, chkId1, USD(10)), ter (tecNO_LINE));
            env.close();

            // bob sets up the trust line, but not at a high enough limit.
            env (trust (bob, USD(9.5)));
            env.close();
            env (check::cash (bob, chkId1, USD(10)), ter (tecPATH_PARTIAL));
            env.close();

            // bob sets the trust line limit high enough but asks for more
            // than the check's SendMax.
            env (trust (bob, USD(10.5)));
            env.close();
            env (check::cash (bob, chkId1, USD(10.5)), ter (tecPATH_PARTIAL));
            env.close();

            // bob asks for exactly the check amount and the check clears.
            env (check::cash (bob, chkId1, USD(10)));
            env.close();
            env.require (balance (alice, USD( 0)));
            env.require (balance (bob,   USD(10)));
            BEAST_EXPECT (checksOnAccount (env, alice).size() == 0);
            BEAST_EXPECT (checksOnAccount (env, bob  ).size() == 0);
            BEAST_EXPECT (ownerCount (env, alice) == 1);
            BEAST_EXPECT (ownerCount (env, bob  ) == 1);

            // bob tries to cash the same check again, which fails.
            env (check::cash (bob, chkId1, USD(10)), ter (tecNO_ENTRY));
            env.close();

            // bob pays alice USD(7) so he can try another case.
            env (pay (bob, alice, USD(7)));
            env.close();

            uint256 const chkId2 {getCheckIndex (alice, env.seq (alice))};
            env (check::create (alice, bob, USD(7)));
            env.close();
            BEAST_EXPECT (checksOnAccount (env, alice).size() == 1);
            BEAST_EXPECT (checksOnAccount (env, bob  ).size() == 1);

            // bob cashes the check for less than the face amount.  That works,
            // consumes the check, and bob receives as much as he asked for.
            env (check::cash (bob, chkId2, USD(5)));
            env.close();
            env.require (balance (alice, USD(2)));
            env.require (balance (bob,   USD(8)));
            BEAST_EXPECT (checksOnAccount (env, alice).size() == 0);
            BEAST_EXPECT (checksOnAccount (env, bob  ).size() == 0);
            BEAST_EXPECT (ownerCount (env, alice) == 1);
            BEAST_EXPECT (ownerCount (env, bob  ) == 1);

            // alice writes two checks for USD(2), although she only has USD(2).
            uint256 const chkId3 {getCheckIndex (alice, env.seq (alice))};
            env (check::create (alice, bob, USD(2)));
            env.close();
            uint256 const chkId4 {getCheckIndex (alice, env.seq (alice))};
            env (check::create (alice, bob, USD(2)));
            env.close();
            BEAST_EXPECT (checksOnAccount (env, alice).size() == 2);
            BEAST_EXPECT (checksOnAccount (env, bob  ).size() == 2);

            // bob cashes the second check for the face amount.
            env (check::cash (bob, chkId4, USD(2)));
            env.close();
            env.require (balance (alice, USD( 0)));
            env.require (balance (bob,   USD(10)));
            BEAST_EXPECT (checksOnAccount (env, alice).size() == 1);
            BEAST_EXPECT (checksOnAccount (env, bob  ).size() == 1);
            BEAST_EXPECT (ownerCount (env, alice) == 2);
            BEAST_EXPECT (ownerCount (env, bob  ) == 1);

            // bob is not allowed to cash the last check for USD(0), he must
            // use check::cancel instead.
            env (check::cash (bob, chkId3, USD(0)), ter (temBAD_AMOUNT));
            env.close();
            env.require (balance (alice, USD( 0)));
            env.require (balance (bob,   USD(10)));
            BEAST_EXPECT (checksOnAccount (env, alice).size() == 1);
            BEAST_EXPECT (checksOnAccount (env, bob  ).size() == 1);
            BEAST_EXPECT (ownerCount (env, alice) == 2);
            BEAST_EXPECT (ownerCount (env, bob  ) == 1);

            // ... so bob cancels alice's remaining check.
            env (check::cancel (bob, chkId3));
            env.close();
            env.require (balance (alice, USD( 0)));
            env.require (balance (bob,   USD(10)));
            BEAST_EXPECT (checksOnAccount (env, alice).size() == 0);
            BEAST_EXPECT (checksOnAccount (env, bob  ).size() == 0);
            BEAST_EXPECT (ownerCount (env, alice) == 1);
            BEAST_EXPECT (ownerCount (env, bob  ) == 1);
        }
        {
            // Simple IOU check cashed with DeliverMin (with failures).
            Env env {*this};
            auto const closeTime =
                fix1449Time() + 100 * env.closed()->info().closeTimeResolution;
            env.close (closeTime);

            env.fund (XRP(1000), gw, alice, bob);

            env (trust (alice, USD(20)));
            env (trust (bob, USD(20)));
            env.close();
            env (pay (gw, alice, USD(8)));
            env.close();

            // alice creates several checks ahead of time.
            uint256 const chkId9 {getCheckIndex (alice, env.seq (alice))};
            env (check::create (alice, bob, USD(9)));
            env.close();
            uint256 const chkId8 {getCheckIndex (alice, env.seq (alice))};
            env (check::create (alice, bob, USD(8)));
            env.close();
            uint256 const chkId7 {getCheckIndex (alice, env.seq (alice))};
            env (check::create (alice, bob, USD(7)));
            env.close();
            uint256 const chkId6 {getCheckIndex (alice, env.seq (alice))};
            env (check::create (alice, bob, USD(6)));
            env.close();

            // bob attempts to cash a check for the amount on the check.
            // Should fail, since alice doesn't have the funds.
            env (check::cash (bob, chkId9, check::DeliverMin (USD(9))),
                ter (tecPATH_PARTIAL));
            env.close();

            // bob sets a DeliverMin of 7 and gets all that alice has.
            env (check::cash (bob, chkId9, check::DeliverMin (USD(7))));
            verifyDeliveredAmount (env, USD(8));
            env.require (balance (alice, USD(0)));
            env.require (balance (bob,   USD(8)));
            BEAST_EXPECT (checksOnAccount (env, alice).size() == 3);
            BEAST_EXPECT (checksOnAccount (env, bob  ).size() == 3);
            BEAST_EXPECT (ownerCount (env, alice) == 4);
            BEAST_EXPECT (ownerCount (env, bob  ) == 1);

            // bob pays alice USD(7) so he can use another check.
            env (pay (bob, alice, USD(7)));
            env.close();

            // Using DeliverMin for the SendMax value of the check (and no
            // transfer fees) should work just like setting Amount.
            env (check::cash (bob, chkId7, check::DeliverMin (USD(7))));
            verifyDeliveredAmount (env, USD(7));
            env.require (balance (alice, USD(0)));
            env.require (balance (bob,   USD(8)));
            BEAST_EXPECT (checksOnAccount (env, alice).size() == 2);
            BEAST_EXPECT (checksOnAccount (env, bob  ).size() == 2);
            BEAST_EXPECT (ownerCount (env, alice) == 3);
            BEAST_EXPECT (ownerCount (env, bob  ) == 1);

            // bob pays alice USD(8) so he can use the last two checks.
            env (pay (bob, alice, USD(8)));
            env.close();

            // alice has USD(8). If bob uses the check for USD(6) and uses a
            // DeliverMin of 4, he should get the SendMax value of the check.
            env (check::cash (bob, chkId6, check::DeliverMin (USD(4))));
            verifyDeliveredAmount (env, USD(6));
            env.require (balance (alice, USD(2)));
            env.require (balance (bob,   USD(6)));
            BEAST_EXPECT (checksOnAccount (env, alice).size() == 1);
            BEAST_EXPECT (checksOnAccount (env, bob  ).size() == 1);
            BEAST_EXPECT (ownerCount (env, alice) == 2);
            BEAST_EXPECT (ownerCount (env, bob  ) == 1);

            // bob cashes the last remaining check setting a DeliverMin.
            // of exactly alice's remaining USD.
            env (check::cash (bob, chkId8, check::DeliverMin (USD(2))));
            verifyDeliveredAmount (env, USD(2));
            env.require (balance (alice, USD(0)));
            env.require (balance (bob,   USD(8)));
            BEAST_EXPECT (checksOnAccount (env, alice).size() == 0);
            BEAST_EXPECT (checksOnAccount (env, bob  ).size() == 0);
            BEAST_EXPECT (ownerCount (env, alice) == 1);
            BEAST_EXPECT (ownerCount (env, bob  ) == 1);
        }
        {
            // Examine the effects of the asfRequireAuth flag.
            Env env {*this};
            auto const closeTime =
                fix1449Time() + 100 * env.closed()->info().closeTimeResolution;
            env.close (closeTime);

            env.fund (XRP(1000), gw, alice, bob);
            env (fset (gw, asfRequireAuth));
            env.close();
            env (trust (gw, alice["USD"](100)), txflags (tfSetfAuth));
            env (trust (alice, USD(20)));
            env.close();
            env (pay (gw, alice, USD(8)));
            env.close();

            // alice writes a check to bob for USD.  bob can't cash it
            // because he is not authorized to hold gw["USD"].
            uint256 const chkId {getCheckIndex (alice, env.seq (alice))};
            env (check::create (alice, bob, USD(7)));
            env.close();

            env (check::cash (bob, chkId, USD(7)), ter (tecNO_LINE));
            env.close();

            // Now give bob a trustline for USD.  bob still can't cash the
            // check because he is not authorized.
            env (trust (bob, USD(5)));
            env.close();

            env (check::cash (bob, chkId, USD(7)), ter (tecNO_AUTH));
            env.close();

            // bob gets authorization to hold gw["USD"].
            env (trust (gw, bob["USD"](1)), txflags (tfSetfAuth));
            env.close();

            // bob tries to cash the check again but fails because his trust
            // limit is too low.
            env (check::cash (bob, chkId, USD(7)), ter (tecPATH_PARTIAL));
            env.close();

            // Since bob set his limit low, he cashes the check with a
            // DeliverMin and hits his trust limit.
            env (check::cash (bob, chkId, check::DeliverMin (USD(4))));
            verifyDeliveredAmount (env, USD(5));
            env.require (balance (alice, USD(3)));
            env.require (balance (bob,   USD(5)));
            BEAST_EXPECT (checksOnAccount (env, alice).size() == 0);
            BEAST_EXPECT (checksOnAccount (env, bob  ).size() == 0);
            BEAST_EXPECT (ownerCount (env, alice) == 1);
            BEAST_EXPECT (ownerCount (env, bob  ) == 1);
        }

        // Use a regular key and also multisign to cash a check.
        // featureMultiSign changes the reserve on a SignerList, so
        // check both before and after.
        FeatureBitset const allSupported {supported_amendments()};
        for (auto const features :
            {allSupported - featureMultiSignReserve,
                allSupported | featureMultiSignReserve})
        {
            Env env {*this, features};
            auto const closeTime =
                fix1449Time() + 100 * env.closed()->info().closeTimeResolution;
            env.close (closeTime);

            env.fund (XRP(1000), gw, alice, bob);

            // alice creates her checks ahead of time.
            uint256 const chkId1 {getCheckIndex (alice, env.seq (alice))};
            env (check::create (alice, bob, USD(1)));
            env.close();

            uint256 const chkId2 {getCheckIndex (alice, env.seq (alice))};
            env (check::create (alice, bob, USD(2)));
            env.close();

            env (trust (alice, USD(20)));
            env (trust (bob, USD(20)));
            env.close();
            env (pay (gw, alice, USD(8)));
            env.close();

            // Give bob a regular key and signers
            Account const bobby {"bobby", KeyType::secp256k1};
            env (regkey (bob, bobby));
            env.close();

            Account const bogie {"bogie", KeyType::secp256k1};
            Account const demon {"demon", KeyType::ed25519};
            env (signers (bob, 2, {{bogie, 1}, {demon, 1}}), sig (bobby));
            env.close();

            // If featureMultiSignReserve is enabled then bob's signer list
            // has an owner count of 1, otherwise it's 4.
            int const signersCount {features[featureMultiSignReserve] ? 1 : 4};
            BEAST_EXPECT (ownerCount (env, bob) == signersCount + 1);

            // bob uses his regular key to cash a check.
            env (check::cash (bob, chkId1, (USD(1))), sig (bobby));
            env.close();
            env.require (balance (alice, USD(7)));
            env.require (balance (bob,   USD(1)));
            BEAST_EXPECT (checksOnAccount (env, alice).size() == 1);
            BEAST_EXPECT (checksOnAccount (env, bob  ).size() == 1);
            BEAST_EXPECT (ownerCount (env, alice) == 2);
            BEAST_EXPECT (ownerCount (env, bob  ) == signersCount + 1);

            // bob uses multisigning to cash a check.
            XRPAmount const baseFeeDrops {env.current()->fees().base};
            env (check::cash (bob, chkId2, (USD(2))),
                msig (bogie, demon), fee (3 * baseFeeDrops));
            env.close();
            env.require (balance (alice, USD(5)));
            env.require (balance (bob,   USD(3)));
            BEAST_EXPECT (checksOnAccount (env, alice).size() == 0);
            BEAST_EXPECT (checksOnAccount (env, bob  ).size() == 0);
            BEAST_EXPECT (ownerCount (env, alice) == 1);
            BEAST_EXPECT (ownerCount (env, bob  ) == signersCount + 1);
        }
    }

    void testCashXferFee()
    {
        // Look at behavior when the issuer charges a transfer fee.
        testcase ("Cash with transfer fee");

        using namespace test::jtx;

        Account const gw {"gateway"};
        Account const alice {"alice"};
        Account const bob {"bob"};
        IOU const USD {gw["USD"]};

        Env env {*this};
        auto const closeTime =
            fix1449Time() + 100 * env.closed()->info().closeTimeResolution;
        env.close (closeTime);

        env.fund (XRP(1000), gw, alice, bob);

        env (trust (alice, USD(1000)));
        env (trust (bob, USD(1000)));
        env.close();
        env (pay (gw, alice, USD(1000)));
        env.close();

        // Set gw's transfer rate and see the consequences when cashing a check.
        env (rate (gw, 1.25));
        env.close();

        // alice writes a check with a SendMax of USD(125).  The most bob
        // can get is USD(100) because of the transfer rate.
        uint256 const chkId125 {getCheckIndex (alice, env.seq (alice))};
        env (check::create (alice, bob, USD(125)));
        env.close();

        // alice writes another check that won't get cashed until the transfer
        // rate changes so we can see the rate applies when the check is
        // cashed, not when it is created.
        uint256 const chkId120 {getCheckIndex (alice, env.seq (alice))};
        env (check::create (alice, bob, USD(120)));
        env.close();

        // bob attempts to cash the check for face value.  Should fail.
        env (check::cash (bob, chkId125, USD(125)), ter (tecPATH_PARTIAL));
        env.close();
        env (check::cash (bob, chkId125, check::DeliverMin (USD(101))),
            ter (tecPATH_PARTIAL));
        env.close();

        // bob decides that he'll accept anything USD(75) or up.
        // He gets USD(100).
        env (check::cash (bob, chkId125, check::DeliverMin (USD(75))));
        verifyDeliveredAmount (env, USD(100));
        env.require (balance (alice, USD(1000 - 125)));
        env.require (balance (bob,   USD(   0 + 100)));
        BEAST_EXPECT (checksOnAccount (env, alice).size() == 1);
        BEAST_EXPECT (checksOnAccount (env, bob  ).size() == 1);

        // Adjust gw's rate...
        env (rate (gw, 1.2));
        env.close();

        // bob cashes the second check for less than the face value.  The new
        // rate applies to the actual value transferred.
        env (check::cash (bob, chkId120, USD(50)));
        env.close();
        env.require (balance (alice, USD(1000 - 125 - 60)));
        env.require (balance (bob,   USD(   0 + 100 + 50)));
        BEAST_EXPECT (checksOnAccount (env, alice).size() == 0);
        BEAST_EXPECT (checksOnAccount (env, bob  ).size() == 0);
    }

    void testCashQuality ()
    {
        // Look at the eight possible cases for Quality In/Out.
        testcase ("Cash quality");

        using namespace test::jtx;

        Account const gw {"gateway"};
        Account const alice {"alice"};
        Account const bob {"bob"};
        IOU const USD {gw["USD"]};

        Env env {*this};
        auto const closeTime =
            fix1449Time() + 100 * env.closed()->info().closeTimeResolution;
        env.close (closeTime);

        env.fund (XRP(1000), gw, alice, bob);

        env (trust (alice, USD(1000)));
        env (trust (bob, USD(1000)));
        env.close();
        env (pay (gw, alice, USD(1000)));
        env.close();

        //
        // Quality effects on transfers between two non-issuers.
        //

        // Provide lambdas that return a qualityInPercent and qualityOutPercent.
        auto qIn =
            [] (double percent) { return qualityInPercent  (percent); };
        auto qOut =
            [] (double percent) { return qualityOutPercent (percent); };

        // There are two test lambdas: one for a Payment and one for a Check.
        // This shows whether a Payment and a Check behave the same.
        auto testNonIssuerQPay = [&env, &alice, &bob, &USD]
        (Account const& truster,
            IOU const& iou, auto const& inOrOut, double pct, double amount)
        {
            // Capture bob's and alice's balances so we can test at the end.
            STAmount const aliceStart {env.balance (alice, USD.issue()).value()};
            STAmount const bobStart   {env.balance (bob,   USD.issue()).value()};

            // Set the modified quality.
            env (trust (truster, iou(1000)), inOrOut (pct));
            env.close();

            env (pay (alice, bob, USD(amount)), sendmax (USD(10)));
            env.close();
            env.require (balance (alice, aliceStart - USD(10)));
            env.require (balance (bob,   bobStart   + USD(10)));

            // Return the quality to the unmodified state so it doesn't
            // interfere with upcoming tests.
            env (trust (truster, iou(1000)), inOrOut (0));
            env.close();
        };

        auto testNonIssuerQCheck = [&env, &alice, &bob, &USD]
        (Account const& truster,
            IOU const& iou, auto const& inOrOut, double pct, double amount)
        {
            // Capture bob's and alice's balances so we can test at the end.
            STAmount const aliceStart {env.balance (alice, USD.issue()).value()};
            STAmount const bobStart   {env.balance (bob,   USD.issue()).value()};

            // Set the modified quality.
            env (trust (truster, iou(1000)), inOrOut (pct));
            env.close();

            uint256 const chkId {getCheckIndex (alice, env.seq (alice))};
            env (check::create (alice, bob, USD(10)));
            env.close();

            env (check::cash (bob, chkId, USD(amount)));
            env.close();
            env.require (balance (alice, aliceStart - USD(10)));
            env.require (balance (bob,   bobStart   + USD(10)));

            // Return the quality to the unmodified state so it doesn't
            // interfere with upcoming tests.
            env (trust (truster, iou(1000)), inOrOut (0));
            env.close();
        };

        //                                           pct  amount
        testNonIssuerQPay   (alice, gw["USD"],  qIn,  50,     10);
        testNonIssuerQCheck (alice, gw["USD"],  qIn,  50,     10);

        // This is the only case where the Quality affects the outcome.
        testNonIssuerQPay   (bob,   gw["USD"],  qIn,  50,      5);
        testNonIssuerQCheck (bob,   gw["USD"],  qIn,  50,      5);

        testNonIssuerQPay   (gw, alice["USD"],  qIn,  50,     10);
        testNonIssuerQCheck (gw, alice["USD"],  qIn,  50,     10);

        testNonIssuerQPay   (gw,   bob["USD"],  qIn,  50,     10);
        testNonIssuerQCheck (gw,   bob["USD"],  qIn,  50,     10);

        testNonIssuerQPay   (alice, gw["USD"], qOut, 200,     10);
        testNonIssuerQCheck (alice, gw["USD"], qOut, 200,     10);

        testNonIssuerQPay   (bob,   gw["USD"], qOut, 200,     10);
        testNonIssuerQCheck (bob,   gw["USD"], qOut, 200,     10);

        testNonIssuerQPay   (gw, alice["USD"], qOut, 200,     10);
        testNonIssuerQCheck (gw, alice["USD"], qOut, 200,     10);

        testNonIssuerQPay   (gw,   bob["USD"], qOut, 200,     10);
        testNonIssuerQCheck (gw,   bob["USD"], qOut, 200,     10);

        //
        // Quality effects on transfers between an issuer and a non-issuer.
        //

        // There are two test lambdas for the same reason as before.
        auto testIssuerQPay = [&env, &gw, &alice, &USD]
        (Account const& truster, IOU const& iou,
            auto const& inOrOut, double pct,
            double amt1, double max1, double amt2, double max2)
        {
            // Capture alice's balance so we can test at the end.  It doesn't
            // make any sense to look at the balance of a gateway.
            STAmount const aliceStart {env.balance (alice, USD.issue()).value()};

            // Set the modified quality.
            env (trust (truster, iou(1000)), inOrOut (pct));
            env.close();

            // alice pays gw.
            env (pay (alice, gw, USD(amt1)), sendmax (USD(max1)));
            env.close();
            env.require (balance (alice, aliceStart - USD(10)));

            // gw pays alice.
            env (pay (gw, alice, USD(amt2)), sendmax (USD(max2)));
            env.close();
            env.require (balance (alice, aliceStart));

            // Return the quality to the unmodified state so it doesn't
            // interfere with upcoming tests.
            env (trust (truster, iou(1000)), inOrOut (0));
            env.close();
        };

        auto testIssuerQCheck = [&env, &gw, &alice, &USD]
        (Account const& truster, IOU const& iou,
            auto const& inOrOut, double pct,
            double amt1, double max1, double amt2, double max2)
        {
            // Capture alice's balance so we can test at the end.  It doesn't
            // make any sense to look at the balance of the issuer.
            STAmount const aliceStart {env.balance (alice, USD.issue()).value()};

            // Set the modified quality.
            env (trust (truster, iou(1000)), inOrOut (pct));
            env.close();

            // alice writes check to gw.  gw cashes.
            uint256 const chkAliceId {getCheckIndex (alice, env.seq (alice))};
            env (check::create (alice, gw, USD(max1)));
            env.close();

            env (check::cash (gw, chkAliceId, USD(amt1)));
            env.close();
            env.require (balance (alice, aliceStart - USD(10)));

            // gw writes check to alice.  alice cashes.
            uint256 const chkGwId {getCheckIndex (gw, env.seq (gw))};
            env (check::create (gw, alice, USD(max2)));
            env.close();

            env (check::cash (alice, chkGwId, USD(amt2)));
            env.close();
            env.require (balance (alice, aliceStart));

            // Return the quality to the unmodified state so it doesn't
            // interfere with upcoming tests.
            env (trust (truster, iou(1000)), inOrOut (0));
            env.close();
        };

        // The first case is the only one where the quality affects the outcome.
        //                                        pct  amt1 max1 amt2 max2
        testIssuerQPay   (alice, gw["USD"],  qIn,  50,   10,  10,   5,  10);
        testIssuerQCheck (alice, gw["USD"],  qIn,  50,   10,  10,   5,  10);

        testIssuerQPay   (gw, alice["USD"],  qIn,  50,   10,  10,  10,  10);
        testIssuerQCheck (gw, alice["USD"],  qIn,  50,   10,  10,  10,  10);

        testIssuerQPay   (alice, gw["USD"], qOut, 200,   10,  10,  10,  10);
        testIssuerQCheck (alice, gw["USD"], qOut, 200,   10,  10,  10,  10);

        testIssuerQPay   (gw, alice["USD"], qOut, 200,   10,  10,  10,  10);
        testIssuerQCheck (gw, alice["USD"], qOut, 200,   10,  10,  10,  10);
    }

    void testCashInvalid()
    {
        // Explore many of the ways to fail at cashing a check.
        testcase ("Cash invalid");

        using namespace test::jtx;

        Account const gw {"gateway"};
        Account const alice {"alice"};
        Account const bob {"bob"};
        Account const zoe {"zoe"};
        IOU const USD {gw["USD"]};

        Env env {*this};
        auto const closeTime =
            fix1449Time() + 100 * env.closed()->info().closeTimeResolution;
        env.close (closeTime);

        env.fund (XRP(1000), gw, alice, bob, zoe);

        // Now set up alice's trustline.
        env (trust (alice, USD(20)));
        env.close();
        env (pay (gw, alice, USD(20)));
        env.close();

        // Before bob gets a trustline, have him try to cash a check.
        // Should fail.
        {
            uint256 const chkId {getCheckIndex (alice, env.seq (alice))};
            env (check::create (alice, bob, USD(20)));
            env.close();

            env (check::cash (bob, chkId, USD(20)), ter (tecNO_LINE));
            env.close();
        }

        // Now set up bob's trustline.
        env (trust (bob, USD(20)));
        env.close();

        // bob tries to cash a non-existent check from alice.
        {
            uint256 const chkId {getCheckIndex (alice, env.seq (alice))};
            env (check::cash (bob, chkId, USD(20)), ter (tecNO_ENTRY));
            env.close();
        }

        // alice creates her checks ahead of time.
        uint256 const chkIdU {getCheckIndex (alice, env.seq (alice))};
        env (check::create (alice, bob, USD(20)));
        env.close();

        uint256 const chkIdX {getCheckIndex (alice, env.seq (alice))};
        env (check::create (alice, bob, XRP(10)));
        env.close();

        using namespace std::chrono_literals;
        uint256 const chkIdExp {getCheckIndex (alice, env.seq (alice))};
        env (check::create (alice, bob, XRP(10)), expiration (env.now() + 1s));
        env.close();

        uint256 const chkIdFroz1 {getCheckIndex (alice, env.seq (alice))};
        env (check::create (alice, bob, USD(1)));
        env.close();

        uint256 const chkIdFroz2 {getCheckIndex (alice, env.seq (alice))};
        env (check::create (alice, bob, USD(2)));
        env.close();

        uint256 const chkIdFroz3 {getCheckIndex (alice, env.seq (alice))};
        env (check::create (alice, bob, USD(3)));
        env.close();

        uint256 const chkIdFroz4 {getCheckIndex (alice, env.seq (alice))};
        env (check::create (alice, bob, USD(4)));
        env.close();

        uint256 const chkIdNoDest1 {getCheckIndex (alice, env.seq (alice))};
        env (check::create (alice, bob, USD(1)));
        env.close();

        uint256 const chkIdHasDest2 {getCheckIndex (alice, env.seq (alice))};
        env (check::create (alice, bob, USD(2)), dest_tag (7));
        env.close();

        // Same set of failing cases for both IOU and XRP check cashing.
        auto failingCases = [&env, &gw, &alice, &bob] (
            uint256 const& chkId, STAmount const& amount)
        {
            // Bad fee.
            env (check::cash (bob, chkId, amount), fee (drops(-10)),
                ter (temBAD_FEE));
            env.close();

            // Bad flags.
            env (check::cash (bob, chkId, amount),
                txflags (tfImmediateOrCancel), ter (temINVALID_FLAG));
            env.close();

            // Missing both Amount and DeliverMin.
            {
                Json::Value tx {check::cash (bob, chkId, amount)};
                tx.removeMember (sfAmount.jsonName);
                env (tx, ter (temMALFORMED));
                env.close();
            }
            // Both Amount and DeliverMin present.
            {
                Json::Value tx {check::cash (bob, chkId, amount)};
                tx[sfDeliverMin.jsonName]  = amount.getJson(JsonOptions::none);
                env (tx, ter (temMALFORMED));
                env.close();
            }

            // Negative or zero amount.
            {
                STAmount neg {amount};
                neg.negate();
                env (check::cash (bob, chkId, neg), ter (temBAD_AMOUNT));
                env.close();
                env (check::cash (bob, chkId, amount.zeroed()),
                    ter (temBAD_AMOUNT));
                env.close();
            }

            // Bad currency.
            if (! amount.native()) {
                Issue const badIssue {badCurrency(), amount.getIssuer()};
                STAmount badAmount {amount};
                badAmount.setIssue (Issue {badCurrency(), amount.getIssuer()});
                env (check::cash (bob, chkId, badAmount),
                    ter (temBAD_CURRENCY));
                env.close();
            }

            // Not destination cashing check.
            env (check::cash (alice, chkId, amount), ter (tecNO_PERMISSION));
            env.close();
            env (check::cash (gw, chkId, amount), ter (tecNO_PERMISSION));
            env.close();

            // Currency mismatch.
            {
                IOU const wrongCurrency {gw["EUR"]};
                STAmount badAmount {amount};
                badAmount.setIssue (wrongCurrency.issue());
                env (check::cash (bob, chkId, badAmount),
                    ter (temMALFORMED));
                env.close();
            }

            // Issuer mismatch.
            {
                IOU const wrongIssuer {alice["USD"]};
                STAmount badAmount {amount};
                badAmount.setIssue (wrongIssuer.issue());
                env (check::cash (bob, chkId, badAmount),
                    ter (temMALFORMED));
                env.close();
            }

            // Amount bigger than SendMax.
            env (check::cash (bob, chkId, amount + amount),
                ter (tecPATH_PARTIAL));
            env.close();

            // DeliverMin bigger than SendMax.
            env (check::cash (bob, chkId, check::DeliverMin (amount + amount)),
                ter (tecPATH_PARTIAL));
            env.close();
        };

        failingCases (chkIdX, XRP(10));
        failingCases (chkIdU, USD(20));

        // Verify that those two checks really were cashable.
        env (check::cash (bob, chkIdU, USD(20)));
        env.close();
        env (check::cash (bob, chkIdX, check::DeliverMin (XRP(10))));
        verifyDeliveredAmount (env, XRP(10));

        // Try to cash an expired check.
        env (check::cash (bob, chkIdExp, XRP(10)), ter (tecEXPIRED));
        env.close();

        // Cancel the expired check.  Anyone can cancel an expired check.
        env (check::cancel (zoe, chkIdExp));
        env.close();

        // Can we cash a check with frozen currency?
        {
            env (pay (bob, alice, USD(20)));
            env.close();
            env.require (balance (alice, USD(20)));
            env.require (balance (bob,   USD( 0)));

            // Global freeze
            env (fset (gw, asfGlobalFreeze));
            env.close();

            env (check::cash (bob, chkIdFroz1, USD(1)), ter (tecPATH_PARTIAL));
            env.close();
            env (check::cash (bob, chkIdFroz1, check::DeliverMin (USD(0.5))),
                ter (tecPATH_PARTIAL));
            env.close();

            env (fclear (gw, asfGlobalFreeze));
            env.close();

            // No longer frozen.  Success.
            env (check::cash (bob, chkIdFroz1, USD(1)));
            env.close();
            env.require (balance (alice, USD(19)));
            env.require (balance (bob,   USD( 1)));

            // Freeze individual trustlines.
            env (trust(gw, alice["USD"](0), tfSetFreeze));
            env.close();
            env (check::cash (bob, chkIdFroz2, USD(2)), ter (tecPATH_PARTIAL));
            env.close();
            env (check::cash (bob, chkIdFroz2, check::DeliverMin (USD(1))),
                ter (tecPATH_PARTIAL));
            env.close();

            // Clear that freeze.  Now check cashing works.
            env (trust(gw, alice["USD"](0), tfClearFreeze));
            env.close();
            env (check::cash (bob, chkIdFroz2, USD(2)));
            env.close();
            env.require (balance (alice, USD(17)));
            env.require (balance (bob,   USD( 3)));

            // Freeze bob's trustline.  bob can't cash the check.
            env (trust(gw, bob["USD"](0), tfSetFreeze));
            env.close();
            env (check::cash (bob, chkIdFroz3, USD(3)), ter (tecFROZEN));
            env.close();
            env (check::cash (bob, chkIdFroz3, check::DeliverMin (USD(1))),
                ter (tecFROZEN));
            env.close();

            // Clear that freeze.  Now check cashing works again.
            env (trust(gw, bob["USD"](0), tfClearFreeze));
            env.close();
            env (check::cash (bob, chkIdFroz3, check::DeliverMin (USD(1))));
            verifyDeliveredAmount (env, USD(3));
            env.require (balance (alice, USD(14)));
            env.require (balance (bob,   USD( 6)));

            // Set bob's freeze bit in the other direction.  Check
            // cashing fails.
            env (trust (bob, USD(20), tfSetFreeze));
            env.close();
            env (check::cash (bob, chkIdFroz4, USD(4)), ter (terNO_LINE));
            env.close();
            env (check::cash (bob, chkIdFroz4, check::DeliverMin (USD(1))),
                ter (terNO_LINE));
            env.close();

            // Clear bob's freeze bit and the check should be cashable.
            env (trust (bob, USD(20), tfClearFreeze));
            env.close();
            env (check::cash (bob, chkIdFroz4, USD(4)));
            env.close();
            env.require (balance (alice, USD(10)));
            env.require (balance (bob,   USD(10)));
        }
        {
            // Set the RequireDest flag on bob's account (after the check
            // was created) then cash a check without a destination tag.
            env (fset (bob, asfRequireDest));
            env.close();
            env (check::cash (bob, chkIdNoDest1, USD(1)),
                ter (tecDST_TAG_NEEDED));
            env.close();
            env (check::cash (bob, chkIdNoDest1, check::DeliverMin (USD(0.5))),
                ter (tecDST_TAG_NEEDED));
            env.close();

            // bob can cash a check with a destination tag.
            env (check::cash (bob, chkIdHasDest2, USD(2)));
            env.close();
            env.require (balance (alice, USD( 8)));
            env.require (balance (bob,   USD(12)));

            // Clear the RequireDest flag on bob's account so he can
            // cash the check with no DestinationTag.
            env (fclear (bob, asfRequireDest));
            env.close();
            env (check::cash (bob, chkIdNoDest1, USD(1)));
            env.close();
            env.require (balance (alice, USD( 7)));
            env.require (balance (bob,   USD(13)));
        }
    }

    void testCancelValid()
    {
        // Explore many of the ways to cancel a check.
        testcase ("Cancel valid");

        using namespace test::jtx;

        Account const gw {"gateway"};
        Account const alice {"alice"};
        Account const bob {"bob"};
        Account const zoe {"zoe"};
        IOU const USD {gw["USD"]};

        // featureMultiSign changes the reserve on a SignerList, so
        // check both before and after.
        FeatureBitset const allSupported {supported_amendments()};
        for (auto const features :
            {allSupported - featureMultiSignReserve,
                allSupported | featureMultiSignReserve})
        {
            Env env {*this, features};

            auto const closeTime =
                fix1449Time() + 100 * env.closed()->info().closeTimeResolution;
            env.close (closeTime);

            env.fund (XRP(1000), gw, alice, bob, zoe);

            // alice creates her checks ahead of time.
            // Three ordinary checks with no expiration.
            uint256 const chkId1 {getCheckIndex (alice, env.seq (alice))};
            env (check::create (alice, bob, USD(10)));
            env.close();

            uint256 const chkId2 {getCheckIndex (alice, env.seq (alice))};
            env (check::create (alice, bob, XRP(10)));
            env.close();

            uint256 const chkId3 {getCheckIndex (alice, env.seq (alice))};
            env (check::create (alice, bob, USD(10)));
            env.close();

            // Three checks that expire in 10 minutes.
            using namespace std::chrono_literals;
            uint256 const chkIdNotExp1 {getCheckIndex (alice, env.seq (alice))};
            env (check::create (alice, bob, XRP(10)), expiration (env.now()+600s));
            env.close();

            uint256 const chkIdNotExp2 {getCheckIndex (alice, env.seq (alice))};
            env (check::create (alice, bob, USD(10)), expiration (env.now()+600s));
            env.close();

            uint256 const chkIdNotExp3 {getCheckIndex (alice, env.seq (alice))};
            env (check::create (alice, bob, XRP(10)), expiration (env.now()+600s));
            env.close();

            // Three checks that expire in one second.
            uint256 const chkIdExp1 {getCheckIndex (alice, env.seq (alice))};
            env (check::create (alice, bob, USD(10)), expiration (env.now()+1s));
            env.close();

            uint256 const chkIdExp2 {getCheckIndex (alice, env.seq (alice))};
            env (check::create (alice, bob, XRP(10)), expiration (env.now()+1s));
            env.close();

            uint256 const chkIdExp3 {getCheckIndex (alice, env.seq (alice))};
            env (check::create (alice, bob, USD(10)), expiration (env.now()+1s));
            env.close();

            // Two checks to cancel using a regular key and using multisigning.
            uint256 const chkIdReg {getCheckIndex (alice, env.seq (alice))};
            env (check::create (alice, bob, USD(10)));
            env.close();

            uint256 const chkIdMSig {getCheckIndex (alice, env.seq (alice))};
            env (check::create (alice, bob, XRP(10)));
            env.close();
            BEAST_EXPECT (checksOnAccount (env, alice).size() == 11);
            BEAST_EXPECT (ownerCount (env, alice) == 11);

            // Creator, destination, and an outsider cancel the checks.
            env (check::cancel (alice, chkId1));
            env.close();
            BEAST_EXPECT (checksOnAccount (env, alice).size() == 10);
            BEAST_EXPECT (ownerCount (env, alice) == 10);

            env (check::cancel (bob, chkId2));
            env.close();
            BEAST_EXPECT (checksOnAccount (env, alice).size() == 9);
            BEAST_EXPECT (ownerCount (env, alice) == 9);

            env (check::cancel (zoe, chkId3), ter (tecNO_PERMISSION));
            env.close();
            BEAST_EXPECT (checksOnAccount (env, alice).size() == 9);
            BEAST_EXPECT (ownerCount (env, alice) == 9);

            // Creator, destination, and an outsider cancel unexpired checks.
            env (check::cancel (alice, chkIdNotExp1));
            env.close();
            BEAST_EXPECT (checksOnAccount (env, alice).size() == 8);
            BEAST_EXPECT (ownerCount (env, alice) == 8);

            env (check::cancel (bob, chkIdNotExp2));
            env.close();
            BEAST_EXPECT (checksOnAccount (env, alice).size() == 7);
            BEAST_EXPECT (ownerCount (env, alice) == 7);

            env (check::cancel (zoe, chkIdNotExp3), ter (tecNO_PERMISSION));
            env.close();
            BEAST_EXPECT (checksOnAccount (env, alice).size() == 7);
            BEAST_EXPECT (ownerCount (env, alice) == 7);

            // Creator, destination, and an outsider cancel expired checks.
            env (check::cancel (alice, chkIdExp1));
            env.close();
            BEAST_EXPECT (checksOnAccount (env, alice).size() == 6);
            BEAST_EXPECT (ownerCount (env, alice) == 6);

            env (check::cancel (bob, chkIdExp2));
            env.close();
            BEAST_EXPECT (checksOnAccount (env, alice).size() == 5);
            BEAST_EXPECT (ownerCount (env, alice) == 5);

            env (check::cancel (zoe, chkIdExp3));
            env.close();
            BEAST_EXPECT (checksOnAccount (env, alice).size() == 4);
            BEAST_EXPECT (ownerCount (env, alice) == 4);

            // Use a regular key and also multisign to cancel checks.
            Account const alie {"alie", KeyType::ed25519};
            env (regkey (alice, alie));
            env.close();

            Account const bogie {"bogie", KeyType::secp256k1};
            Account const demon {"demon", KeyType::ed25519};
            env (signers (alice, 2, {{bogie, 1}, {demon, 1}}), sig (alie));
            env.close();

            // If featureMultiSignReserve is enabled then alices's signer list
            // has an owner count of 1, otherwise it's 4.
            int const signersCount {features[featureMultiSignReserve] ? 1 : 4};

            // alice uses her regular key to cancel a check.
            env (check::cancel (alice, chkIdReg), sig (alie));
            env.close();
            BEAST_EXPECT (checksOnAccount (env, alice).size() == 3);
            BEAST_EXPECT (ownerCount (env, alice) == signersCount + 3);

            // alice uses multisigning to cancel a check.
            XRPAmount const baseFeeDrops {env.current()->fees().base};
            env (check::cancel (alice, chkIdMSig),
                msig (bogie, demon), fee (3 * baseFeeDrops));
            env.close();
            BEAST_EXPECT (checksOnAccount (env, alice).size() == 2);
            BEAST_EXPECT (ownerCount (env, alice) == signersCount + 2);

            // Creator and destination cancel the remaining unexpired checks.
            env (check::cancel (alice, chkId3), sig (alice));
            env.close();
            BEAST_EXPECT (checksOnAccount (env, alice).size() == 1);
            BEAST_EXPECT (ownerCount (env, alice) == signersCount + 1);

            env (check::cancel (bob, chkIdNotExp3));
            env.close();
            BEAST_EXPECT (checksOnAccount (env, alice).size() == 0);
            BEAST_EXPECT (ownerCount (env, alice) == signersCount + 0);
        }
    }

    void testCancelInvalid()
    {
        // Explore many of the ways to fail at canceling a check.
        testcase ("Cancel invalid");

        using namespace test::jtx;

        Account const alice {"alice"};
        Account const bob {"bob"};

        Env env {*this};
        auto const closeTime =
            fix1449Time() + 100 * env.closed()->info().closeTimeResolution;
        env.close (closeTime);

        env.fund (XRP(1000), alice, bob);

        // Bad fee.
        env (check::cancel (bob, getCheckIndex (alice, env.seq (alice))),
            fee (drops(-10)), ter (temBAD_FEE));
        env.close();

        // Bad flags.
        env (check::cancel (bob, getCheckIndex (alice, env.seq (alice))),
            txflags (tfImmediateOrCancel), ter (temINVALID_FLAG));
        env.close();

        // Non-existent check.
        env (check::cancel (bob, getCheckIndex (alice, env.seq (alice))),
            ter (tecNO_ENTRY));
        env.close();
    }

    void testFix1623Enable ()
    {
        testcase ("Fix1623 enable");

        using namespace test::jtx;

        auto testEnable = [this] (FeatureBitset const& features, bool hasFields)
        {
            // Unless fix1623 is enabled a "tx" RPC command should return
            // neither "DeliveredAmount" nor "delivered_amount" on a CheckCash
            // transaction.
            Account const alice {"alice"};
            Account const bob {"bob"};

            Env env {*this, features};
            auto const closeTime =
                fix1449Time() + 100 * env.closed()->info().closeTimeResolution;
            env.close (closeTime);

            env.fund (XRP(1000), alice, bob);
            env.close();

            uint256 const chkId {getCheckIndex (alice, env.seq (alice))};
            env (check::create (alice, bob, XRP(200)));
            env.close();

            env (check::cash (bob, chkId, check::DeliverMin (XRP(100))));

            // Get the hash for the most recent transaction.
            std::string const txHash {
                env.tx()->getJson (JsonOptions::none)[jss::hash].asString()};

            // DeliveredAmount and delivered_amount are either present or
            // not present in the metadata returned by "tx" based on fix1623.
            env.close();
            Json::Value const meta =
                env.rpc ("tx", txHash)[jss::result][jss::meta];

            BEAST_EXPECT(meta.isMember (
                sfDeliveredAmount.jsonName) == hasFields);
            BEAST_EXPECT(meta.isMember (jss::delivered_amount) == hasFields);
        };

        // Run both the disabled and enabled cases.
        testEnable (supported_amendments() - fix1623, false);
        testEnable (supported_amendments(),           true);
    }

public:
    void run () override
    {
        testEnabled();
        testCreateValid();
        testCreateInvalid();
        testCashXRP();
        testCashIOU();
        testCashXferFee();
        testCashQuality();
        testCashInvalid();
        testCancelValid();
        testCancelInvalid();
        testFix1623Enable();
    }
};

BEAST_DEFINE_TESTSUITE (Check, tx, ripple);

}  // ripple


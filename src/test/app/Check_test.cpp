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

#include <ripple/protocol/Feature.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>

namespace ripple {
namespace test {
namespace jtx {

/** Set Expiration on a JTx. */
class expiration
{
private:
    std::uint32_t const expry_;

public:
    explicit expiration(NetClock::time_point const& expiry)
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
    explicit source_tag(std::uint32_t tag) : tag_{tag}
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
    explicit dest_tag(std::uint32_t tag) : tag_{tag}
    {
    }

    void
    operator()(Env&, JTx& jt) const
    {
        jt[sfDestinationTag.jsonName] = tag_;
    }
};

}  // namespace jtx
}  // namespace test

class Check_test : public beast::unit_test::suite
{
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

    // Helper function that returns the owner count on an account.
    static std::uint32_t
    ownerCount(test::jtx::Env const& env, test::jtx::Account const& account)
    {
        std::uint32_t ret{0};
        if (auto const sleAccount = env.le(account))
            ret = sleAccount->getFieldU32(sfOwnerCount);
        return ret;
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
    testEnabled(FeatureBitset features)
    {
        testcase("Enabled");

        using namespace test::jtx;
        Account const alice{"alice"};
        {
            // If the Checks amendment is not enabled, you should not be able
            // to create, cash, or cancel checks.
            Env env{*this, features - featureChecks};

            env.fund(XRP(1000), alice);

            uint256 const checkId{
                getCheckIndex(env.master, env.seq(env.master))};
            env(check::create(env.master, alice, XRP(100)), ter(temDISABLED));
            env.close();

            env(check::cash(alice, checkId, XRP(100)), ter(temDISABLED));
            env.close();

            env(check::cancel(alice, checkId), ter(temDISABLED));
            env.close();
        }
        {
            // If the Checks amendment is enabled all check-related
            // facilities should be available.
            Env env{*this, features};

            env.fund(XRP(1000), alice);

            uint256 const checkId1{
                getCheckIndex(env.master, env.seq(env.master))};
            env(check::create(env.master, alice, XRP(100)));
            env.close();

            env(check::cash(alice, checkId1, XRP(100)));
            env.close();

            uint256 const checkId2{
                getCheckIndex(env.master, env.seq(env.master))};
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
        IOU const USD{gw["USD"]};

        Env env{*this, features};

        STAmount const startBalance{XRP(1000).value()};
        env.fund(startBalance, gw, alice, bob);

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
    testCreateInvalid(FeatureBitset features)
    {
        // Explore many of the invalid ways to create a check.
        testcase("Create invalid");

        using namespace test::jtx;

        Account const gw1{"gateway1"};
        Account const gwF{"gatewayFrozen"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        IOU const USD{gw1["USD"]};

        Env env{*this, features};

        STAmount const startBalance{XRP(1000).value()};
        env.fund(startBalance, gw1, gwF, alice, bob);

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
            IOU const BAD{gw1, badCurrency()};
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
            IOU const USF{gwF["USF"]};
            env(fset(gwF, asfGlobalFreeze));
            env.close();

            env(check::create(alice, bob, USF(50)), ter(tecFROZEN));
            env.close();

            env(fclear(gwF, asfGlobalFreeze));
            env.close();

            env(check::create(alice, bob, USF(50)));
            env.close();
        }
        {
            // Frozen trust line.  Check creation should be similar to payment
            // behavior in the face of frozen trust lines.
            env.trust(USD(1000), alice);
            env.trust(USD(1000), bob);
            env.close();
            env(pay(gw1, alice, USD(25)));
            env(pay(gw1, bob, USD(25)));
            env.close();

            // Setting trustline freeze in one direction prevents alice from
            // creating a check for USD.  But bob and gw1 should still be able
            // to create a check for USD to alice.
            env(trust(gw1, alice["USD"](0), tfSetFreeze));
            env.close();
            env(check::create(alice, bob, USD(50)), ter(tecFROZEN));
            env.close();
            env(pay(alice, bob, USD(1)), ter(tecPATH_DRY));
            env.close();
            env(check::create(bob, alice, USD(50)));
            env.close();
            env(pay(bob, alice, USD(1)));
            env.close();
            env(check::create(gw1, alice, USD(50)));
            env.close();
            env(pay(gw1, alice, USD(1)));
            env.close();

            // Clear that freeze.  Now check creation works.
            env(trust(gw1, alice["USD"](0), tfClearFreeze));
            env.close();
            env(check::create(alice, bob, USD(50)));
            env.close();
            env(check::create(bob, alice, USD(50)));
            env.close();
            env(check::create(gw1, alice, USD(50)));
            env.close();

            // Freezing in the other direction does not effect alice's USD
            // check creation, but prevents bob and gw1 from writing a check
            // for USD to alice.
            env(trust(alice, USD(0), tfSetFreeze));
            env.close();
            env(check::create(alice, bob, USD(50)));
            env.close();
            env(pay(alice, bob, USD(1)));
            env.close();
            env(check::create(bob, alice, USD(50)), ter(tecFROZEN));
            env.close();
            env(pay(bob, alice, USD(1)), ter(tecPATH_DRY));
            env.close();
            env(check::create(gw1, alice, USD(50)), ter(tecFROZEN));
            env.close();
            env(pay(gw1, alice, USD(1)), ter(tecPATH_DRY));
            env.close();

            // Clear that freeze.
            env(trust(alice, USD(0), tfClearFreeze));
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
        {
            // Basic XRP check.
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, XRP(10)));
            env.close();
            env.require(balance(alice, startBalance - drops(baseFeeDrops)));
            env.require(balance(bob, startBalance));
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 1);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 1);
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 0);

            env(check::cash(bob, chkId, XRP(10)));
            env.close();
            env.require(
                balance(alice, startBalance - XRP(10) - drops(baseFeeDrops)));
            env.require(
                balance(bob, startBalance + XRP(10) - drops(baseFeeDrops)));
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 0);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 0);
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, bob) == 0);

            // Make alice's and bob's balances easy to think about.
            env(pay(env.master, alice, XRP(10) + drops(baseFeeDrops)));
            env(pay(bob, env.master, XRP(10) - drops(baseFeeDrops * 2)));
            env.close();
            env.require(balance(alice, startBalance));
            env.require(balance(bob, startBalance));
        }
        {
            // Write a check that chews into alice's reserve.
            STAmount const reserve{env.current()->fees().accountReserve(0)};
            STAmount const checkAmount{
                startBalance - reserve - drops(baseFeeDrops)};
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, checkAmount));
            env.close();

            // bob tries to cash for more than the check amount.
            env(check::cash(bob, chkId, checkAmount + drops(1)),
                ter(tecPATH_PARTIAL));
            env.close();
            env(check::cash(
                    bob, chkId, check::DeliverMin(checkAmount + drops(1))),
                ter(tecPATH_PARTIAL));
            env.close();

            // bob cashes exactly the check amount.  This is successful
            // because one unit of alice's reserve is released when the
            // check is consumed.
            env(check::cash(bob, chkId, check::DeliverMin(checkAmount)));
            verifyDeliveredAmount(env, drops(checkAmount.mantissa()));
            env.require(balance(alice, reserve));
            env.require(balance(
                bob, startBalance + checkAmount - drops(baseFeeDrops * 3)));
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 0);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 0);
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, bob) == 0);

            // Make alice's and bob's balances easy to think about.
            env(pay(env.master, alice, checkAmount + drops(baseFeeDrops)));
            env(pay(bob, env.master, checkAmount - drops(baseFeeDrops * 4)));
            env.close();
            env.require(balance(alice, startBalance));
            env.require(balance(bob, startBalance));
        }
        {
            // Write a check that goes one drop past what alice can pay.
            STAmount const reserve{env.current()->fees().accountReserve(0)};
            STAmount const checkAmount{
                startBalance - reserve - drops(baseFeeDrops - 1)};
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, checkAmount));
            env.close();

            // bob tries to cash for exactly the check amount.  Fails because
            // alice is one drop shy of funding the check.
            env(check::cash(bob, chkId, checkAmount), ter(tecPATH_PARTIAL));
            env.close();

            // bob decides to get what he can from the bounced check.
            env(check::cash(bob, chkId, check::DeliverMin(drops(1))));
            verifyDeliveredAmount(env, drops(checkAmount.mantissa() - 1));
            env.require(balance(alice, reserve));
            env.require(balance(
                bob, startBalance + checkAmount - drops(baseFeeDrops * 2 + 1)));
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 0);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 0);
            BEAST_EXPECT(ownerCount(env, alice) == 0);
            BEAST_EXPECT(ownerCount(env, bob) == 0);

            // Make alice's and bob's balances easy to think about.
            env(pay(env.master, alice, checkAmount + drops(baseFeeDrops - 1)));
            env(pay(
                bob, env.master, checkAmount - drops(baseFeeDrops * 3 + 1)));
            env.close();
            env.require(balance(alice, startBalance));
            env.require(balance(bob, startBalance));
        }
    }

    void
    testCashIOU(FeatureBitset features)
    {
        // Explore many of the valid ways to cash a check for an IOU.
        testcase("Cash IOU");

        using namespace test::jtx;

        bool const cashCheckMakesTrustLine =
            features[featureCheckCashMakesTrustLine];

        Account const gw{"gateway"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        IOU const USD{gw["USD"]};
        {
            // Simple IOU check cashed with Amount (with failures).
            Env env{*this, features};

            env.fund(XRP(1000), gw, alice, bob);

            // alice writes the check before she gets the funds.
            uint256 const chkId1{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, USD(10)));
            env.close();

            // bob attempts to cash the check.  Should fail.
            env(check::cash(bob, chkId1, USD(10)), ter(tecPATH_PARTIAL));
            env.close();

            // alice gets almost enough funds.  bob tries and fails again.
            env(trust(alice, USD(20)));
            env.close();
            env(pay(gw, alice, USD(9.5)));
            env.close();
            env(check::cash(bob, chkId1, USD(10)), ter(tecPATH_PARTIAL));
            env.close();

            // alice gets the last of the necessary funds.  bob tries again
            // and fails because he hasn't got a trust line for USD.
            env(pay(gw, alice, USD(0.5)));
            env.close();
            if (!cashCheckMakesTrustLine)
            {
                // If cashing a check automatically creates a trustline then
                // this returns tesSUCCESS and the check is removed from the
                // ledger which would mess up later tests.
                env(check::cash(bob, chkId1, USD(10)), ter(tecNO_LINE));
                env.close();
            }

            // bob sets up the trust line, but not at a high enough limit.
            env(trust(bob, USD(9.5)));
            env.close();
            if (!cashCheckMakesTrustLine)
            {
                // If cashing a check is allowed to exceed the trust line
                // limit then this returns tesSUCCESS and the check is
                // removed from the ledger which would mess up later tests.
                env(check::cash(bob, chkId1, USD(10)), ter(tecPATH_PARTIAL));
                env.close();
            }

            // bob sets the trust line limit high enough but asks for more
            // than the check's SendMax.
            env(trust(bob, USD(10.5)));
            env.close();
            env(check::cash(bob, chkId1, USD(10.5)), ter(tecPATH_PARTIAL));
            env.close();

            // bob asks for exactly the check amount and the check clears.
            env(check::cash(bob, chkId1, USD(10)));
            env.close();
            env.require(balance(alice, USD(0)));
            env.require(balance(bob, USD(10)));
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 0);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 0);
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 1);

            // bob tries to cash the same check again, which fails.
            env(check::cash(bob, chkId1, USD(10)), ter(tecNO_ENTRY));
            env.close();

            // bob pays alice USD(7) so he can try another case.
            env(pay(bob, alice, USD(7)));
            env.close();

            uint256 const chkId2{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, USD(7)));
            env.close();
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 1);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 1);

            // bob cashes the check for less than the face amount.  That works,
            // consumes the check, and bob receives as much as he asked for.
            env(check::cash(bob, chkId2, USD(5)));
            env.close();
            env.require(balance(alice, USD(2)));
            env.require(balance(bob, USD(8)));
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 0);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 0);
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 1);

            // alice writes two checks for USD(2), although she only has USD(2).
            uint256 const chkId3{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, USD(2)));
            env.close();
            uint256 const chkId4{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, USD(2)));
            env.close();
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 2);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 2);

            // bob cashes the second check for the face amount.
            env(check::cash(bob, chkId4, USD(2)));
            env.close();
            env.require(balance(alice, USD(0)));
            env.require(balance(bob, USD(10)));
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 1);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 1);
            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(ownerCount(env, bob) == 1);

            // bob is not allowed to cash the last check for USD(0), he must
            // use check::cancel instead.
            env(check::cash(bob, chkId3, USD(0)), ter(temBAD_AMOUNT));
            env.close();
            env.require(balance(alice, USD(0)));
            env.require(balance(bob, USD(10)));
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 1);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 1);
            BEAST_EXPECT(ownerCount(env, alice) == 2);
            BEAST_EXPECT(ownerCount(env, bob) == 1);

            if (cashCheckMakesTrustLine)
            {
                // Automatic trust lines are enabled.  But one aspect of
                // automatic trust lines is that they allow the account
                // cashing a check to exceed their trust line limit.  Show
                // that at work.
                //
                // bob's trust line limit is currently USD(10.5).  Show that
                // a payment to bob cannot exceed that trust line, but cashing
                // a check can.

                // Payment of 20 USD fails.
                env(pay(gw, bob, USD(20)), ter(tecPATH_PARTIAL));
                env.close();

                uint256 const chkId20{getCheckIndex(gw, env.seq(gw))};
                env(check::create(gw, bob, USD(20)));
                env.close();

                // However cashing a check for 20 USD succeeds.
                env(check::cash(bob, chkId20, USD(20)));
                env.close();
                env.require(balance(bob, USD(30)));

                // Clean up this most recent experiment so the rest of the
                // tests work.
                env(pay(bob, gw, USD(20)));
            }

            // ... so bob cancels alice's remaining check.
            env(check::cancel(bob, chkId3));
            env.close();
            env.require(balance(alice, USD(0)));
            env.require(balance(bob, USD(10)));
            BEAST_EXPECT(checksOnAccount(env, alice).size() == 0);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 0);
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 1);
        }
        {
            // Simple IOU check cashed with DeliverMin (with failures).
            Env env{*this, features};

            env.fund(XRP(1000), gw, alice, bob);

            env(trust(alice, USD(20)));
            env(trust(bob, USD(20)));
            env.close();
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
            env(fset(gw, asfRequireAuth));
            env.close();
            env(trust(gw, alice["USD"](100)), txflags(tfSetfAuth));
            env(trust(alice, USD(20)));
            env.close();
            env(pay(gw, alice, USD(8)));
            env.close();

            // alice writes a check to bob for USD.  bob can't cash it
            // because he is not authorized to hold gw["USD"].
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, USD(7)));
            env.close();

            env(check::cash(bob, chkId, USD(7)),
                ter(cashCheckMakesTrustLine ? tecNO_AUTH : tecNO_LINE));
            env.close();

            // Now give bob a trustline for USD.  bob still can't cash the
            // check because he is not authorized.
            env(trust(bob, USD(5)));
            env.close();

            env(check::cash(bob, chkId, USD(7)), ter(tecNO_AUTH));
            env.close();

            // bob gets authorization to hold gw["USD"].
            env(trust(gw, bob["USD"](1)), txflags(tfSetfAuth));
            env.close();

            // bob tries to cash the check again but fails because his trust
            // limit is too low.
            if (!cashCheckMakesTrustLine)
            {
                // If cashing a check is allowed to exceed the trust line
                // limit then this returns tesSUCCESS and the check is
                // removed from the ledger which would mess up later tests.
                env(check::cash(bob, chkId, USD(7)), ter(tecPATH_PARTIAL));
                env.close();
            }

            // Two possible outcomes here depending on whether cashing a
            // check can build a trust line:
            //   o If it can't build a trust line, then since bob set his
            //     limit low, he cashes the check with a DeliverMin and hits
            //     his trust limit.
            //  o If it can build a trust line, then the check is allowed to
            //    exceed the trust limit and bob gets the full transfer.
            env(check::cash(bob, chkId, check::DeliverMin(USD(4))));
            STAmount const bobGot = cashCheckMakesTrustLine ? USD(7) : USD(5);
            verifyDeliveredAmount(env, bobGot);
            env.require(balance(alice, USD(8) - bobGot));
            env.require(balance(bob, bobGot));

            BEAST_EXPECT(checksOnAccount(env, alice).size() == 0);
            BEAST_EXPECT(checksOnAccount(env, bob).size() == 0);
            BEAST_EXPECT(ownerCount(env, alice) == 1);
            BEAST_EXPECT(ownerCount(env, bob) == 1);
        }

        // Use a regular key and also multisign to cash a check.
        // featureMultiSignReserve changes the reserve on a SignerList, so
        // check both before and after.
        for (auto const& testFeatures :
             {features - featureMultiSignReserve,
              features | featureMultiSignReserve})
        {
            Env env{*this, testFeatures};

            env.fund(XRP(1000), gw, alice, bob);

            // alice creates her checks ahead of time.
            uint256 const chkId1{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, USD(1)));
            env.close();

            uint256 const chkId2{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, USD(2)));
            env.close();

            env(trust(alice, USD(20)));
            env(trust(bob, USD(20)));
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

            // If featureMultiSignReserve is enabled then bob's signer list
            // has an owner count of 1, otherwise it's 4.
            int const signersCount = {
                testFeatures[featureMultiSignReserve] ? 1 : 4};
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
        IOU const USD{gw["USD"]};

        Env env{*this, features};

        env.fund(XRP(1000), gw, alice, bob);

        env(trust(alice, USD(1000)));
        env(trust(bob, USD(1000)));
        env.close();
        env(pay(gw, alice, USD(1000)));
        env.close();

        // Set gw's transfer rate and see the consequences when cashing a check.
        env(rate(gw, 1.25));
        env.close();

        // alice writes a check with a SendMax of USD(125).  The most bob
        // can get is USD(100) because of the transfer rate.
        uint256 const chkId125{getCheckIndex(alice, env.seq(alice))};
        env(check::create(alice, bob, USD(125)));
        env.close();

        // alice writes another check that won't get cashed until the transfer
        // rate changes so we can see the rate applies when the check is
        // cashed, not when it is created.
        uint256 const chkId120{getCheckIndex(alice, env.seq(alice))};
        env(check::create(alice, bob, USD(120)));
        env.close();

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
        env.require(balance(alice, USD(1000 - 125)));
        env.require(balance(bob, USD(0 + 100)));
        BEAST_EXPECT(checksOnAccount(env, alice).size() == 1);
        BEAST_EXPECT(checksOnAccount(env, bob).size() == 1);

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
        IOU const USD{gw["USD"]};

        Env env{*this, features};

        env.fund(XRP(1000), gw, alice, bob);

        env(trust(alice, USD(1000)));
        env(trust(bob, USD(1000)));
        env.close();
        env(pay(gw, alice, USD(1000)));
        env.close();

        //
        // Quality effects on transfers between two non-issuers.
        //

        // Provide lambdas that return a qualityInPercent and qualityOutPercent.
        auto qIn = [](double percent) { return qualityInPercent(percent); };
        auto qOut = [](double percent) { return qualityOutPercent(percent); };

        // There are two test lambdas: one for a Payment and one for a Check.
        // This shows whether a Payment and a Check behave the same.
        auto testNonIssuerQPay = [&env, &alice, &bob, &USD](
                                     Account const& truster,
                                     IOU const& iou,
                                     auto const& inOrOut,
                                     double pct,
                                     double amount) {
            // Capture bob's and alice's balances so we can test at the end.
            STAmount const aliceStart{env.balance(alice, USD.issue()).value()};
            STAmount const bobStart{env.balance(bob, USD.issue()).value()};

            // Set the modified quality.
            env(trust(truster, iou(1000)), inOrOut(pct));
            env.close();

            env(pay(alice, bob, USD(amount)), sendmax(USD(10)));
            env.close();
            env.require(balance(alice, aliceStart - USD(10)));
            env.require(balance(bob, bobStart + USD(10)));

            // Return the quality to the unmodified state so it doesn't
            // interfere with upcoming tests.
            env(trust(truster, iou(1000)), inOrOut(0));
            env.close();
        };

        auto testNonIssuerQCheck = [&env, &alice, &bob, &USD](
                                       Account const& truster,
                                       IOU const& iou,
                                       auto const& inOrOut,
                                       double pct,
                                       double amount) {
            // Capture bob's and alice's balances so we can test at the end.
            STAmount const aliceStart{env.balance(alice, USD.issue()).value()};
            STAmount const bobStart{env.balance(bob, USD.issue()).value()};

            // Set the modified quality.
            env(trust(truster, iou(1000)), inOrOut(pct));
            env.close();

            uint256 const chkId = getCheckIndex(alice, env.seq(alice));
            env(check::create(alice, bob, USD(10)));
            env.close();

            env(check::cash(bob, chkId, USD(amount)));
            env.close();
            env.require(balance(alice, aliceStart - USD(10)));
            env.require(balance(bob, bobStart + USD(10)));

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
        auto testIssuerQPay = [&env, &gw, &alice, &USD](
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
            STAmount const aliceStart{env.balance(alice, USD.issue()).value()};

            // Set the modified quality.
            env(trust(truster, iou(1000)), inOrOut(pct));
            env.close();

            // alice pays gw.
            env(pay(alice, gw, USD(amt1)), sendmax(USD(max1)));
            env.close();
            env.require(balance(alice, aliceStart - USD(10)));

            // gw pays alice.
            env(pay(gw, alice, USD(amt2)), sendmax(USD(max2)));
            env.close();
            env.require(balance(alice, aliceStart));

            // Return the quality to the unmodified state so it doesn't
            // interfere with upcoming tests.
            env(trust(truster, iou(1000)), inOrOut(0));
            env.close();
        };

        auto testIssuerQCheck = [&env, &gw, &alice, &USD](
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
            STAmount const aliceStart{env.balance(alice, USD.issue()).value()};

            // Set the modified quality.
            env(trust(truster, iou(1000)), inOrOut(pct));
            env.close();

            // alice writes check to gw.  gw cashes.
            uint256 const chkAliceId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, gw, USD(max1)));
            env.close();

            env(check::cash(gw, chkAliceId, USD(amt1)));
            env.close();
            env.require(balance(alice, aliceStart - USD(10)));

            // gw writes check to alice.  alice cashes.
            uint256 const chkGwId{getCheckIndex(gw, env.seq(gw))};
            env(check::create(gw, alice, USD(max2)));
            env.close();

            env(check::cash(alice, chkGwId, USD(amt2)));
            env.close();
            env.require(balance(alice, aliceStart));

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
        IOU const USD{gw["USD"]};

        Env env(*this, features);

        env.fund(XRP(1000), gw, alice, bob, zoe);

        // Now set up alice's trustline.
        env(trust(alice, USD(20)));
        env.close();
        env(pay(gw, alice, USD(20)));
        env.close();

        // Before bob gets a trustline, have him try to cash a check.
        // Should fail.
        {
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, USD(20)));
            env.close();

            if (!features[featureCheckCashMakesTrustLine])
            {
                // If cashing a check automatically creates a trustline then
                // this returns tesSUCCESS and the check is removed from the
                // ledger which would mess up later tests.
                env(check::cash(bob, chkId, USD(20)), ter(tecNO_LINE));
                env.close();
            }
        }

        // Now set up bob's trustline.
        env(trust(bob, USD(20)));
        env.close();

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

        uint256 const chkIdFroz4{getCheckIndex(alice, env.seq(alice))};
        env(check::create(alice, bob, USD(4)));
        env.close();

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
                IOU const wrongCurrency{gw["EUR"]};
                STAmount badAmount{amount};
                badAmount.setIssue(wrongCurrency.issue());
                env(check::cash(bob, chkId, badAmount), ter(temMALFORMED));
                env.close();
            }

            // Issuer mismatch.
            {
                IOU const wrongIssuer{alice["USD"]};
                STAmount badAmount{amount};
                badAmount.setIssue(wrongIssuer.issue());
                env(check::cash(bob, chkId, badAmount), ter(temMALFORMED));
                env.close();
            }

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
            env(fset(gw, asfGlobalFreeze));
            env.close();

            env(check::cash(bob, chkIdFroz1, USD(1)), ter(tecPATH_PARTIAL));
            env.close();
            env(check::cash(bob, chkIdFroz1, check::DeliverMin(USD(0.5))),
                ter(tecPATH_PARTIAL));
            env.close();

            env(fclear(gw, asfGlobalFreeze));
            env.close();

            // No longer frozen.  Success.
            env(check::cash(bob, chkIdFroz1, USD(1)));
            env.close();
            env.require(balance(alice, USD(19)));
            env.require(balance(bob, USD(1)));

            // Freeze individual trustlines.
            env(trust(gw, alice["USD"](0), tfSetFreeze));
            env.close();
            env(check::cash(bob, chkIdFroz2, USD(2)), ter(tecPATH_PARTIAL));
            env.close();
            env(check::cash(bob, chkIdFroz2, check::DeliverMin(USD(1))),
                ter(tecPATH_PARTIAL));
            env.close();

            // Clear that freeze.  Now check cashing works.
            env(trust(gw, alice["USD"](0), tfClearFreeze));
            env.close();
            env(check::cash(bob, chkIdFroz2, USD(2)));
            env.close();
            env.require(balance(alice, USD(17)));
            env.require(balance(bob, USD(3)));

            // Freeze bob's trustline.  bob can't cash the check.
            env(trust(gw, bob["USD"](0), tfSetFreeze));
            env.close();
            env(check::cash(bob, chkIdFroz3, USD(3)), ter(tecFROZEN));
            env.close();
            env(check::cash(bob, chkIdFroz3, check::DeliverMin(USD(1))),
                ter(tecFROZEN));
            env.close();

            // Clear that freeze.  Now check cashing works again.
            env(trust(gw, bob["USD"](0), tfClearFreeze));
            env.close();
            env(check::cash(bob, chkIdFroz3, check::DeliverMin(USD(1))));
            verifyDeliveredAmount(env, USD(3));
            env.require(balance(alice, USD(14)));
            env.require(balance(bob, USD(6)));

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
        }
        {
            // Set the RequireDest flag on bob's account (after the check
            // was created) then cash a check without a destination tag.
            env(fset(bob, asfRequireDest));
            env.close();
            env(check::cash(bob, chkIdNoDest1, USD(1)), ter(tecDST_TAG_NEEDED));
            env.close();
            env(check::cash(bob, chkIdNoDest1, check::DeliverMin(USD(0.5))),
                ter(tecDST_TAG_NEEDED));
            env.close();

            // bob can cash a check with a destination tag.
            env(check::cash(bob, chkIdHasDest2, USD(2)));
            env.close();
            env.require(balance(alice, USD(8)));
            env.require(balance(bob, USD(12)));

            // Clear the RequireDest flag on bob's account so he can
            // cash the check with no DestinationTag.
            env(fclear(bob, asfRequireDest));
            env.close();
            env(check::cash(bob, chkIdNoDest1, USD(1)));
            env.close();
            env.require(balance(alice, USD(7)));
            env.require(balance(bob, USD(13)));
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
        IOU const USD{gw["USD"]};

        // featureMultiSignReserve changes the reserve on a SignerList, so
        // check both before and after.
        for (auto const& testFeatures :
             {features - featureMultiSignReserve,
              features | featureMultiSignReserve})
        {
            Env env{*this, testFeatures};

            env.fund(XRP(1000), gw, alice, bob, zoe);

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

            // If featureMultiSignReserve is enabled then alices's signer list
            // has an owner count of 1, otherwise it's 4.
            int const signersCount{
                testFeatures[featureMultiSignReserve] ? 1 : 4};

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
    testCancelInvalid(FeatureBitset features)
    {
        // Explore many of the ways to fail at canceling a check.
        testcase("Cancel invalid");

        using namespace test::jtx;

        Account const alice{"alice"};
        Account const bob{"bob"};

        Env env{*this, features};

        env.fund(XRP(1000), alice, bob);

        // Bad fee.
        env(check::cancel(bob, getCheckIndex(alice, env.seq(alice))),
            fee(drops(-10)),
            ter(temBAD_FEE));
        env.close();

        // Bad flags.
        env(check::cancel(bob, getCheckIndex(alice, env.seq(alice))),
            txflags(tfImmediateOrCancel),
            ter(temINVALID_FLAG));
        env.close();

        // Non-existent check.
        env(check::cancel(bob, getCheckIndex(alice, env.seq(alice))),
            ter(tecNO_ENTRY));
        env.close();
    }

    void
    testFix1623Enable(FeatureBitset features)
    {
        testcase("Fix1623 enable");

        using namespace test::jtx;

        auto testEnable = [this](
                              FeatureBitset const& features, bool hasFields) {
            // Unless fix1623 is enabled a "tx" RPC command should return
            // neither "DeliveredAmount" nor "delivered_amount" on a CheckCash
            // transaction.
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
                env.tx()->getJson(JsonOptions::none)[jss::hash].asString()};

            // DeliveredAmount and delivered_amount are either present or
            // not present in the metadata returned by "tx" based on fix1623.
            env.close();
            Json::Value const meta =
                env.rpc("tx", txHash)[jss::result][jss::meta];

            BEAST_EXPECT(
                meta.isMember(sfDeliveredAmount.jsonName) == hasFields);
            BEAST_EXPECT(meta.isMember(jss::delivered_amount) == hasFields);
        };

        // Run both the disabled and enabled cases.
        testEnable(features - fix1623, false);
        testEnable(features, true);
    }

    void
    testWithTickets(FeatureBitset features)
    {
        testcase("With Tickets");

        using namespace test::jtx;

        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        IOU const USD{gw["USD"]};

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
        env.require(owners(alice, 10));
        env.require(owners(bob, 10));

        // alice gets enough USD to write a few checks.
        env(trust(alice, USD(1000)), ticket::use(aliceTicketSeq++));
        env(trust(bob, USD(1000)), ticket::use(bobTicketSeq++));
        env.close();
        env.require(owners(alice, 10));
        env.require(owners(bob, 10));

        env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        env.require(tickets(bob, env.seq(bob) - bobTicketSeq));
        BEAST_EXPECT(env.seq(bob) == bobSeq);

        env(pay(gw, alice, USD(900)));
        env.close();

        // alice creates four checks; two XRP, two IOU.  Bob will cash
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
        env.require(owners(alice, 10));
        env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
        BEAST_EXPECT(checksOnAccount(env, alice).size() == 4);
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        env.require(owners(bob, 10));
        BEAST_EXPECT(env.seq(bob) == bobSeq);

        // Bob cancels two of alice's checks.
        env(check::cancel(bob, chkIdXrp1), ticket::use(bobTicketSeq++));
        env(check::cancel(bob, chkIdUsd2), ticket::use(bobTicketSeq++));
        env.close();

        env.require(owners(alice, 8));
        env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
        BEAST_EXPECT(checksOnAccount(env, alice).size() == 2);
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        env.require(owners(bob, 8));
        BEAST_EXPECT(env.seq(bob) == bobSeq);

        // Bob cashes alice's two remaining checks.
        env(check::cash(bob, chkIdXrp2, XRP(300)), ticket::use(bobTicketSeq++));
        env(check::cash(bob, chkIdUsd1, USD(200)), ticket::use(bobTicketSeq++));
        env.close();

        env.require(owners(alice, 6));
        env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
        BEAST_EXPECT(checksOnAccount(env, alice).size() == 0);
        BEAST_EXPECT(env.seq(alice) == aliceSeq);
        env.require(balance(alice, USD(700)));
        env.require(balance(alice, drops(699'999'940)));

        env.require(owners(bob, 6));
        BEAST_EXPECT(env.seq(bob) == bobSeq);
        env.require(balance(bob, USD(200)));
        env.require(balance(bob, drops(1'299'999'940)));
    }

    void
    testTrustLineCreation(FeatureBitset features)
    {
        // Explore automatic trust line creation when a check is cashed.
        //
        // This capability is enabled by the featureCheckCashMakesTrustLine
        // amendment.  So this test executes only when that amendment is
        // active.
        assert(features[featureCheckCashMakesTrustLine]);

        testcase("Trust Line Creation");

        using namespace test::jtx;

        Env env{*this, features};

        // An account that independently tracks its owner count.
        struct AccountOwns
        {
            beast::unit_test::suite& suite;
            Env const& env;
            Account const acct;
            std::size_t owners;

            void
            verifyOwners(std::uint32_t line) const
            {
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

            IOU
            operator[](std::string const& s) const
            {
                return acct[s];
            }
        };

        AccountOwns alice{*this, env, "alice", 0};
        AccountOwns bob{*this, env, "bob", 0};

        // Fund with noripple so the accounts do not have any flags set.
        env.fund(XRP(5000), noripple(alice, bob));
        env.close();

        // Automatic trust line creation should fail if the check destination
        // can't afford the reserve for the trust line.
        {
            AccountOwns gw1{*this, env, "gw1", 0};

            // Fund gw1 with noripple (even though that's atypical for a
            // gateway) so it does not have any flags set.  We'll set flags
            // on gw1 later.
            env.fund(XRP(5000), noripple(gw1));
            env.close();

            IOU const CK8 = gw1["CK8"];
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
            auto const offerLine =
                env.le(keylet::line(acct1, acct2, offerIou.currency));
            auto const checkLine =
                env.le(keylet::line(acct1, acct2, checkIou.currency));
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
                auto cmpReqAmount =
                    [this, offerLine, checkLine](SF_AMOUNT const& sfield) {
                        STAmount const offerAmount = offerLine->at(sfield);
                        STAmount const checkAmount = checkLine->at(sfield);

                        // Neither STAmount should be native.
                        if (!BEAST_EXPECT(
                                !offerAmount.native() && !checkAmount.native()))
                            return;

                        BEAST_EXPECT(
                            offerAmount.issue().account ==
                            checkAmount.issue().account);
                        BEAST_EXPECT(
                            offerAmount.negative() == checkAmount.negative());
                        BEAST_EXPECT(
                            offerAmount.mantissa() == checkAmount.mantissa());
                        BEAST_EXPECT(
                            offerAmount.exponent() == checkAmount.exponent());
                    };
                cmpReqAmount(sfBalance);
                cmpReqAmount(sfLowLimit);
                cmpReqAmount(sfHighLimit);
            }
            {
                // Lambda that compares the contents of optional fields.
                auto cmpOptField =
                    [this, offerLine, checkLine](auto const& sfield) {
                        // Expect both fields to either be present or absent.
                        if (!BEAST_EXPECT(
                                offerLine->isFieldPresent(sfield) ==
                                checkLine->isFieldPresent(sfield)))
                            return;

                        // If both fields are absent then there's nothing
                        // further to check.
                        if (!offerLine->isFieldPresent(sfield))
                            return;

                        // Both optional fields are present so we can compare
                        // them.
                        BEAST_EXPECT(
                            offerLine->at(sfield) == checkLine->at(sfield));
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
            AccountOwns gw1{*this, env, "gw1", 0};

            BEAST_EXPECT((*env.le(gw1))[sfFlags] == 0);
            BEAST_EXPECT((*env.le(alice))[sfFlags] == 0);
            BEAST_EXPECT((*env.le(bob))[sfFlags] == 0);

            // Use offers to automatically create the trust line.
            IOU const OF1 = gw1["OF1"];
            env(offer(gw1, XRP(98), OF1(98)));
            env.close();
            BEAST_EXPECT(
                env.le(keylet::line(gw1, alice, OF1.currency)) == nullptr);
            env(offer(alice, OF1(98), XRP(98)));
            ++alice.owners;
            env.close();

            // Both offers should be consumed.
            // Since gw1's offer was consumed and the trust line was not
            // created by gw1, gw1's owner count should be 0.
            gw1.verifyOwners(__LINE__);

            // alice's automatically created trust line bumps her owner count.
            alice.verifyOwners(__LINE__);

            // Use check cashing to automatically create the trust line.
            IOU const CK1 = gw1["CK1"];
            uint256 const chkId{getCheckIndex(gw1, env.seq(gw1))};
            env(check::create(gw1, alice, CK1(98)));
            env.close();
            BEAST_EXPECT(
                env.le(keylet::line(gw1, alice, CK1.currency)) == nullptr);
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

            cmpTrustLines(gw1, alice, OF1, CK1);
        }
        //--------- No account root flags, check written by non-issuer ---------
        {
            // No account root flags on any participant.
            // Automatic trust line from non-issuer to non-issuer.

            // Use offers to automatically create the trust line.
            // Transfer of assets using offers does not require rippling.
            // So bob's offer is successfully crossed which creates the
            // trust line.
            AccountOwns gw1{*this, env, "gw1", 0};
            IOU const OF1 = gw1["OF1"];
            env(offer(alice, XRP(97), OF1(97)));
            env.close();
            BEAST_EXPECT(
                env.le(keylet::line(alice, bob, OF1.currency)) == nullptr);
            env(offer(bob, OF1(97), XRP(97)));
            ++bob.owners;
            env.close();

            // Both offers should be consumed.
            env.require(balance(alice, OF1(1)));
            env.require(balance(bob, OF1(97)));

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
            IOU const CK1 = gw1["CK1"];
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, CK1(97)));
            env.close();
            BEAST_EXPECT(
                env.le(keylet::line(alice, bob, CK1.currency)) == nullptr);
            env(check::cash(bob, chkId, CK1(97)), ter(terNO_RIPPLE));
            env.close();

            BEAST_EXPECT(
                env.le(keylet::line(gw1, bob, OF1.currency)) != nullptr);
            BEAST_EXPECT(
                env.le(keylet::line(gw1, bob, CK1.currency)) == nullptr);

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
            AccountOwns gw1{*this, env, "gw1", 0};
            env(fset(gw1, asfDefaultRipple));
            env.close();

            // Use offers to automatically create the trust line.
            IOU const OF2 = gw1["OF2"];
            env(offer(gw1, XRP(96), OF2(96)));
            env.close();
            BEAST_EXPECT(
                env.le(keylet::line(gw1, alice, OF2.currency)) == nullptr);
            env(offer(alice, OF2(96), XRP(96)));
            ++alice.owners;
            env.close();

            // Both offers should be consumed.
            // Since gw1's offer was consumed and the trust line was not
            // created by gw1, gw1's owner count should still be 0.
            gw1.verifyOwners(__LINE__);

            // alice's automatically created trust line bumps her owner count.
            alice.verifyOwners(__LINE__);

            // Use check cashing to automatically create the trust line.
            IOU const CK2 = gw1["CK2"];
            uint256 const chkId{getCheckIndex(gw1, env.seq(gw1))};
            env(check::create(gw1, alice, CK2(96)));
            env.close();
            BEAST_EXPECT(
                env.le(keylet::line(gw1, alice, CK2.currency)) == nullptr);
            env(check::cash(alice, chkId, CK2(96)));
            ++alice.owners;
            verifyDeliveredAmount(env, CK2(96));
            env.close();

            // gw1's check should be consumed.
            // Since gw1's check was consumed and the trust line was not
            // created by gw1, gw1's owner count should still be 0.
            gw1.verifyOwners(__LINE__);

            // alice's automatically created trust line bumps her owner count.
            alice.verifyOwners(__LINE__);

            cmpTrustLines(gw1, alice, OF2, CK2);
        }
        //----------- lsfDefaultRipple, check written by non-issuer ------------
        {
            // gw1 enabled rippling, so automatic trust line from non-issuer
            // to non-issuer should work.

            // Use offers to automatically create the trust line.
            AccountOwns gw1{*this, env, "gw1", 0};
            IOU const OF2 = gw1["OF2"];
            env(offer(alice, XRP(95), OF2(95)));
            env.close();
            BEAST_EXPECT(
                env.le(keylet::line(alice, bob, OF2.currency)) == nullptr);
            env(offer(bob, OF2(95), XRP(95)));
            ++bob.owners;
            env.close();

            // bob's owner count should increase due to the new trust line.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Use check cashing to automatically create the trust line.
            IOU const CK2 = gw1["CK2"];
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, CK2(95)));
            env.close();
            BEAST_EXPECT(
                env.le(keylet::line(alice, bob, CK2.currency)) == nullptr);
            env(check::cash(bob, chkId, CK2(95)));
            ++bob.owners;
            verifyDeliveredAmount(env, CK2(95));
            env.close();

            // bob's owner count should increase due to the new trust line.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            cmpTrustLines(alice, bob, OF2, CK2);
        }

        //-------------- lsfDepositAuth, check written by issuer ---------------
        {
            // Both offers and checks ignore the lsfDepositAuth flag, since
            // the destination signs the transaction that delivers their funds.
            // So setting lsfDepositAuth on all the participants should not
            // change any outcomes.
            //
            // Automatic trust line from issuer to non-issuer should still work.
            AccountOwns gw1{*this, env, "gw1", 0};
            env(fset(gw1, asfDepositAuth));
            env(fset(alice, asfDepositAuth));
            env(fset(bob, asfDepositAuth));
            env.close();

            // Use offers to automatically create the trust line.
            IOU const OF3 = gw1["OF3"];
            env(offer(gw1, XRP(94), OF3(94)));
            env.close();
            BEAST_EXPECT(
                env.le(keylet::line(gw1, alice, OF3.currency)) == nullptr);
            env(offer(alice, OF3(94), XRP(94)));
            ++alice.owners;
            env.close();

            // Both offers should be consumed.
            // Since gw1's offer was consumed and the trust line was not
            // created by gw1, gw1's owner count should still be 0.
            gw1.verifyOwners(__LINE__);

            // alice's automatically created trust line bumps her owner count.
            alice.verifyOwners(__LINE__);

            // Use check cashing to automatically create the trust line.
            IOU const CK3 = gw1["CK3"];
            uint256 const chkId{getCheckIndex(gw1, env.seq(gw1))};
            env(check::create(gw1, alice, CK3(94)));
            env.close();
            BEAST_EXPECT(
                env.le(keylet::line(gw1, alice, CK3.currency)) == nullptr);
            env(check::cash(alice, chkId, CK3(94)));
            ++alice.owners;
            verifyDeliveredAmount(env, CK3(94));
            env.close();

            // gw1's check should be consumed.
            // Since gw1's check was consumed and the trust line was not
            // created by gw1, gw1's owner count should still be 0.
            gw1.verifyOwners(__LINE__);

            // alice's automatically created trust line bumps her owner count.
            alice.verifyOwners(__LINE__);

            cmpTrustLines(gw1, alice, OF3, CK3);
        }
        //------------ lsfDepositAuth, check written by non-issuer -------------
        {
            // The presence of the lsfDepositAuth flag should not affect
            // automatic trust line creation.

            // Use offers to automatically create the trust line.
            AccountOwns gw1{*this, env, "gw1", 0};
            IOU const OF3 = gw1["OF3"];
            env(offer(alice, XRP(93), OF3(93)));
            env.close();
            BEAST_EXPECT(
                env.le(keylet::line(alice, bob, OF3.currency)) == nullptr);
            env(offer(bob, OF3(93), XRP(93)));
            ++bob.owners;
            env.close();

            // bob's owner count should increase due to the new trust line.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Use check cashing to automatically create the trust line.
            IOU const CK3 = gw1["CK3"];
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, CK3(93)));
            env.close();
            BEAST_EXPECT(
                env.le(keylet::line(alice, bob, CK3.currency)) == nullptr);
            env(check::cash(bob, chkId, CK3(93)));
            ++bob.owners;
            verifyDeliveredAmount(env, CK3(93));
            env.close();

            // bob's owner count should increase due to the new trust line.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            cmpTrustLines(alice, bob, OF3, CK3);
        }

        //-------------- lsfGlobalFreeze, check written by issuer --------------
        {
            // Set lsfGlobalFreeze on gw1.  That should stop any automatic
            // trust lines from being created.
            AccountOwns gw1{*this, env, "gw1", 0};
            env(fset(gw1, asfGlobalFreeze));
            env.close();

            // Use offers to automatically create the trust line.
            IOU const OF4 = gw1["OF4"];
            env(offer(gw1, XRP(92), OF4(92)), ter(tecFROZEN));
            env.close();
            BEAST_EXPECT(
                env.le(keylet::line(gw1, alice, OF4.currency)) == nullptr);
            env(offer(alice, OF4(92), XRP(92)), ter(tecFROZEN));
            env.close();

            // No one's owner count should have changed.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Use check cashing to automatically create the trust line.
            IOU const CK4 = gw1["CK4"];
            uint256 const chkId{getCheckIndex(gw1, env.seq(gw1))};
            env(check::create(gw1, alice, CK4(92)), ter(tecFROZEN));
            env.close();
            BEAST_EXPECT(
                env.le(keylet::line(gw1, alice, CK4.currency)) == nullptr);
            env(check::cash(alice, chkId, CK4(92)), ter(tecNO_ENTRY));
            env.close();

            // No one's owner count should have changed.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Because gw1 has set lsfGlobalFreeze, neither trust line
            // is created.
            BEAST_EXPECT(
                env.le(keylet::line(gw1, alice, OF4.currency)) == nullptr);
            BEAST_EXPECT(
                env.le(keylet::line(gw1, alice, CK4.currency)) == nullptr);
        }
        //------------ lsfGlobalFreeze, check written by non-issuer ------------
        {
            // Since gw1 has the lsfGlobalFreeze flag set, there should be
            // no automatic trust line creation between non-issuers.

            // Use offers to automatically create the trust line.
            AccountOwns gw1{*this, env, "gw1", 0};
            IOU const OF4 = gw1["OF4"];
            env(offer(alice, XRP(91), OF4(91)), ter(tecFROZEN));
            env.close();
            BEAST_EXPECT(
                env.le(keylet::line(alice, bob, OF4.currency)) == nullptr);
            env(offer(bob, OF4(91), XRP(91)), ter(tecFROZEN));
            env.close();

            // No one's owner count should have changed.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Use check cashing to automatically create the trust line.
            IOU const CK4 = gw1["CK4"];
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, CK4(91)), ter(tecFROZEN));
            env.close();
            BEAST_EXPECT(
                env.le(keylet::line(alice, bob, CK4.currency)) == nullptr);
            env(check::cash(bob, chkId, CK4(91)), ter(tecNO_ENTRY));
            env.close();

            // No one's owner count should have changed.
            gw1.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Because gw1 has set lsfGlobalFreeze, neither trust line
            // is created.
            BEAST_EXPECT(
                env.le(keylet::line(gw1, bob, OF4.currency)) == nullptr);
            BEAST_EXPECT(
                env.le(keylet::line(gw1, bob, CK4.currency)) == nullptr);
        }

        //-------------- lsfRequireAuth, check written by issuer ---------------

        // We want to test the lsfRequireAuth flag, but we can't set that
        // flag on an account that already has trust lines.  So we'll fund
        // a new gateway and use that.
        {
            AccountOwns gw2{*this, env, "gw2", 0};
            env.fund(XRP(5000), gw2);
            env.close();

            // Set lsfRequireAuth on gw2.  That should stop any automatic
            // trust lines from being created.
            env(fset(gw2, asfRequireAuth));
            env.close();

            // Use offers to automatically create the trust line.
            IOU const OF5 = gw2["OF5"];
            std::uint32_t gw2OfferSeq = {env.seq(gw2)};
            env(offer(gw2, XRP(92), OF5(92)));
            ++gw2.owners;
            env.close();
            BEAST_EXPECT(
                env.le(keylet::line(gw2, alice, OF5.currency)) == nullptr);
            env(offer(alice, OF5(92), XRP(92)), ter(tecNO_LINE));
            env.close();

            // gw2 should still own the offer, but no one else's owner
            // count should have changed.
            gw2.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Since we don't need it any more, remove gw2's offer.
            env(offer_cancel(gw2, gw2OfferSeq));
            --gw2.owners;
            env.close();
            gw2.verifyOwners(__LINE__);

            // Use check cashing to automatically create the trust line.
            IOU const CK5 = gw2["CK5"];
            uint256 const chkId{getCheckIndex(gw2, env.seq(gw2))};
            env(check::create(gw2, alice, CK5(92)));
            ++gw2.owners;
            env.close();
            BEAST_EXPECT(
                env.le(keylet::line(gw2, alice, CK5.currency)) == nullptr);
            env(check::cash(alice, chkId, CK5(92)), ter(tecNO_AUTH));
            env.close();

            // gw2 should still own the check, but no one else's owner
            // count should have changed.
            gw2.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Because gw2 has set lsfRequireAuth, neither trust line
            // is created.
            BEAST_EXPECT(
                env.le(keylet::line(gw2, alice, OF5.currency)) == nullptr);
            BEAST_EXPECT(
                env.le(keylet::line(gw2, alice, CK5.currency)) == nullptr);

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
            AccountOwns gw2{*this, env, "gw2", 0};
            IOU const OF5 = gw2["OF5"];
            env(offer(alice, XRP(91), OF5(91)), ter(tecUNFUNDED_OFFER));
            env.close();
            env(offer(bob, OF5(91), XRP(91)), ter(tecNO_LINE));
            BEAST_EXPECT(
                env.le(keylet::line(gw2, bob, OF5.currency)) == nullptr);
            env.close();

            gw2.verifyOwners(__LINE__);
            alice.verifyOwners(__LINE__);
            bob.verifyOwners(__LINE__);

            // Use check cashing to automatically create the trust line.
            IOU const CK5 = gw2["CK5"];
            uint256 const chkId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, bob, CK5(91)));
            env.close();
            BEAST_EXPECT(
                env.le(keylet::line(alice, bob, CK5.currency)) == nullptr);
            env(check::cash(bob, chkId, CK5(91)), ter(tecPATH_PARTIAL));
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
            BEAST_EXPECT(
                env.le(keylet::line(gw2, bob, OF5.currency)) == nullptr);
            BEAST_EXPECT(
                env.le(keylet::line(gw2, bob, CK5.currency)) == nullptr);
        }
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testEnabled(features);
        testCreateValid(features);
        testCreateInvalid(features);
        testCashXRP(features);
        testCashIOU(features);
        testCashXferFee(features);
        testCashQuality(features);
        testCashInvalid(features);
        testCancelValid(features);
        testCancelInvalid(features);
        testFix1623Enable(features);
        testWithTickets(features);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        auto const sa = supported_amendments();
        testWithFeats(sa - featureCheckCashMakesTrustLine);
        testWithFeats(sa);

        testTrustLineCreation(sa);  // Test with featureCheckCashMakesTrustLine
    }
};

BEAST_DEFINE_TESTSUITE(Check, tx, ripple);

}  // namespace ripple

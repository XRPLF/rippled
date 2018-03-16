//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <BeastConfig.h>
#include <ripple/basics/chrono.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/PayChan.h>
#include <ripple/protocol/TxFlags.h>
#include <test/jtx.h>

#include <chrono>

namespace ripple
{
namespace test
{
struct PayChan_test : public beast::unit_test::suite
{
    static
    uint256
    channel (ReadView const& view,
        jtx::Account const& account,
        jtx::Account const& dst)
    {
        auto const sle = view.read (keylet::account (account));
        if (!sle)
            return beast::zero;
        auto const k = keylet::payChan (account, dst, (*sle)[sfSequence] - 1);
        return k.key;
    }

    static Buffer
    signClaimAuth (PublicKey const& pk,
        SecretKey const& sk,
        uint256 const& channel,
        STAmount const& authAmt)
    {
        Serializer msg;
        serializePayChanAuthorization (msg, channel, authAmt.xrp ());
        return sign (pk, sk, msg.slice ());
    }

    static
    STAmount
    channelBalance (ReadView const& view, uint256 const& chan)
    {
        auto const slep = view.read ({ltPAYCHAN, chan});
        if (!slep)
            return XRPAmount{-1};
        return (*slep)[sfBalance];
    }

    static
    bool
    channelExists (ReadView const& view, uint256 const& chan)
    {
        auto const slep = view.read ({ltPAYCHAN, chan});
        return bool(slep);
    }

    static
    STAmount
    channelAmount (ReadView const& view, uint256 const& chan)
    {
        auto const slep = view.read ({ltPAYCHAN, chan});
        if (!slep)
            return XRPAmount{-1};
        return (*slep)[sfAmount];
    }

    static
    boost::optional<std::int64_t>
    channelExpiration (ReadView const& view, uint256 const& chan)
    {
        auto const slep = view.read ({ltPAYCHAN, chan});
        if (!slep)
            return boost::none;
        if (auto const r = (*slep)[~sfExpiration])
            return r.value();
        return boost::none;

    }

    static Json::Value
    create (jtx::Account const& account,
        jtx::Account const& to,
        STAmount const& amount,
        NetClock::duration const& settleDelay,
        PublicKey const& pk,
        boost::optional<NetClock::time_point> const& cancelAfter = boost::none,
        boost::optional<std::uint32_t> const& dstTag = boost::none)
    {
        using namespace jtx;
        Json::Value jv;
        jv[jss::TransactionType] = "PaymentChannelCreate";
        jv[jss::Flags] = tfUniversal;
        jv[jss::Account] = account.human ();
        jv[jss::Destination] = to.human ();
        jv[jss::Amount] = amount.getJson (0);
        jv["SettleDelay"] = settleDelay.count ();
        jv["PublicKey"] = strHex (pk.slice ());
        if (cancelAfter)
            jv["CancelAfter"] = cancelAfter->time_since_epoch ().count ();
        if (dstTag)
            jv["DestinationTag"] = *dstTag;
        return jv;
    }

    static
    Json::Value
    fund (jtx::Account const& account,
        uint256 const& channel,
        STAmount const& amount,
        boost::optional<NetClock::time_point> const& expiration = boost::none)
    {
        using namespace jtx;
        Json::Value jv;
        jv[jss::TransactionType] = "PaymentChannelFund";
        jv[jss::Flags] = tfUniversal;
        jv[jss::Account] = account.human ();
        jv["Channel"] = to_string (channel);
        jv[jss::Amount] = amount.getJson (0);
        if (expiration)
            jv["Expiration"] = expiration->time_since_epoch ().count ();
        return jv;
    }

    static
    Json::Value
    claim (jtx::Account const& account,
        uint256 const& channel,
        boost::optional<STAmount> const& balance = boost::none,
        boost::optional<STAmount> const& amount = boost::none,
        boost::optional<Slice> const& signature = boost::none,
        boost::optional<PublicKey> const& pk = boost::none)
    {
        using namespace jtx;
        Json::Value jv;
        jv[jss::TransactionType] = "PaymentChannelClaim";
        jv[jss::Flags] = tfUniversal;
        jv[jss::Account] = account.human ();
        jv["Channel"] = to_string (channel);
        if (amount)
            jv[jss::Amount] = amount->getJson (0);
        if (balance)
            jv["Balance"] = balance->getJson (0);
        if (signature)
            jv["Signature"] = strHex (*signature);
        if (pk)
            jv["PublicKey"] = strHex (pk->slice ());
        return jv;
    }

    void
    testSimple ()
    {
        testcase ("simple");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env (*this);
        auto const alice = Account ("alice");
        auto const bob = Account ("bob");
        auto USDA = alice["USD"];
        env.fund (XRP (10000), alice, bob);
        auto const pk = alice.pk ();
        auto const settleDelay = 100s;
        env (create (alice, bob, XRP (1000), settleDelay, pk));
        auto const chan = channel (*env.current (), alice, bob);
        BEAST_EXPECT (channelBalance (*env.current (), chan) == XRP (0));
        BEAST_EXPECT (channelAmount (*env.current (), chan) == XRP (1000));

        {
            auto const preAlice = env.balance (alice);
            env (fund (alice, chan, XRP (1000)));
            auto const feeDrops = env.current ()->fees ().base;
            BEAST_EXPECT (env.balance (alice) == preAlice - XRP (1000) - feeDrops);
        }

        auto chanBal = channelBalance (*env.current (), chan);
        auto chanAmt = channelAmount (*env.current (), chan);
        BEAST_EXPECT (chanBal == XRP (0));
        BEAST_EXPECT (chanAmt == XRP (2000));

        {
            // bad amounts (non-xrp, negative amounts)
            env (create (alice, bob, USDA (1000), settleDelay, pk),
                ter (temBAD_AMOUNT));
            env (fund (alice, chan, USDA (1000)),
                ter (temBAD_AMOUNT));
            env (create (alice, bob, XRP (-1000), settleDelay, pk),
                ter (temBAD_AMOUNT));
            env (fund (alice, chan, XRP (-1000)),
                ter (temBAD_AMOUNT));
        }

        // invalid account
        env (create (alice, "noAccount", XRP (1000), settleDelay, pk),
            ter (tecNO_DST));
        // can't create channel to the same account
        env (create (alice, alice, XRP (1000), settleDelay, pk),
             ter (temDST_IS_SRC));
        // invalid channel
        env (fund (alice, channel (*env.current (), alice, "noAccount"), XRP (1000)),
            ter (tecNO_ENTRY));
        // not enough funds
        env (create (alice, bob, XRP (10000), settleDelay, pk),
            ter (tecUNFUNDED));

        {
            // No signature claim with bad amounts (negative and non-xrp)
            auto const iou = USDA (100).value ();
            auto const negXRP = XRP (-100).value ();
            auto const posXRP = XRP (100).value ();
            env (claim (alice, chan, iou, iou), ter (temBAD_AMOUNT));
            env (claim (alice, chan, posXRP, iou), ter (temBAD_AMOUNT));
            env (claim (alice, chan, iou, posXRP), ter (temBAD_AMOUNT));
            env (claim (alice, chan, negXRP, negXRP), ter (temBAD_AMOUNT));
            env (claim (alice, chan, posXRP, negXRP), ter (temBAD_AMOUNT));
            env (claim (alice, chan, negXRP, posXRP), ter (temBAD_AMOUNT));
        }
        {
            // No signature claim more than authorized
            auto const delta = XRP (500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = reqBal + XRP (-100);
            assert (reqBal <= chanAmt);
            env (claim (alice, chan, reqBal, authAmt), ter (temBAD_AMOUNT));
        }
        {
            // No signature needed since the owner is claiming
            auto const preBob = env.balance (bob);
            auto const delta = XRP (500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = reqBal + XRP (100);
            assert (reqBal <= chanAmt);
            env (claim (alice, chan, reqBal, authAmt));
            BEAST_EXPECT (channelBalance (*env.current (), chan) == reqBal);
            BEAST_EXPECT (channelAmount (*env.current (), chan) == chanAmt);
            BEAST_EXPECT (env.balance (bob) == preBob + delta);
            chanBal = reqBal;
        }
        {
            // Claim with signature
            auto preBob = env.balance (bob);
            auto const delta = XRP (500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = reqBal + XRP (100);
            assert (reqBal <= chanAmt);
            auto const sig =
                signClaimAuth (alice.pk (), alice.sk (), chan, authAmt);
            env (claim (bob, chan, reqBal, authAmt, Slice (sig), alice.pk ()));
            BEAST_EXPECT (channelBalance (*env.current (), chan) == reqBal);
            BEAST_EXPECT (channelAmount (*env.current (), chan) == chanAmt);
            auto const feeDrops = env.current ()->fees ().base;
            BEAST_EXPECT (env.balance (bob) == preBob + delta - feeDrops);
            chanBal = reqBal;

            // claim again
            preBob = env.balance (bob);
            env (claim (bob, chan, reqBal, authAmt, Slice (sig), alice.pk ()),
                ter (tecUNFUNDED_PAYMENT));
            BEAST_EXPECT (channelBalance (*env.current (), chan) == chanBal);
            BEAST_EXPECT (channelAmount (*env.current (), chan) == chanAmt);
            BEAST_EXPECT (env.balance (bob) == preBob - feeDrops);
        }
        {
            // Try to claim more than authorized
            auto const preBob = env.balance (bob);
            STAmount const authAmt = chanBal + XRP (500);
            STAmount const reqAmt = authAmt + 1;
            assert (reqAmt <= chanAmt);
            auto const sig =
                signClaimAuth (alice.pk (), alice.sk (), chan, authAmt);
            env (claim (bob, chan, reqAmt, authAmt, Slice (sig), alice.pk ()),
                ter (temBAD_AMOUNT));
            BEAST_EXPECT (channelBalance (*env.current (), chan) == chanBal);
            BEAST_EXPECT (channelAmount (*env.current (), chan) == chanAmt);
            BEAST_EXPECT (env.balance (bob) == preBob);
        }

        // Dst tries to fund the channel
        env (fund (bob, chan, XRP (1000)), ter (tecNO_PERMISSION));
        BEAST_EXPECT (channelBalance (*env.current (), chan) == chanBal);
        BEAST_EXPECT (channelAmount (*env.current (), chan) == chanAmt);

        {
            // Wrong signing key
            auto const sig =
                signClaimAuth (bob.pk (), bob.sk (), chan, XRP (1500));
            env (claim (bob, chan, XRP (1500).value (), XRP (1500).value (),
                     Slice (sig), bob.pk ()),
                ter (temBAD_SIGNER));
            BEAST_EXPECT (channelBalance (*env.current (), chan) == chanBal);
            BEAST_EXPECT (channelAmount (*env.current (), chan) == chanAmt);
        }
        {
            // Bad signature
            auto const sig =
                signClaimAuth (bob.pk (), bob.sk (), chan, XRP (1500));
            env (claim (bob, chan, XRP (1500).value (), XRP (1500).value (),
                     Slice (sig), alice.pk ()),
                ter (temBAD_SIGNATURE));
            BEAST_EXPECT (channelBalance (*env.current (), chan) == chanBal);
            BEAST_EXPECT (channelAmount (*env.current (), chan) == chanAmt);
        }
        {
            // Dst closes channel
            auto const preAlice = env.balance (alice);
            auto const preBob = env.balance (bob);
            env (claim (bob, chan), txflags (tfClose));
            BEAST_EXPECT (!channelExists (*env.current (), chan));
            auto const feeDrops = env.current ()->fees ().base;
            auto const delta = chanAmt - chanBal;
            assert (delta > beast::zero);
            BEAST_EXPECT (env.balance (alice) == preAlice + delta);
            BEAST_EXPECT (env.balance (bob) == preBob - feeDrops);
        }
    }

    void
    testCancelAfter ()
    {
        testcase ("cancel after");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        auto const alice = Account ("alice");
        auto const bob = Account ("bob");
        auto const carol = Account ("carol");
        {
            // If dst claims after cancel after, channel closes
            Env env (*this);
            env.fund (XRP (10000), alice, bob);
            auto const pk = alice.pk ();
            auto const settleDelay = 100s;
            NetClock::time_point const cancelAfter =
                env.current ()->info ().parentCloseTime + 3600s;
            auto const channelFunds = XRP (1000);
            env (create (
                alice, bob, channelFunds, settleDelay, pk, cancelAfter));
            auto const chan = channel (*env.current (), alice, bob);
            if (!chan)
            {
                fail ();
                return;
            }
            BEAST_EXPECT (channelExists (*env.current (), chan));
            env.close (cancelAfter);
            {
                // dst cannot claim after cancelAfter
                auto const chanBal = channelBalance (*env.current (), chan);
                auto const chanAmt = channelAmount (*env.current (), chan);
                auto preAlice = env.balance (alice);
                auto preBob = env.balance (bob);
                auto const delta = XRP (500);
                auto const reqBal = chanBal + delta;
                auto const authAmt = reqBal + XRP (100);
                assert (reqBal <= chanAmt);
                auto const sig =
                    signClaimAuth (alice.pk (), alice.sk (), chan, authAmt);
                env (claim (
                    bob, chan, reqBal, authAmt, Slice (sig), alice.pk ()));
                auto const feeDrops = env.current ()->fees ().base;
                BEAST_EXPECT (!channelExists (*env.current (), chan));
                BEAST_EXPECT (env.balance (bob) == preBob - feeDrops);
                BEAST_EXPECT (env.balance (alice) == preAlice + channelFunds);
            }
        }
        {
            // Third party can close after cancel after
            Env env (*this);
            env.fund (XRP (10000), alice, bob, carol);
            auto const pk = alice.pk ();
            auto const settleDelay = 100s;
            NetClock::time_point const cancelAfter =
                env.current ()->info ().parentCloseTime + 3600s;
            auto const channelFunds = XRP (1000);
            env (create (
                alice, bob, channelFunds, settleDelay, pk, cancelAfter));
            auto const chan = channel (*env.current (), alice, bob);
            BEAST_EXPECT (channelExists (*env.current (), chan));
            // third party close before cancelAfter
            env (claim (carol, chan), txflags (tfClose),
                ter (tecNO_PERMISSION));
            BEAST_EXPECT (channelExists (*env.current (), chan));
            env.close (cancelAfter);
            // third party close after cancelAfter
            auto const preAlice = env.balance (alice);
            env (claim (carol, chan), txflags (tfClose));
            BEAST_EXPECT (!channelExists (*env.current (), chan));
            BEAST_EXPECT (env.balance (alice) == preAlice + channelFunds);
        }
    }

    void
    testExpiration ()
    {
        testcase ("expiration");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env (*this);
        auto const alice = Account ("alice");
        auto const bob = Account ("bob");
        auto const carol = Account ("carol");
        env.fund (XRP (10000), alice, bob, carol);
        auto const pk = alice.pk ();
        auto const settleDelay = 3600s;
        auto const closeTime = env.current ()->info ().parentCloseTime;
        auto const minExpiration = closeTime + settleDelay;
        NetClock::time_point const cancelAfter = closeTime + 7200s;
        auto const channelFunds = XRP (1000);
        env (create (alice, bob, channelFunds, settleDelay, pk, cancelAfter));
        auto const chan = channel (*env.current (), alice, bob);
        BEAST_EXPECT (channelExists (*env.current (), chan));
        BEAST_EXPECT (!channelExpiration (*env.current (), chan));
        // Owner closes, will close after settleDelay
        env (claim (alice, chan), txflags (tfClose));
        auto counts = [](
            auto const& t) { return t.time_since_epoch ().count (); };
        BEAST_EXPECT (*channelExpiration (*env.current (), chan) ==
            counts (minExpiration));
        // increase the expiration time
        env (fund (
            alice, chan, XRP (1), NetClock::time_point{minExpiration + 100s}));
        BEAST_EXPECT (*channelExpiration (*env.current (), chan) ==
            counts (minExpiration) + 100);
        // decrease the expiration, but still above minExpiration
        env (fund (
            alice, chan, XRP (1), NetClock::time_point{minExpiration + 50s}));
        BEAST_EXPECT (*channelExpiration (*env.current (), chan) ==
            counts (minExpiration) + 50);
        // decrease the expiration below minExpiration
        env (fund (alice, chan, XRP (1),
                 NetClock::time_point{minExpiration - 50s}),
            ter (temBAD_EXPIRATION));
        BEAST_EXPECT (*channelExpiration (*env.current (), chan) ==
            counts (minExpiration) + 50);
        env (claim (bob, chan), txflags (tfRenew), ter (tecNO_PERMISSION));
        BEAST_EXPECT (*channelExpiration (*env.current (), chan) ==
            counts (minExpiration) + 50);
        env (claim (alice, chan), txflags (tfRenew));
        BEAST_EXPECT (!channelExpiration (*env.current (), chan));
        // decrease the expiration below minExpiration
        env (fund (alice, chan, XRP (1),
                 NetClock::time_point{minExpiration - 50s}),
            ter (temBAD_EXPIRATION));
        BEAST_EXPECT (!channelExpiration (*env.current (), chan));
        env (fund (alice, chan, XRP (1), NetClock::time_point{minExpiration}));
        env.close (minExpiration);
        // Try to extend the expiration after the expiration has already passed
        env (fund (
            alice, chan, XRP (1), NetClock::time_point{minExpiration + 1000s}));
        BEAST_EXPECT (!channelExists (*env.current (), chan));
    }

    void
    testSettleDelay ()
    {
        testcase ("settle delay");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env (*this);
        auto const alice = Account ("alice");
        auto const bob = Account ("bob");
        env.fund (XRP (10000), alice, bob);
        auto const pk = alice.pk ();
        auto const settleDelay = 3600s;
        NetClock::time_point const settleTimepoint =
            env.current ()->info ().parentCloseTime + settleDelay;
        auto const channelFunds = XRP (1000);
        env (create (alice, bob, channelFunds, settleDelay, pk));
        auto const chan = channel (*env.current (), alice, bob);
        BEAST_EXPECT (channelExists (*env.current (), chan));
        // Owner closes, will close after settleDelay
        env (claim (alice, chan), txflags (tfClose));
        BEAST_EXPECT (channelExists (*env.current (), chan));
        env.close (settleTimepoint-settleDelay/2);
        {
            // receiver can still claim
            auto const chanBal = channelBalance (*env.current (), chan);
            auto const chanAmt = channelAmount (*env.current (), chan);
            auto preBob = env.balance (bob);
            auto const delta = XRP (500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = reqBal + XRP (100);
            assert (reqBal <= chanAmt);
            auto const sig =
                signClaimAuth (alice.pk (), alice.sk (), chan, authAmt);
            env (claim (bob, chan, reqBal, authAmt, Slice (sig), alice.pk ()));
            BEAST_EXPECT (channelBalance (*env.current (), chan) == reqBal);
            BEAST_EXPECT (channelAmount (*env.current (), chan) == chanAmt);
            auto const feeDrops = env.current ()->fees ().base;
            BEAST_EXPECT (env.balance (bob) == preBob + delta - feeDrops);
        }
        env.close (settleTimepoint);
        {
            // past settleTime, channel will close
            auto const chanBal = channelBalance (*env.current (), chan);
            auto const chanAmt = channelAmount (*env.current (), chan);
            auto const preAlice = env.balance (alice);
            auto preBob = env.balance (bob);
            auto const delta = XRP (500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = reqBal + XRP (100);
            assert (reqBal <= chanAmt);
            auto const sig =
                signClaimAuth (alice.pk (), alice.sk (), chan, authAmt);
            env (claim (bob, chan, reqBal, authAmt, Slice (sig), alice.pk ()));
            BEAST_EXPECT (!channelExists (*env.current (), chan));
            auto const feeDrops = env.current ()->fees ().base;
            BEAST_EXPECT (env.balance (alice) == preAlice + chanAmt - chanBal);
            BEAST_EXPECT (env.balance (bob) == preBob - feeDrops);
        }
    }

    void
    testCloseDry ()
    {
        testcase ("close dry");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env (*this);
        auto const alice = Account ("alice");
        auto const bob = Account ("bob");
        env.fund (XRP (10000), alice, bob);
        auto const pk = alice.pk ();
        auto const settleDelay = 3600s;
        auto const channelFunds = XRP (1000);
        env (create (alice, bob, channelFunds, settleDelay, pk));
        auto const chan = channel (*env.current (), alice, bob);
        BEAST_EXPECT (channelExists (*env.current (), chan));
        // Owner tries to close channel, but it will remain open (settle delay)
        env (claim (alice, chan), txflags (tfClose));
        BEAST_EXPECT (channelExists (*env.current (), chan));
        {
            // claim the entire amount
            auto const preBob = env.balance (bob);
            env (claim (
                alice, chan, channelFunds.value (), channelFunds.value ()));
            BEAST_EXPECT (channelBalance (*env.current (), chan) == channelFunds);
            BEAST_EXPECT (env.balance (bob) == preBob + channelFunds);
        }
        auto const preAlice = env.balance (alice);
        // Channel is now dry, can close before expiration date
        env (claim (alice, chan), txflags (tfClose));
        BEAST_EXPECT (!channelExists (*env.current (), chan));
        auto const feeDrops = env.current ()->fees ().base;
        BEAST_EXPECT (env.balance (alice) == preAlice - feeDrops);
    }

    void
    testDefaultAmount ()
    {
        // auth amount defaults to balance if not present
        testcase ("default amount");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env (*this);
        auto const alice = Account ("alice");
        auto const bob = Account ("bob");
        env.fund (XRP (10000), alice, bob);
        auto const pk = alice.pk ();
        auto const settleDelay = 3600s;
        auto const channelFunds = XRP (1000);
        env (create (alice, bob, channelFunds, settleDelay, pk));
        auto const chan = channel (*env.current (), alice, bob);
        BEAST_EXPECT (channelExists (*env.current (), chan));
        // Owner tries to close channel, but it will remain open (settle delay)
        env (claim (alice, chan), txflags (tfClose));
        BEAST_EXPECT (channelExists (*env.current (), chan));
        {
            auto chanBal = channelBalance (*env.current (), chan);
            auto chanAmt = channelAmount (*env.current (), chan);
            auto const preBob = env.balance (bob);

            auto const delta = XRP (500);
            auto const reqBal = chanBal + delta;
            assert (reqBal <= chanAmt);
            auto const sig =
                signClaimAuth (alice.pk (), alice.sk (), chan, reqBal);
            env (claim (
                bob, chan, reqBal, boost::none, Slice (sig), alice.pk ()));
            BEAST_EXPECT (channelBalance (*env.current (), chan) == reqBal);
            auto const feeDrops = env.current ()->fees ().base;
            BEAST_EXPECT (env.balance (bob) == preBob + delta - feeDrops);
            chanBal = reqBal;
        }
        {
            // Claim again
            auto chanBal = channelBalance (*env.current (), chan);
            auto chanAmt = channelAmount (*env.current (), chan);
            auto const preBob = env.balance (bob);

            auto const delta = XRP (500);
            auto const reqBal = chanBal + delta;
            assert (reqBal <= chanAmt);
            auto const sig =
                signClaimAuth (alice.pk (), alice.sk (), chan, reqBal);
            env (claim (
                bob, chan, reqBal, boost::none, Slice (sig), alice.pk ()));
            BEAST_EXPECT (channelBalance (*env.current (), chan) == reqBal);
            auto const feeDrops = env.current ()->fees ().base;
            BEAST_EXPECT (env.balance (bob) == preBob + delta - feeDrops);
            chanBal = reqBal;
        }
    }

    void
    testDisallowXRP ()
    {
        // auth amount defaults to balance if not present
        testcase ("Disallow XRP");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account ("alice");
        auto const bob = Account ("bob");
        {
            // Create a channel where dst disallows XRP
            Env env (*this, supported_amendments() - featureDepositAuth);
            env.fund (XRP (10000), alice, bob);
            env (fset (bob, asfDisallowXRP));
            env (create (alice, bob, XRP (1000), 3600s, alice.pk()),
                ter (tecNO_TARGET));
            auto const chan = channel (*env.current (), alice, bob);
            BEAST_EXPECT (!channelExists (*env.current (), chan));
        }
        {
            // Create a channel where dst disallows XRP.  Ignore that flag,
            // since it's just advisory.
            Env env (*this);
            env.fund (XRP (10000), alice, bob);
            env (fset (bob, asfDisallowXRP));
            env (create (alice, bob, XRP (1000), 3600s, alice.pk()));
            auto const chan = channel (*env.current (), alice, bob);
            BEAST_EXPECT (channelExists (*env.current (), chan));
        }

        {
            // Claim to a channel where dst disallows XRP
            // (channel is created before disallow xrp is set)
            Env env (*this, supported_amendments() - featureDepositAuth);
            env.fund (XRP (10000), alice, bob);
            env (create (alice, bob, XRP (1000), 3600s, alice.pk()));
            auto const chan = channel (*env.current (), alice, bob);
            BEAST_EXPECT (channelExists (*env.current (), chan));

            env (fset (bob, asfDisallowXRP));
            auto const reqBal = XRP (500).value();
            env (claim (alice, chan, reqBal, reqBal), ter(tecNO_TARGET));
        }
        {
            // Claim to a channel where dst disallows XRP (channel is
            // created before disallow xrp is set).  Ignore that flag
            // since it is just advisory.
            Env env (*this);
            env.fund (XRP (10000), alice, bob);
            env (create (alice, bob, XRP (1000), 3600s, alice.pk()));
            auto const chan = channel (*env.current (), alice, bob);
            BEAST_EXPECT (channelExists (*env.current (), chan));

            env (fset (bob, asfDisallowXRP));
            auto const reqBal = XRP (500).value();
            env (claim (alice, chan, reqBal, reqBal));
        }
    }

    void
    testDstTag ()
    {
        // auth amount defaults to balance if not present
        testcase ("Dst Tag");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        // Create a channel where dst disallows XRP
        Env env (*this);
        auto const alice = Account ("alice");
        auto const bob = Account ("bob");
        env.fund (XRP (10000), alice, bob);
        env (fset (bob, asfRequireDest));
        auto const pk = alice.pk ();
        auto const settleDelay = 3600s;
        auto const channelFunds = XRP (1000);
        env (create (alice, bob, channelFunds, settleDelay, pk),
            ter (tecDST_TAG_NEEDED));
        BEAST_EXPECT (!channelExists (
            *env.current (), channel (*env.current (), alice, bob)));
        env (
            create (alice, bob, channelFunds, settleDelay, pk, boost::none, 1));
        BEAST_EXPECT (channelExists (
            *env.current (), channel (*env.current (), alice, bob)));
    }

    void
    testDepositAuth ()
    {
        testcase ("Deposit Authorization");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account ("alice");
        auto const bob = Account ("bob");
        auto USDA = alice["USD"];
        {
            Env env (*this);
            env.fund (XRP (10000), alice, bob);

            env (fset (bob, asfDepositAuth));
            env.close();

            auto const pk = alice.pk ();
            auto const settleDelay = 100s;
            env (create (alice, bob, XRP (1000), settleDelay, pk));
            env.close();

            auto const chan = channel (*env.current (), alice, bob);
            BEAST_EXPECT (channelBalance (*env.current (), chan) == XRP (0));
            BEAST_EXPECT (channelAmount (*env.current (), chan) == XRP (1000));

            // alice can add more funds to the channel even though bob has
            // asfDepositAuth set.
            env (fund (alice, chan, XRP (1000)));
            env.close();

            // alice claims. Fails because bob's lsfDepositAuth flag is set.
            env (claim (alice, chan,
                XRP (500).value(), XRP (500).value()), ter (tecNO_PERMISSION));
            env.close();

            // Claim with signature
            auto const baseFee = env.current()->fees().base;
            auto const preBob = env.balance (bob);
            {
                auto const delta = XRP (500).value();
                auto const sig = signClaimAuth (pk, alice.sk (), chan, delta);

                // alice claims with signature.  Fails since bob has
                // lsfDepositAuth flag set.
                env (claim (alice, chan,
                    delta, delta, Slice (sig), pk), ter (tecNO_PERMISSION));
                env.close();
                BEAST_EXPECT (env.balance (bob) == preBob);

                // bob claims with signature.  Succeeds even though bob's
                // lsfDepositAuth flag is set since bob signed the transaction.
                env (claim (bob, chan, delta, delta, Slice (sig), pk));
                env.close();
                BEAST_EXPECT (env.balance (bob) == preBob + delta - baseFee);
            }

            // bob clears lsfDepositAuth.  Now alice can use an unsigned claim.
            env (fclear (bob, asfDepositAuth));
            env.close();

            // alice claims successfully.
            env (claim (alice, chan, XRP (800).value(), XRP (800).value()));
            env.close();
            BEAST_EXPECT (
                env.balance (bob) == preBob + XRP (800) - (2 * baseFee));
        }
    }

    void
    testMultiple ()
    {
        // auth amount defaults to balance if not present
        testcase ("Multiple channels to the same account");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env (*this);
        auto const alice = Account ("alice");
        auto const bob = Account ("bob");
        env.fund (XRP (10000), alice, bob);
        auto const pk = alice.pk ();
        auto const settleDelay = 3600s;
        auto const channelFunds = XRP (1000);
        env (create (alice, bob, channelFunds, settleDelay, pk));
        auto const chan1 = channel (*env.current (), alice, bob);
        BEAST_EXPECT (channelExists (*env.current (), chan1));
        env (create (alice, bob, channelFunds, settleDelay, pk));
        auto const chan2 = channel (*env.current (), alice, bob);
        BEAST_EXPECT (channelExists (*env.current (), chan2));
        BEAST_EXPECT (chan1 != chan2);
    }

    void
    testRPC ()
    {
        testcase ("RPC");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env (*this);
        auto const alice = Account ("alice");
        auto const bob = Account ("bob");
        env.fund (XRP (10000), alice, bob);
        auto const pk = alice.pk ();
        auto const settleDelay = 3600s;
        auto const channelFunds = XRP (1000);
        env (create (alice, bob, channelFunds, settleDelay, pk));
        env.close();
        auto const chan1Str = to_string (channel (*env.current (), alice, bob));
        std::string chan1PkStr;
        {
            auto const r =
                env.rpc ("account_channels", alice.human (), bob.human ());
            BEAST_EXPECT (r[jss::result][jss::channels].size () == 1);
            BEAST_EXPECT (r[jss::result][jss::channels][0u][jss::channel_id] == chan1Str);
            BEAST_EXPECT (r[jss::result][jss::validated]);
            chan1PkStr = r[jss::result][jss::channels][0u][jss::public_key].asString();
        }
        {
            auto const r =
                env.rpc ("account_channels", alice.human ());
            BEAST_EXPECT (r[jss::result][jss::channels].size () == 1);
            BEAST_EXPECT (r[jss::result][jss::channels][0u][jss::channel_id] == chan1Str);
            BEAST_EXPECT (r[jss::result][jss::validated]);
            chan1PkStr = r[jss::result][jss::channels][0u][jss::public_key].asString();
        }
        {
            auto const r =
                env.rpc ("account_channels", bob.human (), alice.human ());
            BEAST_EXPECT (r[jss::result][jss::channels].size () == 0);
            BEAST_EXPECT (r[jss::result][jss::validated]);
        }
        env (create (alice, bob, channelFunds, settleDelay, pk));
        env.close();
        auto const chan2Str = to_string (channel (*env.current (), alice, bob));
        {
            auto const r =
                    env.rpc ("account_channels", alice.human (), bob.human ());
            BEAST_EXPECT (r[jss::result][jss::channels].size () == 2);
            BEAST_EXPECT (r[jss::result][jss::validated]);
            BEAST_EXPECT (chan1Str != chan2Str);
            for (auto const& c : {chan1Str, chan2Str})
                BEAST_EXPECT (r[jss::result][jss::channels][0u][jss::channel_id] == c ||
                        r[jss::result][jss::channels][1u][jss::channel_id] == c);
        }

        auto sliceToHex = [](Slice const& slice) {
            std::string s;
            s.reserve(2 * slice.size());
            for (int i = 0; i < slice.size(); ++i)
            {
                s += "0123456789ABCDEF"[((slice[i] & 0xf0) >> 4)];
                s += "0123456789ABCDEF"[((slice[i] & 0x0f) >> 0)];
            }
            return s;
        };

        {
            // Verify chan1 auth
            auto const rs =
                env.rpc ("channel_authorize", "alice", chan1Str, "1000");
            auto const sig = rs[jss::result][jss::signature].asString ();
            BEAST_EXPECT (!sig.empty ());
            {
                auto const rv = env.rpc(
                    "channel_verify", chan1PkStr, chan1Str, "1000", sig);
                BEAST_EXPECT(rv[jss::result][jss::signature_verified].asBool());
            }

            {
                // use pk hex to verify
                auto const pkAsHex = sliceToHex(pk.slice());
                auto const rv = env.rpc (
                    "channel_verify", pkAsHex, chan1Str, "1000", sig);
                BEAST_EXPECT (rv[jss::result][jss::signature_verified].asBool ());
            }
            {
                // malformed amount
                auto const pkAsHex = sliceToHex(pk.slice());
                auto rv =
                    env.rpc("channel_verify", pkAsHex, chan1Str, "1000x", sig);
                BEAST_EXPECT(rv[jss::error] == "channelAmtMalformed");
                rv = env.rpc("channel_verify", pkAsHex, chan1Str, "1000 ", sig);
                BEAST_EXPECT(rv[jss::error] == "channelAmtMalformed");
                rv = env.rpc("channel_verify", pkAsHex, chan1Str, "x1000", sig);
                BEAST_EXPECT(rv[jss::error] == "channelAmtMalformed");
                rv = env.rpc("channel_verify", pkAsHex, chan1Str, "x", sig);
                BEAST_EXPECT(rv[jss::error] == "channelAmtMalformed");
                rv = env.rpc("channel_verify", pkAsHex, chan1Str, " ", sig);
                BEAST_EXPECT(rv[jss::error] == "channelAmtMalformed");
                rv = env.rpc("channel_verify", pkAsHex, chan1Str, "1000 1000", sig);
                BEAST_EXPECT(rv[jss::error] == "channelAmtMalformed");
                rv = env.rpc("channel_verify", pkAsHex, chan1Str, "1,000", sig);
                BEAST_EXPECT(rv[jss::error] == "channelAmtMalformed");
                rv = env.rpc("channel_verify", pkAsHex, chan1Str, " 1000", sig);
                BEAST_EXPECT(rv[jss::error] == "channelAmtMalformed");
                rv = env.rpc("channel_verify", pkAsHex, chan1Str, "", sig);
                BEAST_EXPECT(rv[jss::error] == "channelAmtMalformed");
            }
            {
                // malformed channel
                auto const pkAsHex = sliceToHex(pk.slice());
                auto chan1StrBad = chan1Str;
                chan1StrBad.pop_back();
                auto rv = env.rpc("channel_verify", pkAsHex, chan1StrBad, "1000", sig);
                BEAST_EXPECT(rv[jss::error] == "channelMalformed");
                rv = env.rpc ("channel_authorize", "alice", chan1StrBad, "1000");
                BEAST_EXPECT(rv[jss::error] == "channelMalformed");

                chan1StrBad = chan1Str;
                chan1StrBad.push_back('0');
                rv = env.rpc("channel_verify", pkAsHex, chan1StrBad, "1000", sig);
                BEAST_EXPECT(rv[jss::error] == "channelMalformed");
                rv = env.rpc ("channel_authorize", "alice", chan1StrBad, "1000");
                BEAST_EXPECT(rv[jss::error] == "channelMalformed");

                chan1StrBad = chan1Str;
                chan1StrBad.back() = 'x';
                rv = env.rpc("channel_verify", pkAsHex, chan1StrBad, "1000", sig);
                BEAST_EXPECT(rv[jss::error] == "channelMalformed");
                rv = env.rpc ("channel_authorize", "alice", chan1StrBad, "1000");
                BEAST_EXPECT(rv[jss::error] == "channelMalformed");
            }
            {
                // give an ill formed base 58 public key
                auto illFormedPk = chan1PkStr.substr(0, chan1PkStr.size() - 1);
                auto const rv = env.rpc(
                    "channel_verify", illFormedPk, chan1Str, "1000", sig);
                BEAST_EXPECT(!rv[jss::result][jss::signature_verified].asBool());
            }
            {
                // give an ill formed hex public key
                auto const pkAsHex = sliceToHex(pk.slice());
                auto illFormedPk = pkAsHex.substr(0, chan1PkStr.size() - 1);
                auto const rv = env.rpc(
                    "channel_verify", illFormedPk, chan1Str, "1000", sig);
                BEAST_EXPECT(!rv[jss::result][jss::signature_verified].asBool());
            }
        }
        {
            // Try to verify chan2 auth with chan1 key
            auto const rs =
                env.rpc ("channel_authorize", "alice", chan2Str, "1000");
            auto const sig = rs[jss::result][jss::signature].asString ();
            BEAST_EXPECT (!sig.empty ());
            {
                auto const rv = env.rpc(
                    "channel_verify", chan1PkStr, chan1Str, "1000", sig);
                BEAST_EXPECT(
                    !rv[jss::result][jss::signature_verified].asBool());
            }
            {
                // use pk hex to verify
                auto const pkAsHex = sliceToHex(pk.slice());
                auto const rv = env.rpc(
                    "channel_verify", pkAsHex, chan1Str, "1000", sig);
                BEAST_EXPECT(
                    !rv[jss::result][jss::signature_verified].asBool());
            }
        }
        {
            // send malformed amounts rpc requests
            auto rs = env.rpc("channel_authorize", "alice", chan1Str, "1000x");
            BEAST_EXPECT(rs[jss::error] == "channelAmtMalformed");
            rs = env.rpc("channel_authorize", "alice", chan1Str, "x1000");
            BEAST_EXPECT(rs[jss::error] == "channelAmtMalformed");
            rs = env.rpc("channel_authorize", "alice", chan1Str, "x");
            BEAST_EXPECT(rs[jss::error] == "channelAmtMalformed");
        }
    }

    void
    testOptionalFields ()
    {
        testcase ("Optional Fields");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env (*this);
        auto const alice = Account ("alice");
        auto const bob = Account ("bob");
        auto const carol = Account ("carol");
        auto const dan = Account ("dan");
        env.fund (XRP (10000), alice, bob, carol, dan);
        auto const pk = alice.pk ();
        auto const settleDelay = 3600s;
        auto const channelFunds = XRP (1000);

        boost::optional<NetClock::time_point> cancelAfter;

        {
            env (create (alice, bob, channelFunds, settleDelay, pk));
            auto const chan = to_string (channel (*env.current (), alice, bob));
            auto const r =
                env.rpc ("account_channels", alice.human (), bob.human ());
            BEAST_EXPECT (r[jss::result][jss::channels].size () == 1);
            BEAST_EXPECT (r[jss::result][jss::channels][0u][jss::channel_id] == chan);
            BEAST_EXPECT (!r[jss::result][jss::channels][0u].isMember(jss::destination_tag));
        }
        {
            std::uint32_t dstTag=42;
            env (create (
                alice, carol, channelFunds, settleDelay, pk, cancelAfter, dstTag));
            auto const chan = to_string (channel (*env.current (), alice, carol));
            auto const r =
                env.rpc ("account_channels", alice.human (), carol.human ());
            BEAST_EXPECT (r[jss::result][jss::channels].size () == 1);
            BEAST_EXPECT (r[jss::result][jss::channels][0u][jss::channel_id] == chan);
            BEAST_EXPECT (r[jss::result][jss::channels][0u][jss::destination_tag] == dstTag);
        }
    }

    void
    testMalformedPK ()
    {
        testcase ("malformed pk");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env (*this);
        auto const alice = Account ("alice");
        auto const bob = Account ("bob");
        auto USDA = alice["USD"];
        env.fund (XRP (10000), alice, bob);
        auto const pk = alice.pk ();
        auto const settleDelay = 100s;

        auto jv = create (alice, bob, XRP (1000), settleDelay, pk);
        auto const pkHex = strHex (pk.slice ());
        jv["PublicKey"] = pkHex.substr(2, pkHex.size()-2);
        env (jv, ter(temMALFORMED));
        jv["PublicKey"] = pkHex.substr(0, pkHex.size()-2);
        env (jv, ter(temMALFORMED));
        auto badPrefix = pkHex;
        badPrefix[0]='f';
        badPrefix[1]='f';
        jv["PublicKey"] = badPrefix;
        env (jv, ter(temMALFORMED));

        jv["PublicKey"] = pkHex;
        env (jv);
        auto const chan = channel (*env.current (), alice, bob);

        auto const authAmt = XRP (100);
        auto const sig = signClaimAuth (alice.pk (), alice.sk (), chan, authAmt);
        jv = claim(bob, chan, authAmt.value(), authAmt.value(), Slice(sig), alice.pk());
        jv["PublicKey"] = pkHex.substr(2, pkHex.size()-2);
        env (jv, ter(temMALFORMED));
        jv["PublicKey"] = pkHex.substr(0, pkHex.size()-2);
        env (jv, ter(temMALFORMED));
        badPrefix = pkHex;
        badPrefix[0]='f';
        badPrefix[1]='f';
        jv["PublicKey"] = badPrefix;
        env (jv, ter(temMALFORMED));

        // missing public key
        jv.removeMember("PublicKey");
        env (jv, ter(temMALFORMED));

        {
            auto const txn = R"*(
        {

        "channel_id":"5DB01B7FFED6B67E6B0414DED11E051D2EE2B7619CE0EAA6286D67A3A4D5BDB3",
                "signature":
        "304402204EF0AFB78AC23ED1C472E74F4299C0C21F1B21D07EFC0A3838A420F76D783A400220154FB11B6F54320666E4C36CA7F686C16A3A0456800BBC43746F34AF50290064",
                "public_key":
        "aKijDDiC2q2gXjMpM7i4BUS6cmixgsEe18e7CjsUxwihKfuoFgS5",
                "amount": "1000000"
            }
        )*";
            auto const r = env.rpc("json", "channel_verify", txn);
            BEAST_EXPECT(r["result"]["error"] == "publicMalformed");
        }
    }

    void
    run () override
    {
        testSimple ();
        testCancelAfter ();
        testSettleDelay ();
        testExpiration ();
        testCloseDry ();
        testDefaultAmount ();
        testDisallowXRP ();
        testDstTag ();
        testDepositAuth ();
        testMultiple ();
        testRPC ();
        testOptionalFields ();
        testMalformedPK ();
    }
};

BEAST_DEFINE_TESTSUITE (PayChan, app, ripple);
}  // test
}  // ripple

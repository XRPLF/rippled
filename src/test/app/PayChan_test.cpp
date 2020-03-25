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

#include <ripple/basics/chrono.h>
#include <ripple/ledger/Directory.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/jss.h>
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


    static
    std::pair<uint256, std::shared_ptr<SLE const>>
    channelKeyAndSle(ReadView const& view,
               jtx::Account const& account,
               jtx::Account const& dst)
    {
        auto const sle = view.read (keylet::account (account));
        if (!sle)
            return {};
        auto const k = keylet::payChan (account, dst, (*sle)[sfSequence] - 1);
        return {k.key, view.read(k)};
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
        jv[jss::TransactionType] = jss::PaymentChannelCreate;
        jv[jss::Flags] = tfUniversal;
        jv[jss::Account] = account.human ();
        jv[jss::Destination] = to.human ();
        jv[jss::Amount] = amount.getJson (JsonOptions::none);
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
        jv[jss::TransactionType] = jss::PaymentChannelFund;
        jv[jss::Flags] = tfUniversal;
        jv[jss::Account] = account.human ();
        jv["Channel"] = to_string (channel);
        jv[jss::Amount] = amount.getJson (JsonOptions::none);
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
        jv[jss::TransactionType] = jss::PaymentChannelClaim;
        jv[jss::Flags] = tfUniversal;
        jv[jss::Account] = account.human ();
        jv["Channel"] = to_string (channel);
        if (amount)
            jv[jss::Amount] = amount->getJson (JsonOptions::none);
        if (balance)
            jv["Balance"] = balance->getJson (JsonOptions::none);
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
            STAmount const reqAmt = authAmt + STAmount{1};
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
            Env env (*this,  supported_amendments());
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
            Env env (*this, supported_amendments());
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
        auto const carol = Account ("carol");
        auto USDA = alice["USD"];
        {
            Env env (*this);
            env.fund (XRP (10000), alice, bob, carol);

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

                // bob claims but omits the signature.  Fails because only
                // alice can claim without a signature.
                env (claim (bob, chan, delta, delta), ter (temBAD_SIGNATURE));
                env.close();

                // bob claims with signature.  Succeeds even though bob's
                // lsfDepositAuth flag is set since bob submitted the
                // transaction.
                env (claim (bob, chan, delta, delta, Slice (sig), pk));
                env.close();
                BEAST_EXPECT (env.balance (bob) == preBob + delta - baseFee);
            }
            {
                // Explore the limits of deposit preauthorization.
                auto const delta = XRP (600).value();
                auto const sig = signClaimAuth (pk, alice.sk (), chan, delta);

                // carol claims and fails.  Only channel participants (bob or
                // alice) may claim.
                env (claim (carol, chan,
                    delta, delta, Slice (sig), pk), ter (tecNO_PERMISSION));
                env.close();

                // bob preauthorizes carol for deposit.  But after that carol
                // still can't claim since only channel participants may claim.
                env(deposit::auth (bob, carol));
                env.close();

                env (claim (carol, chan,
                    delta, delta, Slice (sig), pk), ter (tecNO_PERMISSION));

                // Since alice is not preauthorized she also may not claim
                // for bob.
                env (claim (alice, chan, delta, delta,
                    Slice (sig), pk), ter (tecNO_PERMISSION));
                env.close();

                // However if bob preauthorizes alice for deposit then she can
                // successfully submit a claim.
                env(deposit::auth (bob, alice));
                env.close();

                env (claim (alice, chan, delta, delta, Slice (sig), pk));
                env.close();

                BEAST_EXPECT (
                    env.balance (bob) == preBob + delta - (3 * baseFee));
            }
            {
                // bob removes preauthorization of alice.  Once again she
                // cannot submit a claim.
                auto const delta = XRP (800).value();

                env(deposit::unauth (bob, alice));
                env.close();

                // alice claims and fails since she is no longer preauthorized.
                env (claim (alice, chan, delta, delta), ter (tecNO_PERMISSION));
                env.close();

                // bob clears lsfDepositAuth.  Now alice can claim.
                env (fclear (bob, asfDepositAuth));
                env.close();

                // alice claims successfully.
                env (claim (alice, chan, delta, delta));
                env.close();
                BEAST_EXPECT (
                    env.balance (bob) == preBob + XRP (800) - (5 * baseFee));
            }
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
    testAccountChannelsRPC()
    {
        testcase("AccountChannels RPC");

        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env(*this);
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const charlie = Account("charlie", KeyType::ed25519);
        env.fund(XRP(10000), alice, bob, charlie);
        auto const pk = alice.pk();
        auto const settleDelay = 3600s;
        auto const channelFunds = XRP(1000);
        env(create(alice, bob, channelFunds, settleDelay, pk));
        env.close();
        auto const chan1Str = to_string(channel(*env.current(), alice, bob));
        {
            auto const r =
                env.rpc("account_channels", alice.human(), bob.human());
            BEAST_EXPECT(r[jss::result][jss::channels].size() == 1);
            BEAST_EXPECT(
                r[jss::result][jss::channels][0u][jss::channel_id] == chan1Str);
            BEAST_EXPECT(r[jss::result][jss::validated]);
        }
        {
            auto const r = env.rpc("account_channels", alice.human());
            BEAST_EXPECT(r[jss::result][jss::channels].size() == 1);
            BEAST_EXPECT(
                r[jss::result][jss::channels][0u][jss::channel_id] == chan1Str);
            BEAST_EXPECT(r[jss::result][jss::validated]);
        }
        {
            auto const r =
                env.rpc("account_channels", bob.human(), alice.human());
            BEAST_EXPECT(r[jss::result][jss::channels].size() == 0);
            BEAST_EXPECT(r[jss::result][jss::validated]);
        }
        env(create(alice, bob, channelFunds, settleDelay, pk));
        env.close();
        auto const chan2Str = to_string(channel(*env.current(), alice, bob));
        {
            auto const r =
                env.rpc("account_channels", alice.human(), bob.human());
            BEAST_EXPECT(r[jss::result][jss::channels].size() == 2);
            BEAST_EXPECT(r[jss::result][jss::validated]);
            BEAST_EXPECT(chan1Str != chan2Str);
            for (auto const& c : {chan1Str, chan2Str})
                BEAST_EXPECT(
                    r[jss::result][jss::channels][0u][jss::channel_id] == c ||
                    r[jss::result][jss::channels][1u][jss::channel_id] == c);
        }
    }

    void
    testAccountChannelsRPCMarkers()
    {
        testcase("Account channels RPC markers");

        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bobs = []() -> std::vector<Account> {
            int const n = 10;
            std::vector<Account> r;
            r.reserve(n);
            for (int i = 0; i < n; ++i)
            {
                r.emplace_back("bob"s + std::to_string(i));
            }
            return r;
        }();

        Env env(*this);
        env.fund(XRP(10000), alice);
        for (auto const& a : bobs)
        {
            env.fund(XRP(10000), a);
            env.close();
        }

        {
            // create a channel from alice to every bob account
            auto const settleDelay = 3600s;
            auto const channelFunds = XRP(1);
            for (auto const& b : bobs)
            {
                env(create(alice, b, channelFunds, settleDelay, alice.pk()));
            }
        }

        auto testLimit = [](test::jtx::Env& env,
                            test::jtx::Account const& src,
                            boost::optional<int> limit = boost::none,
                            Json::Value const& marker = Json::nullValue,
                            boost::optional<test::jtx::Account> const& dst =
                                boost::none) {
            Json::Value jvc;
            jvc[jss::account] = src.human();
            if (dst)
                jvc[jss::destination_account] = dst->human();
            if (limit)
                jvc[jss::limit] = *limit;
            if (marker)
                jvc[jss::marker] = marker;

            return env.rpc(
                "json", "account_channels", to_string(jvc))[jss::result];
        };

        {
            // No marker
            auto const r = testLimit(env, alice);
            BEAST_EXPECT(r.isMember(jss::channels));
            BEAST_EXPECT(r[jss::channels].size() == bobs.size());
        }

        auto const bobsB58 = [&bobs]() -> std::set<std::string> {
            std::set<std::string> r;
            for (auto const& a : bobs)
                r.insert(a.human());
            return r;
        }();

        for (int limit = 1; limit < bobs.size() + 1; ++limit)
        {
            auto leftToFind = bobsB58;
            auto const numFull = bobs.size() / limit;
            auto const numNonFull = bobs.size() % limit ? 1 : 0;

            Json::Value marker = Json::nullValue;

            auto const testIt = [&](bool expectMarker, int expectedBatchSize) {
                auto const r = testLimit(env, alice, limit, marker);
                BEAST_EXPECT(!expectMarker || r.isMember(jss::marker));
                if (r.isMember(jss::marker))
                    marker = r[jss::marker];
                BEAST_EXPECT(r[jss::channels].size() == expectedBatchSize);
                auto const c = r[jss::channels];
                auto const s = r[jss::channels].size();
                for (int j = 0; j < s; ++j)
                {
                    auto const dstAcc =
                        c[j][jss::destination_account].asString();
                    BEAST_EXPECT(leftToFind.count(dstAcc));
                    leftToFind.erase(dstAcc);
                }
            };

            for (int i = 0; i < numFull; ++i)
            {
                bool const expectMarker = (numNonFull != 0 || i < numFull - 1);
                testIt(expectMarker, limit);
            }

            if (numNonFull)
            {
                testIt(false, bobs.size() % limit);
            }
            BEAST_EXPECT(leftToFind.empty());
        }

        {
            // degenerate case
            auto const r = testLimit(env, alice, 0);
            BEAST_EXPECT(r.isMember(jss::marker));
            BEAST_EXPECT(r[jss::channels].size() == 0);
        }
    }

    void
    testAccountChannelsRPCSenderOnly()
    {
        // Check that the account_channels command only returns channels owned by the account
        testcase("Account channels RPC owner only");

        using namespace test::jtx;
        using namespace std::literals;


        auto const alice = Account("alice");
        auto const bob = Account("bob");
        Env env(*this);
        env.fund(XRP(10000), alice, bob);

        // Create a channel from alice to bob and from bob to alice
        // When retrieving alice's channels, it should only retrieve the
        // channels where alice is the source, not the destination
        auto const settleDelay = 3600s;
        auto const channelFunds = XRP (1000);
        env(create(alice, bob, channelFunds, settleDelay, alice.pk()));
        env(create(bob, alice, channelFunds, settleDelay, bob.pk()));

        auto const r = [&]{
            Json::Value jvc;
            jvc[jss::account] = alice.human();

            return env.rpc(
                "json", "account_channels", to_string(jvc))[jss::result];
        }();
        BEAST_EXPECT(r.isMember(jss::channels));
        BEAST_EXPECT(r[jss::channels].size() == 1);
        BEAST_EXPECT(r[jss::channels][0u][jss::destination_account].asString() == bob.human());
    }

    void
    testAuthVerifyRPC ()
    {
        testcase ("PayChan Auth/Verify RPC");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env (*this);
        auto const alice = Account ("alice");
        auto const bob = Account ("bob");
        auto const charlie = Account("charlie", KeyType::ed25519);
        env.fund (XRP (10000), alice, bob, charlie);
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
            // Try to explicitly specify secp256k1 and Ed25519 keys:
            env (create (charlie, alice, channelFunds, settleDelay, charlie.pk()));
            env.close();

            auto const chan = to_string (channel (*env.current (), charlie, alice));

            std::string cpk;
            {
                auto const r =
                    env.rpc ("account_channels", charlie.human (), alice.human ());
                BEAST_EXPECT (r[jss::result][jss::channels].size () == 1);
                BEAST_EXPECT (r[jss::result][jss::channels][0u][jss::channel_id] == chan);
                BEAST_EXPECT (r[jss::result][jss::validated]);
                cpk = r[jss::result][jss::channels][0u][jss::public_key].asString();
            }

            // Try to authorize without specifying a key type, expect an error:
            auto const rs =
                env.rpc ("channel_authorize", "charlie", chan, "1000");
            auto const sig = rs[jss::result][jss::signature].asString ();
            BEAST_EXPECT (!sig.empty ());
            {
                auto const rv = env.rpc(
                    "channel_verify", cpk, chan, "1000", sig);
                BEAST_EXPECT(!rv[jss::result][jss::signature_verified].asBool());
            }

            // Try to authorize using an unknown key type, except an error:
            auto const rs1 =
                env.rpc ("channel_authorize", "charlie", "nyx", chan, "1000");
            BEAST_EXPECT(rs1[jss::error] == "badKeyType");

            // Try to authorize using secp256k1; the authorization _should_
            // succeed but the verification should fail:
            auto const rs2 =
                env.rpc ("channel_authorize", "charlie", "secp256k1", chan, "1000");
            auto const sig2 = rs2[jss::result][jss::signature].asString ();
            BEAST_EXPECT (!sig2.empty ());
            {
                auto const rv = env.rpc(
                    "channel_verify", cpk, chan, "1000", sig2);
                BEAST_EXPECT(!rv[jss::result][jss::signature_verified].asBool());
            }

            // Try to authorize using Ed25519; expect success:
            auto const rs3 =
                env.rpc ("channel_authorize", "charlie", "ed25519", chan, "1000");
            auto const sig3 = rs3[jss::result][jss::signature].asString ();
            BEAST_EXPECT (!sig3.empty ());
            {
                auto const rv = env.rpc(
                    "channel_verify", cpk, chan, "1000", sig3);
                BEAST_EXPECT(rv[jss::result][jss::signature_verified].asBool());
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
            {
                // Missing channel_id
                Json::Value args {Json::objectValue};
                args[jss::amount] = "2000";
                args[jss::key_type] = "secp256k1";
                args[jss::passphrase] = "passphrase_can_be_anything";
                rs = env.rpc ("json",
                    "channel_authorize", args.toStyledString())[jss::result];
                BEAST_EXPECT(rs[jss::error] == "invalidParams");
            }
            {
                // Missing amount
                Json::Value args {Json::objectValue};
                args[jss::channel_id] = chan1Str;
                args[jss::key_type] = "secp256k1";
                args[jss::passphrase] = "passphrase_can_be_anything";
                rs = env.rpc ("json",
                    "channel_authorize", args.toStyledString())[jss::result];
                BEAST_EXPECT(rs[jss::error] == "invalidParams");
            }
            {
                // Missing key_type and no secret.
                Json::Value args {Json::objectValue};
                args[jss::amount] = "2000";
                args[jss::channel_id] = chan1Str;
                args[jss::passphrase] = "passphrase_can_be_anything";
                rs = env.rpc ("json",
                    "channel_authorize", args.toStyledString())[jss::result];
                BEAST_EXPECT(rs[jss::error] == "invalidParams");
            }
            {
                // Both passphrase and seed specified.
                Json::Value args {Json::objectValue};
                args[jss::amount] = "2000";
                args[jss::channel_id] = chan1Str;
                args[jss::key_type] = "secp256k1";
                args[jss::passphrase] = "passphrase_can_be_anything";
                args[jss::seed] = "seed can be anything";
                rs = env.rpc ("json",
                    "channel_authorize", args.toStyledString())[jss::result];
                BEAST_EXPECT(rs[jss::error] == "invalidParams");
            }
            {
                // channel_id is not exact hex.
                Json::Value args {Json::objectValue};
                args[jss::amount] = "2000";
                args[jss::channel_id] = chan1Str + "1";
                args[jss::key_type] = "secp256k1";
                args[jss::passphrase] = "passphrase_can_be_anything";
                rs = env.rpc ("json",
                    "channel_authorize", args.toStyledString())[jss::result];
                BEAST_EXPECT(rs[jss::error] == "channelMalformed");
            }
            {
                //amount is not a string
                Json::Value args {Json::objectValue};
                args[jss::amount] = 2000;
                args[jss::channel_id] = chan1Str;
                args[jss::key_type] = "secp256k1";
                args[jss::passphrase] = "passphrase_can_be_anything";
                rs = env.rpc ("json",
                    "channel_authorize", args.toStyledString())[jss::result];
                BEAST_EXPECT(rs[jss::error] == "channelAmtMalformed");
            }
            {
                // Amount is not a decimal string.
                Json::Value args {Json::objectValue};
                args[jss::amount] = "TwoThousand";
                args[jss::channel_id] = chan1Str;
                args[jss::key_type] = "secp256k1";
                args[jss::passphrase] = "passphrase_can_be_anything";
                rs = env.rpc ("json",
                    "channel_authorize", args.toStyledString())[jss::result];
                BEAST_EXPECT(rs[jss::error] == "channelAmtMalformed");
            }
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
    testMetaAndOwnership()
    {
        testcase("Metadata & Ownership");

        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const settleDelay = 100s;
        auto const pk = alice.pk();

        auto inOwnerDir = [](ReadView const& view,
                             Account const& acc,
                             std::shared_ptr<SLE const> const& chan) -> bool {
            ripple::Dir const ownerDir(view, keylet::ownerDir(acc.id()));
            return std::find(ownerDir.begin(), ownerDir.end(), chan) != ownerDir.end();
        };

        auto ownerDirCount = [](ReadView const& view,
                                Account const& acc) -> std::size_t
        {
            ripple::Dir const ownerDir(view, keylet::ownerDir(acc.id()));
            return std::distance(ownerDir.begin(), ownerDir.end());
        };

        {
            // Test without adding the paychan to the recipient's owner
            // directory
            Env env(
                *this, supported_amendments() - fixPayChanRecipientOwnerDir);
            env.fund (XRP (10000), alice, bob);
            env(create(alice, bob, XRP(1000), settleDelay, pk));
            env.close();
            auto const [chan, chanSle] = channelKeyAndSle(*env.current(), alice, bob);
            BEAST_EXPECT(inOwnerDir(*env.current(), alice, chanSle));
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 1);
            BEAST_EXPECT(!inOwnerDir(*env.current(), bob, chanSle));
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 0);
            // close the channel
            env(claim(bob, chan), txflags(tfClose));
            BEAST_EXPECT(!channelExists(*env.current(), chan));
            BEAST_EXPECT(!inOwnerDir(*env.current(), alice, chanSle));
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 0);
            BEAST_EXPECT(!inOwnerDir(*env.current(), bob, chanSle));
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 0);
        }

        {
            // Test with adding the paychan to the recipient's owner directory
            Env env(*this, supported_amendments());
            env.fund (XRP (10000), alice, bob);
            env(create(alice, bob, XRP(1000), settleDelay, pk));
            env.close();
            auto const [chan, chanSle] = channelKeyAndSle(*env.current(), alice, bob);
            BEAST_EXPECT(inOwnerDir(*env.current(), alice, chanSle));
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 1);
            BEAST_EXPECT(inOwnerDir(*env.current(), bob, chanSle));
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 1);
            // close the channel
            env(claim(bob, chan), txflags(tfClose));
            BEAST_EXPECT(!channelExists(*env.current(), chan));
            BEAST_EXPECT(!inOwnerDir(*env.current(), alice, chanSle));
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 0);
            BEAST_EXPECT(!inOwnerDir(*env.current(), bob, chanSle));
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 0);
        }

        {
            // Test removing paychans created before adding to the recipient's
            // owner directory
            Env env(
                *this, supported_amendments() - fixPayChanRecipientOwnerDir);
            env.fund (XRP (10000), alice, bob);
            // create the channel before the amendment activates
            env(create(alice, bob, XRP(1000), settleDelay, pk));
            env.close();
            auto const [chan, chanSle] = channelKeyAndSle(*env.current(), alice, bob);
            BEAST_EXPECT(inOwnerDir(*env.current(), alice, chanSle));
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 1);
            BEAST_EXPECT(!inOwnerDir(*env.current(), bob, chanSle));
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 0);
            env.enableFeature(fixPayChanRecipientOwnerDir);
            env.close();
            BEAST_EXPECT(
                env.current()->rules().enabled(fixPayChanRecipientOwnerDir));
            // These checks look redundant, but if you don't `close` after the
            // `create` these checks will fail. I believe this is due to the
            // create running with one set of amendments initially, then with a
            // different set with the ledger closes (tho I haven't dug into it)
            BEAST_EXPECT(inOwnerDir(*env.current(), alice, chanSle));
            BEAST_EXPECT(!inOwnerDir(*env.current(), bob, chanSle));
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 0);

            // close the channel after the amendment activates
            env(claim(bob, chan), txflags(tfClose));
            BEAST_EXPECT(!channelExists(*env.current(), chan));
            BEAST_EXPECT(!inOwnerDir(*env.current(), alice, chanSle));
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 0);
            BEAST_EXPECT(!inOwnerDir(*env.current(), bob, chanSle));
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 0);
        }
    }

    void
    testAccountDelete()
    {
        testcase("Account Delete");
        using namespace test::jtx;
        using namespace std::literals::chrono_literals;
        auto rmAccount = [this](
                             Env& env,
                             Account const& toRm,
                             Account const& dst,
                             TER expectedTer = tesSUCCESS) {
            // only allow an account to be deleted if the account's sequence
            // number is at least 256 less than the current ledger sequence
            for (auto minRmSeq = env.seq(toRm) + 257;
                env.current()->seq() < minRmSeq;
                env.close())
            { }

            env(acctdelete(toRm, dst),
                fee(drops(env.current()->fees().increment)),
                ter(expectedTer));
            env.close();
            this->BEAST_EXPECT(
                isTesSuccess(expectedTer) ==
                !env.closed()->exists(keylet::account(toRm.id())));
        };

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");

        for (bool const withOwnerDirFix : {false, true})
        {
            auto const amd = withOwnerDirFix
                ? supported_amendments()
                : supported_amendments() - fixPayChanRecipientOwnerDir;
            Env env{*this, amd};
            env.fund(XRP(10000), alice, bob, carol);
            env.close();
            auto const feeDrops = env.current()->fees().base;

            // Create a channel from alice to bob
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(create(alice, bob, XRP(1000), settleDelay, pk));
            env.close();
            auto const chan = channel(*env.current(), alice, bob);
            BEAST_EXPECT(channelBalance(*env.current(), chan) == XRP(0));
            BEAST_EXPECT(channelAmount(*env.current(), chan) == XRP(1000));

            rmAccount(env, alice, carol, tecHAS_OBLIGATIONS);
            // can only remove bob if the channel isn't in their owner direcotry
            rmAccount(
                env,
                bob,
                carol,
                withOwnerDirFix ? TER(tecHAS_OBLIGATIONS) : TER(tesSUCCESS));

            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            BEAST_EXPECT(chanBal == XRP(0));
            BEAST_EXPECT(chanAmt == XRP(1000));

            auto preBob = env.balance(bob);
            auto const delta = XRP(50);
            auto reqBal = chanBal + delta;
            auto authAmt = reqBal + XRP(100);
            assert(reqBal <= chanAmt);

            // claim should fail if the dst was removed
            if (withOwnerDirFix)
            {
                env(claim(alice, chan, reqBal, authAmt));
                env.close();
                BEAST_EXPECT(channelBalance(*env.current(), chan) == reqBal);
                BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
                BEAST_EXPECT(env.balance(bob) == preBob + delta);
                chanBal = reqBal;
            }
            else
            {
                auto const preAlice = env.balance(alice);
                env(claim(alice, chan, reqBal, authAmt), ter(tecNO_DST));
                env.close();
                BEAST_EXPECT(channelBalance(*env.current(), chan) == chanBal);
                BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
                BEAST_EXPECT(env.balance(bob) == preBob);
                BEAST_EXPECT(env.balance(alice) == preAlice - feeDrops);
            }

            // fund should fail if the dst was removed
            if (withOwnerDirFix)
            {
                auto const preAlice = env.balance(alice);
                env(fund(alice, chan, XRP(1000)));
                env.close();
                BEAST_EXPECT(
                    env.balance(alice) == preAlice - XRP(1000) - feeDrops);
                BEAST_EXPECT(
                    channelAmount(*env.current(), chan) == chanAmt + XRP(1000));
                chanAmt = chanAmt + XRP(1000);
            }
            else
            {
                auto const preAlice = env.balance(alice);
                env(fund(alice, chan, XRP(1000)), ter(tecNO_DST));
                env.close();
                BEAST_EXPECT(env.balance(alice) == preAlice - feeDrops);
                BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
            }

            {
                // Owner closes, will close after settleDelay
                env(claim(alice, chan), txflags(tfClose));
                env.close();
                // settle delay hasn't ellapsed. Channels should exist.
                BEAST_EXPECT(channelExists(*env.current(), chan));
                auto const closeTime = env.current()->info().parentCloseTime;
                auto const minExpiration = closeTime + settleDelay;
                env.close(minExpiration);
                env(claim(alice, chan), txflags(tfClose));
                BEAST_EXPECT(!channelExists(*env.current(), chan));
            }
        }

        {
            // test resurrected account
            Env env{*this,
                    supported_amendments() - fixPayChanRecipientOwnerDir};
            env.fund(XRP(10000), alice, bob, carol);
            env.close();
            auto const feeDrops = env.current()->fees().base;

            // Create a channel from alice to bob
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(create(alice, bob, XRP(1000), settleDelay, pk));
            env.close();
            auto const chan = channel(*env.current(), alice, bob);
            BEAST_EXPECT(channelBalance(*env.current(), chan) == XRP(0));
            BEAST_EXPECT(channelAmount(*env.current(), chan) == XRP(1000));

            // Since `fixPayChanRecipientOwnerDir` is not active, can remove bob
            rmAccount(env, bob, carol);
            BEAST_EXPECT(!env.closed()->exists(keylet::account(bob.id())));

            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            BEAST_EXPECT(chanBal == XRP(0));
            BEAST_EXPECT(chanAmt == XRP(1000));
            auto preBob = env.balance(bob);
            auto const delta = XRP(50);
            auto reqBal = chanBal + delta;
            auto authAmt = reqBal + XRP(100);
            assert(reqBal <= chanAmt);

            {
                // claim should fail, since bob doesn't exist
                auto const preAlice = env.balance(alice);
                env(claim(alice, chan, reqBal, authAmt), ter(tecNO_DST));
                env.close();
                BEAST_EXPECT(channelBalance(*env.current(), chan) == chanBal);
                BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
                BEAST_EXPECT(env.balance(bob) == preBob);
                BEAST_EXPECT(env.balance(alice) == preAlice - feeDrops);
            }

            {
                // fund should fail, sincebob doesn't exist
                auto const preAlice = env.balance(alice);
                env(fund(alice, chan, XRP(1000)), ter(tecNO_DST));
                env.close();
                BEAST_EXPECT(env.balance(alice) == preAlice - feeDrops);
                BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
            }

            // resurrect bob
            env(pay(alice, bob, XRP(20)));
            env.close();
            BEAST_EXPECT(env.closed()->exists(keylet::account(bob.id())));

            {
                // alice should be able to claim
                preBob = env.balance(bob);
                reqBal = chanBal + delta;
                authAmt = reqBal + XRP(100);
                env(claim(alice, chan, reqBal, authAmt));
                BEAST_EXPECT(channelBalance(*env.current(), chan) == reqBal);
                BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
                BEAST_EXPECT(env.balance(bob) == preBob + delta);
                chanBal = reqBal;
            }

            {
                // bob should be able to claim
                preBob = env.balance(bob);
                reqBal = chanBal + delta;
                authAmt = reqBal + XRP(100);
                auto const sig =
                    signClaimAuth(alice.pk(), alice.sk(), chan, authAmt);
                env(claim(bob, chan, reqBal, authAmt, Slice(sig), alice.pk()));
                BEAST_EXPECT(channelBalance(*env.current(), chan) == reqBal);
                BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
                BEAST_EXPECT(env.balance(bob) == preBob + delta - feeDrops);
                chanBal = reqBal;
            }

            {
                // alice should be able to fund
                auto const preAlice = env.balance(alice);
                env(fund(alice, chan, XRP(1000)));
                BEAST_EXPECT(
                    env.balance(alice) == preAlice - XRP(1000) - feeDrops);
                BEAST_EXPECT(
                    channelAmount(*env.current(), chan) == chanAmt + XRP(1000));
                chanAmt = chanAmt + XRP(1000);
            }

            {
                // Owner closes, will close after settleDelay
                env(claim(alice, chan), txflags(tfClose));
                env.close();
                // settle delay hasn't ellapsed. Channels should exist.
                BEAST_EXPECT(channelExists(*env.current(), chan));
                auto const closeTime = env.current()->info().parentCloseTime;
                auto const minExpiration = closeTime + settleDelay;
                env.close(minExpiration);
                env(claim(alice, chan), txflags(tfClose));
                BEAST_EXPECT(!channelExists(*env.current(), chan));
            }
        }
    }

    void
    run() override
    {
        testSimple();
        testCancelAfter();
        testSettleDelay();
        testExpiration();
        testCloseDry();
        testDefaultAmount();
        testDisallowXRP();
        testDstTag();
        testDepositAuth();
        testMultiple();
        testAccountChannelsRPC();
        testAccountChannelsRPCMarkers();
        testAccountChannelsRPCSenderOnly();
        testAuthVerifyRPC();
        testOptionalFields();
        testMalformedPK();
        testMetaAndOwnership();
        testAccountDelete();
    }
};

BEAST_DEFINE_TESTSUITE (PayChan, app, ripple);
}  // test
}  // ripple

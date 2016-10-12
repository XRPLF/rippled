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
#include <ripple/protocol/Sign.h>
#include <ripple/protocol/STParsedJSON.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/test/jtx.h>

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
    STAmount
    channelBalance (ReadView const& view,
        uint256 const& chan,
        jtx::Account const& account)
    {
        auto const slep = view.read ({ltPAYCHAN, chan});
        if (!slep)
            return XRPAmount{-1};

        for (auto const& member : slep->getFieldArray (sfChannelMembers))
        {
            if (member.getAccountID (sfAccount) == account.id ())
                return member[sfBalance];
        }
        return XRPAmount{-1};
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
    channelAmount (ReadView const& view,
        uint256 const& chan,
        jtx::Account const& account)
    {
        auto const slep = view.read ({ltPAYCHAN, chan});
        if (!slep)
            return XRPAmount{-1};
        for (auto const& member : slep->getFieldArray (sfChannelMembers))
        {
            if (member.getAccountID (sfAccount) == account.id ())
                return member[sfAmount];
        }
        return XRPAmount{-1};
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
        PublicKey const& dpk,
        boost::optional<NetClock::time_point> const& cancelAfter = boost::none)
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
        jv["DstPublicKey"] = strHex (dpk.slice ());
        if (cancelAfter)
            jv["CancelAfter"] = cancelAfter->time_since_epoch ().count ();
        return jv;
    }

    static
    Json::Value
    fund (jtx::Account const& account,
        uint256 const& channel,
        STAmount const& amount)
    {
        using namespace jtx;
        Json::Value jv;
        jv[jss::TransactionType] = "PaymentChannelFund";
        jv[jss::Flags] = tfUniversal;
        jv[jss::Account] = account.human ();
        jv["Channel"] = to_string (channel);
        jv[jss::Amount] = amount.getJson (0);
        return jv;
    }

    static
    Json::Value
    makeClaim (PublicKey const& pk,
        uint256 const& channel,
        STAmount const& amount,
        std::uint32_t sequence,
        boost::optional<std::uint32_t> const& dstTag = boost::none,
        boost::optional<std::uint32_t> const& sourceTag = boost::none,
        boost::optional<std::string> const& invoiceId = boost::none)
    {
        using namespace jtx;

        Json::Value claim;
        claim["Channel"] = to_string (channel);
        claim["PublicKey"] = strHex (pk.slice ());
        claim[jss::Amount] = amount.getJson (0);
        claim[jss::Sequence] = sequence;
        if (dstTag)
            claim["DestinationTag"] = *dstTag;
        if (sourceTag)
            claim["SourceTag"] = *sourceTag;
        if (invoiceId)
            claim["InvoiceID"] = *invoiceId;

        return claim;
    }

    Json::Value
    signClaim (SecretKey const& sk,
        Json::Value claimJson)
    {
        using namespace jtx;

        STParsedJSONObject claim ("claim", claimJson, sfChannelClaim);
        BEAST_EXPECT (claim.object);

        auto const pk = get<PublicKey>(*claim.object, sfPublicKey);
        BEAST_EXPECT (pk);

        auto keyType = publicKeyType (*pk);
        BEAST_EXPECT (keyType);
        BEAST_EXPECT (*pk == derivePublicKey (*keyType, sk));

        sign (*claim.object, HashPrefix::paymentChannelClaim, *keyType, sk);

        return claim.object->getJson (0);
    }

    static
    Json::Value
    claim (jtx::Account const& account,
        uint256 const& channel,
        boost::optional<Json::Value> const& claim1 = boost::none,
        boost::optional<Json::Value> const& claim2 = boost::none)
    {
        using namespace jtx;
        Json::Value jv;
        jv[jss::TransactionType] = "PaymentChannelClaim";
        jv[jss::Flags] = tfUniversal;
        jv[jss::Account] = account.human ();
        jv["Channel"] = to_string (channel);
        if (claim1)
        {
            jv["ChannelClaims"] = Json::Value (Json::arrayValue);
            Json::Value jvClaim1;
            jvClaim1["ChannelClaim"] = *claim1;
            jv["ChannelClaims"].append (jvClaim1);
            if (claim2)
            {
                Json::Value jvClaim2;
                jvClaim2["ChannelClaim"] = *claim2;
                jv["ChannelClaims"].append (jvClaim2);
            }
        }
        return jv;
    }

    void
    testCreate ()
    {
        testcase ("create");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env (*this, features (featurePayChan));
        auto const alice = Account ("alice");
        auto const bob = Account ("bob");
        env.fund (XRP (10000), alice, bob);
        auto const pk = alice.pk ();
        auto const dpk = bob.pk ();
        auto const settleDelay = 100s;

        {
            // bad amounts (non-xrp, negative or zero amounts)
            env (create (alice, bob, alice["USD"] (1000), settleDelay, pk, dpk),
                ter (temBAD_AMOUNT));
            env (create (alice, bob, XRP (-1000), settleDelay, pk, dpk),
                ter (temBAD_AMOUNT));
            env (create (alice, bob, XRP (0), settleDelay, pk, dpk),
                ter (temBAD_AMOUNT));
        }

        // invalid account
        env (create (alice, "noAccount", XRP (1000), settleDelay, pk, dpk),
            ter (tecNO_DST));
        // can't create channel to the same account
        env (create (alice, alice, XRP (1000), settleDelay, pk, dpk),
            ter (temDST_IS_SRC));
        // can't create channel with matching public keys
        env (create (alice, bob, XRP (1000), settleDelay, pk, pk),
            ter (temDST_IS_SRC));
        // not enough funds
        env (create (alice, bob, XRP (10000), settleDelay, pk, dpk),
            ter (tecUNFUNDED));

        {
            // src disallows XRP
            env (fset (alice, asfDisallowXRP));
            env (create (
                alice, bob, XRP (1000), settleDelay, alice.pk (), bob.pk ()),
                    ter (tecNO_TARGET));
            auto const chan = channel (*env.current (), alice, bob);
            BEAST_EXPECT (!channelExists (*env.current (), chan));
            env (fclear (alice, asfDisallowXRP));
        }
        {
            // dst disallows XRP
            env (fset (bob, asfDisallowXRP));
            env (create (
                alice, bob, XRP (1000), settleDelay, alice.pk (), bob.pk ()),
                    ter (tecNO_TARGET));
            auto const chan = channel (*env.current (), alice, bob);
            BEAST_EXPECT (!channelExists (*env.current (), chan));
            env (fclear (bob, asfDisallowXRP));
        }
        {
            // Create channel
            auto const preAlice = env.balance (alice);
            auto const preBob = env.balance (bob);
            auto const amount = XRP (1000);
            env (create (alice, bob, amount, settleDelay, pk, dpk));
            auto const chan = channel (*env.current (), alice, bob);
            BEAST_EXPECT (channelExists (*env.current (), chan));
            BEAST_EXPECT (
                channelBalance (*env.current (), chan, alice) == XRP (1000));
            BEAST_EXPECT (
                channelBalance (*env.current (), chan, bob) == XRP (0));
            BEAST_EXPECT (
                channelAmount (*env.current (), chan, alice) == XRP (0));
            BEAST_EXPECT (
                channelAmount (*env.current (), chan, bob) == XRP (0));
            auto const feeDrops = env.current ()->fees ().base;
            BEAST_EXPECT (env.balance (alice) == preAlice - amount - feeDrops);
            BEAST_EXPECT (env.balance (bob) == preBob);

            // Create multiple channels to the same account
            env (create (alice, bob, amount, settleDelay, pk, dpk));
            auto const chan2 = channel (*env.current (), alice, bob);
            BEAST_EXPECT (channelExists (*env.current (), chan2));
            BEAST_EXPECT (chan != chan2);
        }
        {
            // Create a channel where member requires destination tag
            env (fset (bob, asfRequireDest));
            env (create (
                alice, bob, XRP (1000), settleDelay, alice.pk (), bob.pk ()));
            auto const chan = channel (*env.current (), alice, bob);
            BEAST_EXPECT (channelExists (*env.current (), chan));
        }
    }

    void
    testFund ()
    {
        testcase ("fund");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env (*this, features (featurePayChan));
        auto const alice = Account ("alice");
        auto const bob = Account ("bob");
        env.fund (XRP (10000), alice, bob);
        auto const settleDelay = 100s;
        env (create (
            alice, bob, XRP (1000), settleDelay, alice.pk (), bob.pk ()));
        auto const chan = channel (*env.current (), alice, bob);
        auto const aliceBalance = channelBalance (*env.current (), chan, alice);
        auto const bobBalance = channelBalance (*env.current (), chan, bob);

        {
            // bad amounts (non-xrp, negative or zero amounts)
            env (fund (alice, chan, alice["USD"] (1000)), ter (temBAD_AMOUNT));
            env (fund (alice, chan, XRP (-1000)), ter (temBAD_AMOUNT));
            env (fund (alice, chan, XRP (0)), ter (temBAD_AMOUNT));
        }
        {
            // invalid channel
            auto const preAlice = env.balance (alice);
            env (fund (alice, channel (
                *env.current (), alice, "noAccount"), XRP (1000)),
                    ter (tecNO_ENTRY));
            auto const feeDrops = env.current ()->fees ().base;
            BEAST_EXPECT (env.balance (alice) == preAlice - feeDrops);
        }
        {
            // not enough funds
            auto const preAlice = env.balance (alice);
            env (fund (alice, chan, XRP (10000)), ter (tecUNFUNDED));
            auto const feeDrops = env.current ()->fees ().base;
            BEAST_EXPECT (env.balance (alice) == preAlice - feeDrops);
        }
        {
            // Third party cannot fund channel
            auto const carol = Account ("carol");
            env.fund (XRP (10000), carol);
            env (fund (carol, chan, XRP (1000)), ter (tecNO_PERMISSION));
        }
        {
            // Alice funds channel
            auto const preAlice = env.balance (alice);
            auto const amount = XRP (1000);
            env (fund (alice, chan, amount));
            BEAST_EXPECT (
                channelBalance (*env.current (), chan, alice) ==
                    aliceBalance + amount);
            auto const feeDrops = env.current ()->fees ().base;
            BEAST_EXPECT (
                env.balance (alice) == preAlice - amount - feeDrops);
        }
        {
            // Bob funds channel
            auto const preBob = env.balance (bob);
            auto const amount = XRP (1000);
            env (fund (bob, chan, amount));
            BEAST_EXPECT (
                channelBalance (*env.current (), chan, bob) ==
                    bobBalance + amount);
            auto const feeDrops = env.current ()->fees ().base;
            BEAST_EXPECT (env.balance (bob) == preBob - amount - feeDrops);
        }
    }

    void
    testClaim ()
    {
        testcase ("claim");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env (*this, features (featurePayChan));
        auto const alice = Account ("alice");
        auto const bob = Account ("bob");
        auto const carol = Account ("carol");
        env.fund (XRP (10000), alice, bob, carol);
        auto const settleDelay = 100s;
        env (create (
            alice, bob, XRP (1000), settleDelay, alice.pk (), bob.pk ()));
        auto const chan = channel (*env.current (), alice, bob);

        {
            // Claim to non-existent channel
            auto const chan = channel (*env.current (), alice, carol);
            auto const newClaim = signClaim (alice.sk (), makeClaim (
                alice.pk (), chan, XRP (500), 0));
            env (claim (alice, chan, newClaim), ter (tecNO_ENTRY));
        }
        {
            // Claim with bad amounts (negative and non-xrp)
            auto iouClaim = signClaim (alice.sk (), makeClaim (
                alice.pk (), chan, alice["USD"] (100), 0));
            env (claim (alice, chan, iouClaim), ter (temBAD_AMOUNT));

            auto const negClaim = signClaim (alice.sk (), makeClaim (
                alice.pk (), chan, XRP (-100), 0));
            env (claim (alice, chan, negClaim), ter (temBAD_AMOUNT));
        }
        {
            // Claim with wrong signing key
            auto const badClaim = signClaim (carol.sk (), makeClaim (
                carol.pk (), chan, XRP (500), 0));
            env (claim (bob, chan, badClaim), ter (temBAD_SIGNER));
        }
        {
            // Claim with bad signature
            auto claim1 = signClaim (alice.sk (), makeClaim (
                alice.pk (), chan, XRP (1500).value (), 1));
            auto claim2 = signClaim (alice.sk (), makeClaim (
                alice.pk (), chan, XRP (1500).value (), 2));
            claim1["Signature"] = claim2["Signature"];
            env (claim (bob, chan, claim1), ter (temBAD_SIGNATURE));
        }
        {
            // Claim
            auto preBob = env.balance (bob);
            auto const authAmt = XRP (500);
            auto const newClaim = signClaim (alice.sk (), makeClaim (
                alice.pk (), chan, authAmt, 0));
            env (claim (bob, chan, newClaim));
            BEAST_EXPECT (
                channelAmount (*env.current (), chan, alice) == authAmt);
            auto const feeDrops = env.current ()->fees ().base;
            BEAST_EXPECT (env.balance (bob) == preBob - feeDrops);

            // Retry stale claim
            preBob = env.balance (bob);
            env (claim (bob, chan, newClaim), ter (tefPAST_SEQ));
            BEAST_EXPECT (
                channelAmount (*env.current (), chan, alice) == authAmt);
            BEAST_EXPECT (env.balance (bob) == preBob);
        }
        {
            // Missing claims after settle delay is triggered
            env (claim (bob, chan), ter (tecNO_PERMISSION));
        }
        {
            // Cannot submit two claims from single signer
            auto const newClaim = signClaim (alice.sk (), makeClaim (
                alice.pk (), chan, XRP (500), 0));
            env (claim (bob, chan, newClaim, newClaim), ter (temMALFORMED));
        }
        {
            // Cannot submit more than two claims
            auto const aliceClaim = signClaim (alice.sk (), makeClaim (
                alice.pk (), chan, XRP (500), 0));
            auto const bobClaim = signClaim (bob.sk (), makeClaim (
                bob.pk (), chan, XRP (500), 0));

            auto tripleClaim = claim (bob, chan, aliceClaim, bobClaim);
            Json::Value jvClaim3;
            jvClaim3["ChannelClaim"] = bobClaim;
            tripleClaim["ChannelClaims"].append (jvClaim3);
            env (tripleClaim, ter (temMALFORMED));
        }
        {
            // Try to claim more than available
            auto const preAmount =
                channelAmount (*env.current (), chan, alice);
            auto const preBob = env.balance (bob);
            auto const authAmt =
                channelBalance (*env.current (), chan, alice) + XRP (500);
            auto const badClaim = signClaim (alice.sk (), makeClaim (
                alice.pk (), chan, authAmt, 0));
            env (claim (bob, chan, badClaim), ter (tecUNFUNDED_PAYMENT));
            BEAST_EXPECT (
                channelAmount (*env.current (), chan, alice) == preAmount);
            auto const feeDrops = env.current ()->fees ().base;
            BEAST_EXPECT (env.balance (bob) == preBob - feeDrops);
        }
        {
            // Honor claim to a channel where member disallows XRP
            // (channel is created before disallow xrp is set)
            env (fset (bob, asfDisallowXRP));
            auto const authAmt =
                channelAmount (*env.current (), chan, alice) + XRP (100);
            auto const newClaim = signClaim (alice.sk (), makeClaim (
                alice.pk (), chan, authAmt, 0));
            env (claim (alice, chan, newClaim));
            BEAST_EXPECT (channelAmount (*env.current (), chan, alice) == authAmt);
        }
        {
            // Honor claims with or without destination tag
            env (fset (bob, asfRequireDest));
            auto const authAmt1 =
                channelAmount (*env.current (), chan, alice) + XRP (100);
            auto const tagClaim = signClaim (alice.sk (), makeClaim (
                alice.pk (), chan, authAmt1, 0, 1));
            env (claim (alice, chan, tagClaim));
            BEAST_EXPECT (
                channelAmount (*env.current (), chan, alice) == authAmt1);
            auto const authAmt2 =
                channelAmount (*env.current (), chan, alice) + XRP (100);
            auto const taglessClaim = signClaim (alice.sk (), makeClaim (
                alice.pk (), chan, authAmt2, 0));
            env (claim (alice, chan, taglessClaim));
            BEAST_EXPECT (
                channelAmount (*env.current (), chan, alice) == authAmt2);
        }
        {
            // Submit claim with optional fields
            auto const authAmt1 =
                channelAmount (*env.current (), chan, alice) + XRP (100);
            std::string invoiceId = "claim invoice";
            auto const loadedClaim = signClaim (alice.sk (), makeClaim (
                alice.pk (), chan, authAmt1, 0, 1, 2, invoiceId));
            env (claim (alice, chan, loadedClaim));
        }
    }

    void
    testCancelAfter ()
    {
        testcase ("cancel after");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env (*this, features (featurePayChan));
        auto const alice = Account ("alice");
        auto const bob = Account ("bob");
        auto const carol = Account ("carol");
        env.fund (XRP (10000), alice, bob, carol);
        auto const pk = alice.pk ();
        auto const dpk = bob.pk ();
        auto const settleDelay = 100s;
        auto const channelFunds = XRP (1000);

        {
            // Close channel after cancelAfter
            NetClock::time_point const cancelAfter =
                env.current ()->info ().parentCloseTime + 3600s;
            env (create (
                alice, bob, channelFunds, settleDelay, pk, dpk, cancelAfter));
            auto const chan = channel (*env.current (), alice, bob);
            BEAST_EXPECT (channelExists (*env.current (), chan));
            BEAST_EXPECT (*channelExpiration (*env.current (), chan) ==
                cancelAfter.time_since_epoch ().count ());
            env.close (cancelAfter);
            {
                // No one can claim after cancelAfter
                auto preAlice = env.balance (alice);
                auto preBob = env.balance (bob);
                auto const authAmt = XRP (500);
                auto const lateClaim = signClaim (alice.sk (), makeClaim (
                    alice.pk (), chan, authAmt, 0));
                env (claim (bob, chan, lateClaim));
                auto const feeDrops = env.current ()->fees ().base;
                BEAST_EXPECT (!channelExists (*env.current (), chan));
                BEAST_EXPECT (env.balance (bob) == preBob - feeDrops);
                BEAST_EXPECT (env.balance (alice) == preAlice + channelFunds);
            }
        }
        {
            // Third party can close after cancelAfter
            NetClock::time_point const cancelAfter =
                env.current ()->info ().parentCloseTime + 3600s;
            env (create (
                alice, bob, channelFunds, settleDelay, pk, dpk, cancelAfter));
            auto const chan = channel (*env.current (), alice, bob);
            BEAST_EXPECT (channelExists (*env.current (), chan));
            BEAST_EXPECT (*channelExpiration (*env.current (), chan) ==
                cancelAfter.time_since_epoch ().count ());
            // third party cannot close before cancelAfter
            env (claim (carol, chan), ter (tecNO_PERMISSION));
            BEAST_EXPECT (channelExists (*env.current (), chan));
            // third party close after cancelAfter
            env.close (cancelAfter);
            auto const preAlice = env.balance (alice);
            env (claim (carol, chan));
            BEAST_EXPECT (!channelExists (*env.current (), chan));
            BEAST_EXPECT (env.balance (alice) == preAlice + channelFunds);
        }
    }

    void
    testSettleDelay ()
    {
        testcase ("settle delay");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env (*this, features (featurePayChan));
        auto const alice = Account ("alice");
        auto const bob = Account ("bob");
        auto const carol = Account ("carol");
        env.fund (XRP (10000), alice, bob, carol);
        auto const settleDelay = 3600s;
        NetClock::time_point settleTimepoint =
            env.current ()->info ().parentCloseTime + settleDelay;
        auto const channelFunds = XRP (1000);
        env (create (
            alice, bob, channelFunds, settleDelay, alice.pk (), bob.pk ()));
        auto const chan = channel (*env.current (), alice, bob);
        BEAST_EXPECT (channelExists (*env.current (), chan));
        BEAST_EXPECT (!channelExpiration (*env.current (), chan));
        env (fund (bob, chan, channelFunds));

        {
            // Third party does not start settle delay when submitting claim
            auto const authAmt =
                channelAmount (*env.current (), chan, alice) + XRP (100);
            auto const aliceClaim = signClaim (alice.sk (), makeClaim (
                alice.pk (), chan, authAmt, 0));
            env (claim (carol, chan, aliceClaim));
            BEAST_EXPECT (channelAmount (*env.current (), chan, alice) == authAmt);
            BEAST_EXPECT (! channelExpiration (*env.current (), chan));
        }

        // Channel member can trigger settle delay without any claims
        env (claim (alice, chan));
        BEAST_EXPECT (channelExists (*env.current (), chan));
        BEAST_EXPECT (*channelExpiration (*env.current (), chan) ==
            settleTimepoint.time_since_epoch ().count ());
        env.close (settleTimepoint-settleDelay/2);

        {
            // Bob extends settle delay with Alice's claim
            auto const authAmt =
                channelAmount (*env.current (), chan, alice) + XRP (100);
            auto const aliceClaim = signClaim (alice.sk (), makeClaim (
                alice.pk (), chan, authAmt, 0));
            env (claim (bob, chan, aliceClaim));
            BEAST_EXPECT (
                channelAmount (*env.current (), chan, alice) == authAmt);
            settleTimepoint =
                env.current ()->info ().parentCloseTime + settleDelay;
            BEAST_EXPECT (*channelExpiration (*env.current (), chan) ==
                settleTimepoint.time_since_epoch ().count ());
            env.close (settleTimepoint-settleDelay/2);
        }
        {
            // extend settle time with two claims
            auto const authAmtAlice =
                channelAmount (*env.current (), chan, alice) + XRP (100);
            auto const aliceClaim = signClaim (alice.sk (), makeClaim (
                alice.pk (), chan, authAmtAlice, 0));
            auto const authAmtBob =
                channelAmount (*env.current (), chan, bob) + XRP (100);
            auto const bobClaim = signClaim (bob.sk (), makeClaim (
                bob.pk (), chan, authAmtBob, 0));
            env (claim (bob, chan, aliceClaim, bobClaim));
            BEAST_EXPECT (
                channelAmount (*env.current (), chan, alice) == authAmtAlice);
            BEAST_EXPECT (
                channelAmount (*env.current (), chan, bob) == authAmtBob);
            settleTimepoint =
                env.current ()->info ().parentCloseTime + settleDelay;
            BEAST_EXPECT (*channelExpiration (*env.current (), chan) ==
                settleTimepoint.time_since_epoch ().count ());
            env.close (settleTimepoint-settleDelay/2);
        }
        {
            // third party extends settle time with claim
            auto const authAmt =
                channelAmount (*env.current (), chan, alice) + XRP (100);
            auto const aliceClaim = signClaim (alice.sk (), makeClaim (
                alice.pk (), chan, authAmt, 0));
            env (claim (carol, chan, aliceClaim));
            BEAST_EXPECT (channelAmount (*env.current (), chan, alice) == authAmt);
            settleTimepoint = env.current ()->info ().parentCloseTime + settleDelay;
            BEAST_EXPECT (*channelExpiration (*env.current (), chan) ==
                settleTimepoint.time_since_epoch ().count ());
            env.close (settleTimepoint-settleDelay/2);
        }
        {
            // submitting own claim does not extend settle time
            auto const preExpiration =
                *channelExpiration (*env.current (), chan);
            auto const authAmt =
                channelAmount (*env.current (), chan, alice) + XRP (100);
            auto const aliceClaim = signClaim (alice.sk (), makeClaim (
                alice.pk (), chan, authAmt, 0));
            env (claim (alice, chan, aliceClaim));
            BEAST_EXPECT (
                channelAmount (*env.current (), chan, alice) == authAmt);
            settleTimepoint =
                env.current ()->info ().parentCloseTime + settleDelay;
            BEAST_EXPECT (*channelExpiration (*env.current (), chan) ==
                preExpiration);
        }
        env.close (settleTimepoint);
        {
            // past settle time, channel will close
            auto const preAlice = env.balance (alice);
            auto const preBob = env.balance (bob);
            auto const aliceAmount = channelAmount (*env.current (), chan, alice);
            auto const bobAmount = channelAmount (*env.current (), chan, bob);
            auto const aliceBalance = channelBalance (*env.current (), chan, alice);
            auto const bobBalance = channelBalance (*env.current (), chan, bob);
            auto const authAmt = bobAmount + XRP (100);
            auto const lateClaim = signClaim (bob.sk (), makeClaim (
                bob.pk (), chan, authAmt, 0));
            env (claim (alice, chan, lateClaim));
            BEAST_EXPECT (! channelExists (*env.current (), chan));
            auto const feeDrops = env.current ()->fees ().base;
            auto const diff = aliceAmount - bobAmount;
            BEAST_EXPECT (env.balance (alice) ==
                preAlice + aliceBalance - diff - feeDrops);
            BEAST_EXPECT (env.balance (bob) == preBob + bobBalance + diff);
        }
    }

    void
    testRPC ()
    {
        testcase ("RPC");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env (*this, features (featurePayChan));
        auto const alice = Account ("alice");
        auto const bob = Account ("bob");
        auto const carol = Account ("carol");
        env.fund (XRP (10000), alice, bob, carol);
        auto const pk = alice.pk ();
        auto const dpk = bob.pk ();
        auto const cpk = carol.pk ();
        auto const settleDelay = 3600s;
        auto const channelFunds = XRP (1000);
        env (create (alice, bob, channelFunds, settleDelay, pk, dpk));
        env.close();
        auto const chan1 = channel (*env.current (), alice, bob);
        auto const chan1Str = to_string (chan1);
        env (create (alice, carol, channelFunds, settleDelay, pk, cpk));
        env.close();
        auto const chan2 = channel (*env.current (), alice, carol);
        auto const chan2Str = to_string (chan2);
        BEAST_EXPECT (chan1Str != chan2Str);

        {
            // get all open channels for account
            auto const r =
                env.rpc ("account_channels", alice.human ());
            BEAST_EXPECT (r[jss::result][jss::channels].size () == 2);
            BEAST_EXPECT (
                r[jss::result][jss::channels][0u][jss::channel_id] !=
                r[jss::result][jss::channels][1u][jss::channel_id]);
            for (auto const& c : {chan1Str, chan2Str})
                BEAST_EXPECT (
                    r[jss::result][jss::channels][0u][jss::channel_id] == c ||
                    r[jss::result][jss::channels][1u][jss::channel_id] == c);
        }
        {
            auto const r =
                env.rpc ("account_channels", bob.human ())[jss::result];
            BEAST_EXPECT (r[jss::channels].size () == 1);
            BEAST_EXPECT (
                r[jss::channels][0u][jss::channel_id] == chan1Str);
            BEAST_EXPECT (
                r[jss::channels][0u][jss::members][0u][jss::account] ==
                alice.human ());
            BEAST_EXPECT (
                r[jss::channels][0u][jss::members][1u][jss::account] ==
                bob.human ());
            std::string const pkStr =
                r[jss::channels][0u][jss::members][0u][jss::public_key].asString();
            BEAST_EXPECT (
                pkStr == toBase58 (TokenType::TOKEN_ACCOUNT_PUBLIC, pk));
            std::string const pkHex =
                r[jss::channels][0u][jss::members][0u][jss::public_key_hex].asString();
            BEAST_EXPECT (pkHex == strHex (pk));
        }
        {
            // get all open channels between two accounts
            auto const r =
                env.rpc ("account_channels", alice.human (), bob.human ());
            BEAST_EXPECT (r[jss::result][jss::channels].size () == 1);
            BEAST_EXPECT (
                r[jss::result][jss::channels][0u][jss::channel_id] == chan1Str);
        }

        auto const secretKey = toBase58 (
            TokenType::TOKEN_ACCOUNT_SECRET, alice.sk());
        {
            // Verify chan1 auth
            auto const claim = makeClaim (pk, chan1, XRP (1000), 1);
            auto rv = env.rpc (
                "channel_authorize", secretKey, to_string(claim))[jss::result];
            BEAST_EXPECT(rv[jss::status] == "success");
            auto signedClaim = rv["ChannelClaim"];
            rv = env.rpc (
                "channel_verify", to_string(signedClaim))[jss::result];
            BEAST_EXPECT (rv[jss::signature_verified].asBool ());

            // Verification fails on bad signature
            signedClaim["Sequence"] = 2;
            rv = env.rpc (
                "channel_verify", to_string(signedClaim))[jss::result];
            BEAST_EXPECT (!rv[jss::signature_verified].asBool ());
        }
        {
            // Do not authorize bad claim
            auto badClaim = makeClaim (pk, chan1, XRP (1000), 1);
            badClaim["BogusField"] = 1;
            auto const rv = env.rpc (
                "channel_authorize", secretKey, to_string(badClaim))[jss::result];
            BEAST_EXPECT (rv[jss::status] == "error");
            BEAST_EXPECT (
                rv[jss::error_message] == "Field 'claim.BogusField' is unknown.");
        }
        {
            // Do not authorize bad amount
            auto const badClaim = makeClaim (pk, chan1, alice["USD"] (100), 1);
            auto const rv = env.rpc (
                "channel_authorize", secretKey, to_string(badClaim))[jss::result];
            BEAST_EXPECT (rv[jss::status] == "error");
            BEAST_EXPECT (
                rv[jss::request][jss::error_message] ==
                "Payment channel amount is malformed.");
        }
    }

    void
    run () override
    {
        testCreate ();
        testFund ();
        testClaim ();
        testCancelAfter ();
        testSettleDelay ();
        testRPC ();
    }
};

BEAST_DEFINE_TESTSUITE (PayChan, app, ripple);
}  // test
}  // ripple

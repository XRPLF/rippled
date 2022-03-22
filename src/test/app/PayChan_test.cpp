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
#include <ripple/protocol/PayChan.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>

#include <chrono>

namespace ripple {
namespace test {
struct PayChan_test : public beast::unit_test::suite
{
    FeatureBitset const disallowIncoming{featureDisallowIncoming};

    static uint256
    channel(
        jtx::Account const& account,
        jtx::Account const& dst,
        std::uint32_t seqProxyValue)
    {
        auto const k = keylet::payChan(account, dst, seqProxyValue);
        return k.key;
    }

    static std::pair<uint256, std::shared_ptr<SLE const>>
    channelKeyAndSle(
        ReadView const& view,
        jtx::Account const& account,
        jtx::Account const& dst)
    {
        auto const sle = view.read(keylet::account(account));
        if (!sle)
            return {};
        auto const k = keylet::payChan(account, dst, (*sle)[sfSequence] - 1);
        return {k.key, view.read(k)};
    }

    static Buffer
    signClaimAuth(
        PublicKey const& pk,
        SecretKey const& sk,
        uint256 const& channel,
        STAmount const& authAmt)
    {
        Serializer msg;
        serializePayChanAuthorization(msg, channel, authAmt.xrp());
        return sign(pk, sk, msg.slice());
    }

    static Buffer
    signClaimICAuth(
        PublicKey const& pk,
        SecretKey const& sk,
        uint256 const& channel,
        STAmount const& authAmt)
    {
        Serializer msg;
        serializePayChanAuthorization(
            msg,
            channel,
            authAmt.iou(),
            authAmt.getCurrency(),
            authAmt.getIssuer());
        return sign(pk, sk, msg.slice());
    }

    static STAmount
    channelBalance(ReadView const& view, uint256 const& chan)
    {
        auto const slep = view.read({ltPAYCHAN, chan});
        if (!slep)
            return XRPAmount{-1};
        return (*slep)[sfBalance];
    }

    static bool
    channelExists(ReadView const& view, uint256 const& chan)
    {
        auto const slep = view.read({ltPAYCHAN, chan});
        return bool(slep);
    }

    static STAmount
    channelAmount(ReadView const& view, uint256 const& chan)
    {
        auto const slep = view.read({ltPAYCHAN, chan});
        if (!slep)
            return XRPAmount{-1};
        return (*slep)[sfAmount];
    }

    static Rate
    channelRate(ReadView const& view, uint256 const& chan)
    {
        auto const slep = view.read({ltPAYCHAN, chan});
        if (!slep)
            return Rate{0};
        return ripple::Rate((*slep)[sfTransferRate]);
    }

    static STAmount
    lockedAmount(
        jtx::Env const& env,
        jtx::Account const& account,
        jtx::Account const& gw,
        jtx::IOU const& iou)
    {
        auto const sle = env.le(keylet::line(account, gw, iou.currency));
        if (sle->isFieldPresent(sfLockedBalance))
            return (*sle)[sfLockedBalance];
        return STAmount(iou, 0);
    }

    static STAmount
    limitAmount(
        jtx::Env const& env,
        jtx::Account const& account,
        jtx::Account const& gw,
        jtx::IOU const& iou)
    {
        auto const sle = env.le(keylet::line(account, gw, iou.currency));
        return (*sle)[sfHighLimit];
    }

    static STAmount
    lineBalance(
        jtx::Env const& env,
        jtx::Account const& account,
        jtx::Account const& gw,
        jtx::IOU const& iou)
    {
        auto const sle = env.le(keylet::line(account, gw, iou.currency));
        if (sle && sle->isFieldPresent(sfBalance))
            return (*sle)[sfBalance];
        return STAmount(iou, 0);
    }

    static std::optional<std::int64_t>
    channelExpiration(ReadView const& view, uint256 const& chan)
    {
        auto const slep = view.read({ltPAYCHAN, chan});
        if (!slep)
            return std::nullopt;
        if (auto const r = (*slep)[~sfExpiration])
            return r.value();
        return std::nullopt;
    }

    static Json::Value
    create(
        jtx::Account const& account,
        jtx::Account const& to,
        STAmount const& amount,
        NetClock::duration const& settleDelay,
        PublicKey const& pk,
        std::optional<NetClock::time_point> const& cancelAfter = std::nullopt,
        std::optional<std::uint32_t> const& dstTag = std::nullopt)
    {
        using namespace jtx;
        Json::Value jv;
        jv[jss::TransactionType] = jss::PaymentChannelCreate;
        jv[jss::Flags] = tfUniversal;
        jv[jss::Account] = account.human();
        jv[jss::Destination] = to.human();
        jv[jss::Amount] = amount.getJson(JsonOptions::none);
        jv["SettleDelay"] = settleDelay.count();
        jv["PublicKey"] = strHex(pk.slice());
        if (cancelAfter)
            jv["CancelAfter"] = cancelAfter->time_since_epoch().count();
        if (dstTag)
            jv["DestinationTag"] = *dstTag;
        return jv;
    }

    static Json::Value
    fund(
        jtx::Account const& account,
        uint256 const& channel,
        STAmount const& amount,
        std::optional<NetClock::time_point> const& expiration = std::nullopt)
    {
        using namespace jtx;
        Json::Value jv;
        jv[jss::TransactionType] = jss::PaymentChannelFund;
        jv[jss::Flags] = tfUniversal;
        jv[jss::Account] = account.human();
        jv["Channel"] = to_string(channel);
        jv[jss::Amount] = amount.getJson(JsonOptions::none);
        if (expiration)
            jv["Expiration"] = expiration->time_since_epoch().count();
        return jv;
    }

    static Json::Value
    claim(
        jtx::Account const& account,
        uint256 const& channel,
        std::optional<STAmount> const& balance = std::nullopt,
        std::optional<STAmount> const& amount = std::nullopt,
        std::optional<Slice> const& signature = std::nullopt,
        std::optional<PublicKey> const& pk = std::nullopt)
    {
        using namespace jtx;
        Json::Value jv;
        jv[jss::TransactionType] = jss::PaymentChannelClaim;
        jv[jss::Flags] = tfUniversal;
        jv[jss::Account] = account.human();
        jv["Channel"] = to_string(channel);
        if (amount)
            jv[jss::Amount] = amount->getJson(JsonOptions::none);
        if (balance)
            jv["Balance"] = balance->getJson(JsonOptions::none);
        if (signature)
            jv["Signature"] = strHex(*signature);
        if (pk)
            jv["PublicKey"] = strHex(pk->slice());
        return jv;
    }

    void
    testSimple(FeatureBitset features)
    {
        testcase("simple");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.fund(XRP(10000), alice, bob);
        auto const pk = alice.pk();
        auto const settleDelay = 100s;
        auto const chan = channel(alice, bob, env.seq(alice));
        env(create(alice, bob, XRP(1000), settleDelay, pk));
        BEAST_EXPECT(channelBalance(*env.current(), chan) == XRP(0));
        BEAST_EXPECT(channelAmount(*env.current(), chan) == XRP(1000));

        {
            auto const preAlice = env.balance(alice);
            env(fund(alice, chan, XRP(1000)));
            auto const feeDrops = env.current()->fees().base;
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(1000) - feeDrops);
        }

        auto chanBal = channelBalance(*env.current(), chan);
        auto chanAmt = channelAmount(*env.current(), chan);
        BEAST_EXPECT(chanBal == XRP(0));
        BEAST_EXPECT(chanAmt == XRP(2000));

        {
            // bad amounts (negative amounts)
            env(create(alice, bob, XRP(-1000), settleDelay, pk),
                ter(temBAD_AMOUNT));
            env(fund(alice, chan, XRP(-1000)), ter(temBAD_AMOUNT));
        }

        // invalid account
        env(create(alice, "noAccount", XRP(1000), settleDelay, pk),
            ter(tecNO_DST));
        // can't create channel to the same account
        env(create(alice, alice, XRP(1000), settleDelay, pk),
            ter(temDST_IS_SRC));
        // invalid channel

        env(fund(
                alice,
                channel(alice, "noAccount", env.seq(alice) - 1),
                XRP(1000)),
            ter(tecNO_ENTRY));
        // not enough funds
        env(create(alice, bob, XRP(10000), settleDelay, pk), ter(tecUNFUNDED));

        {
            // No signature claim with bad amounts (negative)
            auto const negXRP = XRP(-100).value();
            auto const posXRP = XRP(100).value();
            env(claim(alice, chan, negXRP, negXRP), ter(temBAD_AMOUNT));
            env(claim(alice, chan, posXRP, negXRP), ter(temBAD_AMOUNT));
            env(claim(alice, chan, negXRP, posXRP), ter(temBAD_AMOUNT));
        }
        {
            // No signature claim more than authorized
            auto const delta = XRP(500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = reqBal + XRP(-100);
            assert(reqBal <= chanAmt);
            env(claim(alice, chan, reqBal, authAmt), ter(temBAD_AMOUNT));
        }
        {
            // No signature needed since the owner is claiming
            auto const preBob = env.balance(bob);
            auto const delta = XRP(500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = reqBal + XRP(100);
            assert(reqBal <= chanAmt);
            env(claim(alice, chan, reqBal, authAmt));
            BEAST_EXPECT(channelBalance(*env.current(), chan) == reqBal);
            BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
            BEAST_EXPECT(env.balance(bob) == preBob + delta);
            chanBal = reqBal;
        }
        {
            // Claim with signature
            auto preBob = env.balance(bob);
            auto const delta = XRP(500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = reqBal + XRP(100);
            assert(reqBal <= chanAmt);
            auto const sig =
                signClaimAuth(alice.pk(), alice.sk(), chan, authAmt);
            env(claim(bob, chan, reqBal, authAmt, Slice(sig), alice.pk()));
            BEAST_EXPECT(channelBalance(*env.current(), chan) == reqBal);
            BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
            auto const feeDrops = env.current()->fees().base;
            BEAST_EXPECT(env.balance(bob) == preBob + delta - feeDrops);
            chanBal = reqBal;

            // claim again
            preBob = env.balance(bob);
            env(claim(bob, chan, reqBal, authAmt, Slice(sig), alice.pk()),
                ter(tecINSUFFICIENT_FUNDS));
            BEAST_EXPECT(channelBalance(*env.current(), chan) == chanBal);
            BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
            BEAST_EXPECT(env.balance(bob) == preBob - feeDrops);
        }
        {
            // Try to claim more than authorized
            auto const preBob = env.balance(bob);
            STAmount const authAmt = chanBal + XRP(500);
            STAmount const reqAmt = authAmt + STAmount{1};
            assert(reqAmt <= chanAmt);
            auto const sig =
                signClaimAuth(alice.pk(), alice.sk(), chan, authAmt);
            env(claim(bob, chan, reqAmt, authAmt, Slice(sig), alice.pk()),
                ter(temBAD_AMOUNT));
            BEAST_EXPECT(channelBalance(*env.current(), chan) == chanBal);
            BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
            BEAST_EXPECT(env.balance(bob) == preBob);
        }

        // Dst tries to fund the channel
        env(fund(bob, chan, XRP(1000)), ter(tecNO_PERMISSION));
        BEAST_EXPECT(channelBalance(*env.current(), chan) == chanBal);
        BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);

        {
            // Wrong signing key
            auto const sig = signClaimAuth(bob.pk(), bob.sk(), chan, XRP(1500));
            env(claim(
                    bob,
                    chan,
                    XRP(1500).value(),
                    XRP(1500).value(),
                    Slice(sig),
                    bob.pk()),
                ter(temBAD_SIGNER));
            BEAST_EXPECT(channelBalance(*env.current(), chan) == chanBal);
            BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
        }
        {
            // Bad signature
            auto const sig = signClaimAuth(bob.pk(), bob.sk(), chan, XRP(1500));
            env(claim(
                    bob,
                    chan,
                    XRP(1500).value(),
                    XRP(1500).value(),
                    Slice(sig),
                    alice.pk()),
                ter(temBAD_SIGNATURE));
            BEAST_EXPECT(channelBalance(*env.current(), chan) == chanBal);
            BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
        }
        {
            // Dst closes channel
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);
            env(claim(bob, chan), txflags(tfClose));
            BEAST_EXPECT(!channelExists(*env.current(), chan));
            auto const feeDrops = env.current()->fees().base;
            auto const delta = chanAmt - chanBal;
            assert(delta > beast::zero);
            BEAST_EXPECT(env.balance(alice) == preAlice + delta);
            BEAST_EXPECT(env.balance(bob) == preBob - feeDrops);
        }
    }

    void
    testDisallowIncoming(FeatureBitset features)
    {
        testcase("Disallow Incoming Flag");
        using namespace jtx;

        // test flag doesn't set unless amendment enabled
        {
            Env env{*this, features - disallowIncoming};
            Account const alice{"alice"};
            env.fund(XRP(10000), alice);
            env(fset(alice, asfDisallowIncomingPayChan));
            env.close();
            auto const sle = env.le(alice);
            uint32_t flags = sle->getFlags();
            BEAST_EXPECT(!(flags & lsfDisallowIncomingPayChan));
        }

        using namespace std::literals::chrono_literals;
        Env env{*this, features | disallowIncoming};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const cho = Account("cho");
        env.fund(XRP(10000), alice, bob, cho);
        auto const pk = alice.pk();
        auto const settleDelay = 100s;

        // set flag on bob only
        env(fset(bob, asfDisallowIncomingPayChan));
        env.close();

        // channel creation from alice to bob is disallowed
        {
            auto const chan = channel(alice, bob, env.seq(alice));
            env(create(alice, bob, XRP(1000), settleDelay, pk),
                ter(tecNO_PERMISSION));
            BEAST_EXPECT(!channelExists(*env.current(), chan));
        }

        // set flag on alice also
        env(fset(alice, asfDisallowIncomingPayChan));
        env.close();

        // channel creation from bob to alice is now disallowed
        {
            auto const chan = channel(bob, alice, env.seq(bob));
            env(create(bob, alice, XRP(1000), settleDelay, pk),
                ter(tecNO_PERMISSION));
            BEAST_EXPECT(!channelExists(*env.current(), chan));
        }

        // remove flag from bob
        env(fclear(bob, asfDisallowIncomingPayChan));
        env.close();

        // now the channel between alice and bob can exist
        {
            auto const chan = channel(alice, bob, env.seq(alice));
            env(create(alice, bob, XRP(1000), settleDelay, pk),
                ter(tesSUCCESS));
            BEAST_EXPECT(channelExists(*env.current(), chan));
        }

        // a channel from cho to alice isn't allowed
        {
            auto const chan = channel(cho, alice, env.seq(cho));
            env(create(cho, alice, XRP(1000), settleDelay, pk),
                ter(tecNO_PERMISSION));
            BEAST_EXPECT(!channelExists(*env.current(), chan));
        }

        // remove flag from alice
        env(fclear(alice, asfDisallowIncomingPayChan));
        env.close();

        // now a channel from cho to alice is allowed
        {
            auto const chan = channel(cho, alice, env.seq(cho));
            env(create(cho, alice, XRP(1000), settleDelay, pk),
                ter(tesSUCCESS));
            BEAST_EXPECT(channelExists(*env.current(), chan));
        }
    }

    void
    testCancelAfter(FeatureBitset features)
    {
        testcase("cancel after");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        {
            // If dst claims after cancel after, channel closes
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob);
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            NetClock::time_point const cancelAfter =
                env.current()->info().parentCloseTime + 3600s;
            auto const channelFunds = XRP(1000);
            auto const chan = channel(alice, bob, env.seq(alice));
            env(create(alice, bob, channelFunds, settleDelay, pk, cancelAfter));
            BEAST_EXPECT(channelExists(*env.current(), chan));
            env.close(cancelAfter);
            {
                // dst cannot claim after cancelAfter
                auto const chanBal = channelBalance(*env.current(), chan);
                auto const chanAmt = channelAmount(*env.current(), chan);
                auto preAlice = env.balance(alice);
                auto preBob = env.balance(bob);
                auto const delta = XRP(500);
                auto const reqBal = chanBal + delta;
                auto const authAmt = reqBal + XRP(100);
                assert(reqBal <= chanAmt);
                auto const sig =
                    signClaimAuth(alice.pk(), alice.sk(), chan, authAmt);
                env(claim(bob, chan, reqBal, authAmt, Slice(sig), alice.pk()));
                auto const feeDrops = env.current()->fees().base;
                BEAST_EXPECT(!channelExists(*env.current(), chan));
                BEAST_EXPECT(env.balance(bob) == preBob - feeDrops);
                BEAST_EXPECT(env.balance(alice) == preAlice + channelFunds);
            }
        }
        {
            // Third party can close after cancel after
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, carol);
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            NetClock::time_point const cancelAfter =
                env.current()->info().parentCloseTime + 3600s;
            auto const channelFunds = XRP(1000);
            auto const chan = channel(alice, bob, env.seq(alice));
            env(create(alice, bob, channelFunds, settleDelay, pk, cancelAfter));
            BEAST_EXPECT(channelExists(*env.current(), chan));
            // third party close before cancelAfter
            env(claim(carol, chan), txflags(tfClose), ter(tecNO_PERMISSION));
            BEAST_EXPECT(channelExists(*env.current(), chan));
            env.close(cancelAfter);
            // third party close after cancelAfter
            auto const preAlice = env.balance(alice);
            env(claim(carol, chan), txflags(tfClose));
            BEAST_EXPECT(!channelExists(*env.current(), chan));
            BEAST_EXPECT(env.balance(alice) == preAlice + channelFunds);
        }
    }

    void
    testExpiration(FeatureBitset features)
    {
        testcase("expiration");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        env.fund(XRP(10000), alice, bob, carol);
        auto const pk = alice.pk();
        auto const settleDelay = 3600s;
        auto const closeTime = env.current()->info().parentCloseTime;
        auto const minExpiration = closeTime + settleDelay;
        NetClock::time_point const cancelAfter = closeTime + 7200s;
        auto const channelFunds = XRP(1000);
        auto const chan = channel(alice, bob, env.seq(alice));
        env(create(alice, bob, channelFunds, settleDelay, pk, cancelAfter));
        BEAST_EXPECT(channelExists(*env.current(), chan));
        BEAST_EXPECT(!channelExpiration(*env.current(), chan));
        // Owner closes, will close after settleDelay
        env(claim(alice, chan), txflags(tfClose));
        auto counts = [](auto const& t) {
            return t.time_since_epoch().count();
        };
        BEAST_EXPECT(
            *channelExpiration(*env.current(), chan) == counts(minExpiration));
        // increase the expiration time
        env(fund(
            alice, chan, XRP(1), NetClock::time_point{minExpiration + 100s}));
        BEAST_EXPECT(
            *channelExpiration(*env.current(), chan) ==
            counts(minExpiration) + 100);
        // decrease the expiration, but still above minExpiration
        env(fund(
            alice, chan, XRP(1), NetClock::time_point{minExpiration + 50s}));
        BEAST_EXPECT(
            *channelExpiration(*env.current(), chan) ==
            counts(minExpiration) + 50);
        // decrease the expiration below minExpiration
        env(fund(
                alice, chan, XRP(1), NetClock::time_point{minExpiration - 50s}),
            ter(temBAD_EXPIRATION));
        BEAST_EXPECT(
            *channelExpiration(*env.current(), chan) ==
            counts(minExpiration) + 50);
        env(claim(bob, chan), txflags(tfRenew), ter(tecNO_PERMISSION));
        BEAST_EXPECT(
            *channelExpiration(*env.current(), chan) ==
            counts(minExpiration) + 50);
        env(claim(alice, chan), txflags(tfRenew));
        BEAST_EXPECT(!channelExpiration(*env.current(), chan));
        // decrease the expiration below minExpiration
        env(fund(
                alice, chan, XRP(1), NetClock::time_point{minExpiration - 50s}),
            ter(temBAD_EXPIRATION));
        BEAST_EXPECT(!channelExpiration(*env.current(), chan));
        env(fund(alice, chan, XRP(1), NetClock::time_point{minExpiration}));
        env.close(minExpiration);
        // Try to extend the expiration after the expiration has already passed
        env(fund(
            alice, chan, XRP(1), NetClock::time_point{minExpiration + 1000s}));
        BEAST_EXPECT(!channelExists(*env.current(), chan));
    }

    void
    testSettleDelay(FeatureBitset features)
    {
        testcase("settle delay");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.fund(XRP(10000), alice, bob);
        auto const pk = alice.pk();
        auto const settleDelay = 3600s;
        NetClock::time_point const settleTimepoint =
            env.current()->info().parentCloseTime + settleDelay;
        auto const channelFunds = XRP(1000);
        auto const chan = channel(alice, bob, env.seq(alice));
        env(create(alice, bob, channelFunds, settleDelay, pk));
        BEAST_EXPECT(channelExists(*env.current(), chan));
        // Owner closes, will close after settleDelay
        env(claim(alice, chan), txflags(tfClose));
        BEAST_EXPECT(channelExists(*env.current(), chan));
        env.close(settleTimepoint - settleDelay / 2);
        {
            // receiver can still claim
            auto const chanBal = channelBalance(*env.current(), chan);
            auto const chanAmt = channelAmount(*env.current(), chan);
            auto preAlice = env.balance(bob);
            auto preBob = env.balance(bob);
            auto const delta = XRP(500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = reqBal + XRP(100);
            assert(reqBal <= chanAmt);
            auto const sig =
                signClaimAuth(alice.pk(), alice.sk(), chan, authAmt);
            env(claim(bob, chan, reqBal, authAmt, Slice(sig), alice.pk()));
            BEAST_EXPECT(channelBalance(*env.current(), chan) == reqBal);
            BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
            auto const feeDrops = env.current()->fees().base;
            BEAST_EXPECT(env.balance(bob) == preBob + delta - feeDrops);
        }
        env.close(settleTimepoint);
        {
            // past settleTime, channel will close
            auto const chanBal = channelBalance(*env.current(), chan);
            auto const chanAmt = channelAmount(*env.current(), chan);
            auto const preAlice = env.balance(alice);
            auto preBob = env.balance(bob);
            auto const delta = XRP(500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = reqBal + XRP(100);
            assert(reqBal <= chanAmt);
            auto const sig =
                signClaimAuth(alice.pk(), alice.sk(), chan, authAmt);
            env(claim(bob, chan, reqBal, authAmt, Slice(sig), alice.pk()));
            BEAST_EXPECT(!channelExists(*env.current(), chan));
            auto const feeDrops = env.current()->fees().base;
            BEAST_EXPECT(env.balance(alice) == preAlice + chanAmt - chanBal);
            BEAST_EXPECT(env.balance(bob) == preBob - feeDrops);
        }
    }

    void
    testCloseDry(FeatureBitset features)
    {
        testcase("close dry");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.fund(XRP(10000), alice, bob);
        auto const pk = alice.pk();
        auto const settleDelay = 3600s;
        auto const channelFunds = XRP(1000);
        auto const chan = channel(alice, bob, env.seq(alice));
        env(create(alice, bob, channelFunds, settleDelay, pk));
        BEAST_EXPECT(channelExists(*env.current(), chan));
        // Owner tries to close channel, but it will remain open (settle delay)
        env(claim(alice, chan), txflags(tfClose));
        BEAST_EXPECT(channelExists(*env.current(), chan));
        {
            // claim the entire amount
            auto const preBob = env.balance(bob);
            env(claim(alice, chan, channelFunds.value(), channelFunds.value()));
            BEAST_EXPECT(channelBalance(*env.current(), chan) == channelFunds);
            BEAST_EXPECT(env.balance(bob) == preBob + channelFunds);
        }
        auto const preAlice = env.balance(alice);
        // Channel is now dry, can close before expiration date
        env(claim(alice, chan), txflags(tfClose));
        BEAST_EXPECT(!channelExists(*env.current(), chan));
        auto const feeDrops = env.current()->fees().base;
        BEAST_EXPECT(env.balance(alice) == preAlice - feeDrops);
    }

    void
    testDefaultAmount(FeatureBitset features)
    {
        // auth amount defaults to balance if not present
        testcase("default amount");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.fund(XRP(10000), alice, bob);
        auto const pk = alice.pk();
        auto const settleDelay = 3600s;
        auto const channelFunds = XRP(1000);
        auto const chan = channel(alice, bob, env.seq(alice));
        env(create(alice, bob, channelFunds, settleDelay, pk));
        BEAST_EXPECT(channelExists(*env.current(), chan));
        // Owner tries to close channel, but it will remain open (settle delay)
        env(claim(alice, chan), txflags(tfClose));
        BEAST_EXPECT(channelExists(*env.current(), chan));
        {
            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            auto const preBob = env.balance(bob);

            auto const delta = XRP(500);
            auto const reqBal = chanBal + delta;
            assert(reqBal <= chanAmt);
            auto const sig =
                signClaimAuth(alice.pk(), alice.sk(), chan, reqBal);
            env(claim(bob, chan, reqBal, std::nullopt, Slice(sig), alice.pk()));
            BEAST_EXPECT(channelBalance(*env.current(), chan) == reqBal);
            auto const feeDrops = env.current()->fees().base;
            BEAST_EXPECT(env.balance(bob) == preBob + delta - feeDrops);
            chanBal = reqBal;
        }
        {
            // Claim again
            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            auto const preBob = env.balance(bob);

            auto const delta = XRP(500);
            auto const reqBal = chanBal + delta;
            assert(reqBal <= chanAmt);
            auto const sig =
                signClaimAuth(alice.pk(), alice.sk(), chan, reqBal);
            env(claim(bob, chan, reqBal, std::nullopt, Slice(sig), alice.pk()));
            BEAST_EXPECT(channelBalance(*env.current(), chan) == reqBal);
            auto const feeDrops = env.current()->fees().base;
            BEAST_EXPECT(env.balance(bob) == preBob + delta - feeDrops);
            chanBal = reqBal;
        }
    }

    void
    testDisallowXRP(FeatureBitset features)
    {
        // auth amount defaults to balance if not present
        testcase("Disallow XRP");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        {
            // Create a channel where dst disallows XRP
            Env env(*this, features - featureDepositAuth);
            env.fund(XRP(10000), alice, bob);
            env(fset(bob, asfDisallowXRP));
            auto const chan = channel(alice, bob, env.seq(alice));
            env(create(alice, bob, XRP(1000), 3600s, alice.pk()),
                ter(tecNO_TARGET));
            BEAST_EXPECT(!channelExists(*env.current(), chan));
        }
        {
            // Create a channel where dst disallows XRP.  Ignore that flag,
            // since it's just advisory.
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob);
            env(fset(bob, asfDisallowXRP));
            auto const chan = channel(alice, bob, env.seq(alice));
            env(create(alice, bob, XRP(1000), 3600s, alice.pk()));
            BEAST_EXPECT(channelExists(*env.current(), chan));
        }

        {
            // Claim to a channel where dst disallows XRP
            // (channel is created before disallow xrp is set)
            Env env(*this, features - featureDepositAuth);
            env.fund(XRP(10000), alice, bob);
            auto const chan = channel(alice, bob, env.seq(alice));
            env(create(alice, bob, XRP(1000), 3600s, alice.pk()));
            BEAST_EXPECT(channelExists(*env.current(), chan));

            env(fset(bob, asfDisallowXRP));
            auto const reqBal = XRP(500).value();
            env(claim(alice, chan, reqBal, reqBal), ter(tecNO_TARGET));
        }
        {
            // Claim to a channel where dst disallows XRP (channel is
            // created before disallow xrp is set).  Ignore that flag
            // since it is just advisory.
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob);
            auto const chan = channel(alice, bob, env.seq(alice));
            env(create(alice, bob, XRP(1000), 3600s, alice.pk()));
            BEAST_EXPECT(channelExists(*env.current(), chan));

            env(fset(bob, asfDisallowXRP));
            auto const reqBal = XRP(500).value();
            env(claim(alice, chan, reqBal, reqBal));
        }
    }

    void
    testDstTag(FeatureBitset features)
    {
        // auth amount defaults to balance if not present
        testcase("Dst Tag");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        // Create a channel where dst disallows XRP
        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.fund(XRP(10000), alice, bob);
        env(fset(bob, asfRequireDest));
        auto const pk = alice.pk();
        auto const settleDelay = 3600s;
        auto const channelFunds = XRP(1000);
        {
            auto const chan = channel(alice, bob, env.seq(alice));
            env(create(alice, bob, channelFunds, settleDelay, pk),
                ter(tecDST_TAG_NEEDED));
            BEAST_EXPECT(!channelExists(*env.current(), chan));
        }
        {
            auto const chan = channel(alice, bob, env.seq(alice));
            env(create(
                alice, bob, channelFunds, settleDelay, pk, std::nullopt, 1));
            BEAST_EXPECT(channelExists(*env.current(), chan));
        }
    }

    void
    testDepositAuth(FeatureBitset features)
    {
        testcase("Deposit Authorization");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        {
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, carol);

            env(fset(bob, asfDepositAuth));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = channel(alice, bob, env.seq(alice));
            env(create(alice, bob, XRP(1000), settleDelay, pk));
            env.close();

            BEAST_EXPECT(channelBalance(*env.current(), chan) == XRP(0));
            BEAST_EXPECT(channelAmount(*env.current(), chan) == XRP(1000));

            // alice can add more funds to the channel even though bob has
            // asfDepositAuth set.
            env(fund(alice, chan, XRP(1000)));
            env.close();

            // alice claims. Fails because bob's lsfDepositAuth flag is set.
            env(claim(alice, chan, XRP(500).value(), XRP(500).value()),
                ter(tecNO_PERMISSION));
            env.close();

            // Claim with signature
            auto const baseFee = env.current()->fees().base;
            auto const preBob = env.balance(bob);
            {
                auto const delta = XRP(500).value();
                auto const sig = signClaimAuth(pk, alice.sk(), chan, delta);

                // alice claims with signature.  Fails since bob has
                // lsfDepositAuth flag set.
                env(claim(alice, chan, delta, delta, Slice(sig), pk),
                    ter(tecNO_PERMISSION));
                env.close();
                BEAST_EXPECT(env.balance(bob) == preBob);

                // bob claims but omits the signature.  Fails because only
                // alice can claim without a signature.
                env(claim(bob, chan, delta, delta), ter(temBAD_SIGNATURE));
                env.close();

                // bob claims with signature.  Succeeds even though bob's
                // lsfDepositAuth flag is set since bob submitted the
                // transaction.
                env(claim(bob, chan, delta, delta, Slice(sig), pk));
                env.close();
                BEAST_EXPECT(env.balance(bob) == preBob + delta - baseFee);
            }
            {
                // Explore the limits of deposit preauthorization.
                auto const delta = XRP(600).value();
                auto const sig = signClaimAuth(pk, alice.sk(), chan, delta);

                // carol claims and fails.  Only channel participants (bob or
                // alice) may claim.
                env(claim(carol, chan, delta, delta, Slice(sig), pk),
                    ter(tecNO_PERMISSION));
                env.close();

                // bob preauthorizes carol for deposit.  But after that carol
                // still can't claim since only channel participants may claim.
                env(deposit::auth(bob, carol));
                env.close();

                env(claim(carol, chan, delta, delta, Slice(sig), pk),
                    ter(tecNO_PERMISSION));

                // Since alice is not preauthorized she also may not claim
                // for bob.
                env(claim(alice, chan, delta, delta, Slice(sig), pk),
                    ter(tecNO_PERMISSION));
                env.close();

                // However if bob preauthorizes alice for deposit then she can
                // successfully submit a claim.
                env(deposit::auth(bob, alice));
                env.close();

                env(claim(alice, chan, delta, delta, Slice(sig), pk));
                env.close();

                BEAST_EXPECT(
                    env.balance(bob) == preBob + delta - (3 * baseFee));
            }
            {
                // bob removes preauthorization of alice.  Once again she
                // cannot submit a claim.
                auto const delta = XRP(800).value();

                env(deposit::unauth(bob, alice));
                env.close();

                // alice claims and fails since she is no longer preauthorized.
                env(claim(alice, chan, delta, delta), ter(tecNO_PERMISSION));
                env.close();

                // bob clears lsfDepositAuth.  Now alice can claim.
                env(fclear(bob, asfDepositAuth));
                env.close();

                // alice claims successfully.
                env(claim(alice, chan, delta, delta));
                env.close();
                BEAST_EXPECT(
                    env.balance(bob) == preBob + XRP(800) - (5 * baseFee));
            }
        }
    }

    void
    testMultiple(FeatureBitset features)
    {
        // auth amount defaults to balance if not present
        testcase("Multiple channels to the same account");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.fund(XRP(10000), alice, bob);
        auto const pk = alice.pk();
        auto const settleDelay = 3600s;
        auto const channelFunds = XRP(1000);
        auto const chan1 = channel(alice, bob, env.seq(alice));
        env(create(alice, bob, channelFunds, settleDelay, pk));
        BEAST_EXPECT(channelExists(*env.current(), chan1));
        auto const chan2 = channel(alice, bob, env.seq(alice));
        env(create(alice, bob, channelFunds, settleDelay, pk));
        BEAST_EXPECT(channelExists(*env.current(), chan2));
        BEAST_EXPECT(chan1 != chan2);
    }

    void
    testAccountChannelsRPC(FeatureBitset features)
    {
        testcase("AccountChannels RPC");

        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const charlie = Account("charlie", KeyType::ed25519);
        env.fund(XRP(10000), alice, bob, charlie);
        auto const pk = alice.pk();
        auto const settleDelay = 3600s;
        auto const channelFunds = XRP(1000);
        auto const chan1Str = to_string(channel(alice, bob, env.seq(alice)));
        env(create(alice, bob, channelFunds, settleDelay, pk));
        env.close();
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
        auto const chan2Str = to_string(channel(alice, bob, env.seq(alice)));
        env(create(alice, bob, channelFunds, settleDelay, pk));
        env.close();
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
    testAccountChannelsRPCMarkers(FeatureBitset features)
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

        Env env{*this, features};
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
                            std::optional<int> limit = std::nullopt,
                            Json::Value const& marker = Json::nullValue,
                            std::optional<test::jtx::Account> const& dst =
                                std::nullopt) {
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
            BEAST_EXPECT(r.isMember(jss::error_message));
        }
    }

    void
    testAccountChannelsRPCSenderOnly(FeatureBitset features)
    {
        // Check that the account_channels command only returns channels owned
        // by the account
        testcase("Account channels RPC owner only");

        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        Env env{*this, features};
        env.fund(XRP(10000), alice, bob);

        // Create a channel from alice to bob and from bob to alice
        // When retrieving alice's channels, it should only retrieve the
        // channels where alice is the source, not the destination
        auto const settleDelay = 3600s;
        auto const channelFunds = XRP(1000);
        env(create(alice, bob, channelFunds, settleDelay, alice.pk()));
        env(create(bob, alice, channelFunds, settleDelay, bob.pk()));

        auto const r = [&] {
            Json::Value jvc;
            jvc[jss::account] = alice.human();

            return env.rpc(
                "json", "account_channels", to_string(jvc))[jss::result];
        }();
        BEAST_EXPECT(r.isMember(jss::channels));
        BEAST_EXPECT(r[jss::channels].size() == 1);
        BEAST_EXPECT(
            r[jss::channels][0u][jss::destination_account].asString() ==
            bob.human());
    }

    void
    testAuthVerifyRPC(FeatureBitset features)
    {
        testcase("PayChan Auth/Verify RPC");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const charlie = Account("charlie", KeyType::ed25519);
        env.fund(XRP(10000), alice, bob, charlie);
        auto const pk = alice.pk();
        auto const settleDelay = 3600s;
        auto const channelFunds = XRP(1000);
        auto const chan1Str = to_string(channel(alice, bob, env.seq(alice)));
        env(create(alice, bob, channelFunds, settleDelay, pk));
        env.close();
        std::string chan1PkStr;
        {
            auto const r =
                env.rpc("account_channels", alice.human(), bob.human());
            BEAST_EXPECT(r[jss::result][jss::channels].size() == 1);
            BEAST_EXPECT(
                r[jss::result][jss::channels][0u][jss::channel_id] == chan1Str);
            BEAST_EXPECT(r[jss::result][jss::validated]);
            chan1PkStr =
                r[jss::result][jss::channels][0u][jss::public_key].asString();
        }
        {
            auto const r = env.rpc("account_channels", alice.human());
            BEAST_EXPECT(r[jss::result][jss::channels].size() == 1);
            BEAST_EXPECT(
                r[jss::result][jss::channels][0u][jss::channel_id] == chan1Str);
            BEAST_EXPECT(r[jss::result][jss::validated]);
            chan1PkStr =
                r[jss::result][jss::channels][0u][jss::public_key].asString();
        }
        {
            auto const r =
                env.rpc("account_channels", bob.human(), alice.human());
            BEAST_EXPECT(r[jss::result][jss::channels].size() == 0);
            BEAST_EXPECT(r[jss::result][jss::validated]);
        }
        auto const chan2Str = to_string(channel(alice, bob, env.seq(alice)));
        env(create(alice, bob, channelFunds, settleDelay, pk));
        env.close();
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
                env.rpc("channel_authorize", "alice", chan1Str, "1000");
            auto const sig = rs[jss::result][jss::signature].asString();
            BEAST_EXPECT(!sig.empty());
            {
                auto const rv = env.rpc(
                    "channel_verify", chan1PkStr, chan1Str, "1000", sig);
                BEAST_EXPECT(rv[jss::result][jss::signature_verified].asBool());
            }

            {
                // use pk hex to verify
                auto const pkAsHex = sliceToHex(pk.slice());
                auto const rv =
                    env.rpc("channel_verify", pkAsHex, chan1Str, "1000", sig);
                BEAST_EXPECT(rv[jss::result][jss::signature_verified].asBool());
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
                rv = env.rpc(
                    "channel_verify", pkAsHex, chan1Str, "1000 1000", sig);
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
                auto rv = env.rpc(
                    "channel_verify", pkAsHex, chan1StrBad, "1000", sig);
                BEAST_EXPECT(rv[jss::error] == "channelMalformed");
                rv = env.rpc("channel_authorize", "alice", chan1StrBad, "1000");
                BEAST_EXPECT(rv[jss::error] == "channelMalformed");

                chan1StrBad = chan1Str;
                chan1StrBad.push_back('0');
                rv = env.rpc(
                    "channel_verify", pkAsHex, chan1StrBad, "1000", sig);
                BEAST_EXPECT(rv[jss::error] == "channelMalformed");
                rv = env.rpc("channel_authorize", "alice", chan1StrBad, "1000");
                BEAST_EXPECT(rv[jss::error] == "channelMalformed");

                chan1StrBad = chan1Str;
                chan1StrBad.back() = 'x';
                rv = env.rpc(
                    "channel_verify", pkAsHex, chan1StrBad, "1000", sig);
                BEAST_EXPECT(rv[jss::error] == "channelMalformed");
                rv = env.rpc("channel_authorize", "alice", chan1StrBad, "1000");
                BEAST_EXPECT(rv[jss::error] == "channelMalformed");
            }
            {
                // give an ill formed base 58 public key
                auto illFormedPk = chan1PkStr.substr(0, chan1PkStr.size() - 1);
                auto const rv = env.rpc(
                    "channel_verify", illFormedPk, chan1Str, "1000", sig);
                BEAST_EXPECT(
                    !rv[jss::result][jss::signature_verified].asBool());
            }
            {
                // give an ill formed hex public key
                auto const pkAsHex = sliceToHex(pk.slice());
                auto illFormedPk = pkAsHex.substr(0, chan1PkStr.size() - 1);
                auto const rv = env.rpc(
                    "channel_verify", illFormedPk, chan1Str, "1000", sig);
                BEAST_EXPECT(
                    !rv[jss::result][jss::signature_verified].asBool());
            }
        }
        {
            // Try to verify chan2 auth with chan1 key
            auto const rs =
                env.rpc("channel_authorize", "alice", chan2Str, "1000");
            auto const sig = rs[jss::result][jss::signature].asString();
            BEAST_EXPECT(!sig.empty());
            {
                auto const rv = env.rpc(
                    "channel_verify", chan1PkStr, chan1Str, "1000", sig);
                BEAST_EXPECT(
                    !rv[jss::result][jss::signature_verified].asBool());
            }
            {
                // use pk hex to verify
                auto const pkAsHex = sliceToHex(pk.slice());
                auto const rv =
                    env.rpc("channel_verify", pkAsHex, chan1Str, "1000", sig);
                BEAST_EXPECT(
                    !rv[jss::result][jss::signature_verified].asBool());
            }
        }
        {
            // Try to explicitly specify secp256k1 and Ed25519 keys:
            auto const chan =
                to_string(channel(charlie, alice, env.seq(charlie)));
            env(create(
                charlie, alice, channelFunds, settleDelay, charlie.pk()));
            env.close();

            std::string cpk;
            {
                auto const r =
                    env.rpc("account_channels", charlie.human(), alice.human());
                BEAST_EXPECT(r[jss::result][jss::channels].size() == 1);
                BEAST_EXPECT(
                    r[jss::result][jss::channels][0u][jss::channel_id] == chan);
                BEAST_EXPECT(r[jss::result][jss::validated]);
                cpk = r[jss::result][jss::channels][0u][jss::public_key]
                          .asString();
            }

            // Try to authorize without specifying a key type, expect an error:
            auto const rs =
                env.rpc("channel_authorize", "charlie", chan, "1000");
            auto const sig = rs[jss::result][jss::signature].asString();
            BEAST_EXPECT(!sig.empty());
            {
                auto const rv =
                    env.rpc("channel_verify", cpk, chan, "1000", sig);
                BEAST_EXPECT(
                    !rv[jss::result][jss::signature_verified].asBool());
            }

            // Try to authorize using an unknown key type, except an error:
            auto const rs1 =
                env.rpc("channel_authorize", "charlie", "nyx", chan, "1000");
            BEAST_EXPECT(rs1[jss::error] == "badKeyType");

            // Try to authorize using secp256k1; the authorization _should_
            // succeed but the verification should fail:
            auto const rs2 = env.rpc(
                "channel_authorize", "charlie", "secp256k1", chan, "1000");
            auto const sig2 = rs2[jss::result][jss::signature].asString();
            BEAST_EXPECT(!sig2.empty());
            {
                auto const rv =
                    env.rpc("channel_verify", cpk, chan, "1000", sig2);
                BEAST_EXPECT(
                    !rv[jss::result][jss::signature_verified].asBool());
            }

            // Try to authorize using Ed25519; expect success:
            auto const rs3 = env.rpc(
                "channel_authorize", "charlie", "ed25519", chan, "1000");
            auto const sig3 = rs3[jss::result][jss::signature].asString();
            BEAST_EXPECT(!sig3.empty());
            {
                auto const rv =
                    env.rpc("channel_verify", cpk, chan, "1000", sig3);
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
                Json::Value args{Json::objectValue};
                args[jss::amount] = "2000";
                args[jss::key_type] = "secp256k1";
                args[jss::passphrase] = "passphrase_can_be_anything";
                rs = env.rpc(
                    "json",
                    "channel_authorize",
                    args.toStyledString())[jss::result];
                BEAST_EXPECT(rs[jss::error] == "invalidParams");
            }
            {
                // Missing amount
                Json::Value args{Json::objectValue};
                args[jss::channel_id] = chan1Str;
                args[jss::key_type] = "secp256k1";
                args[jss::passphrase] = "passphrase_can_be_anything";
                rs = env.rpc(
                    "json",
                    "channel_authorize",
                    args.toStyledString())[jss::result];
                BEAST_EXPECT(rs[jss::error] == "invalidParams");
            }
            {
                // Missing key_type and no secret.
                Json::Value args{Json::objectValue};
                args[jss::amount] = "2000";
                args[jss::channel_id] = chan1Str;
                args[jss::passphrase] = "passphrase_can_be_anything";
                rs = env.rpc(
                    "json",
                    "channel_authorize",
                    args.toStyledString())[jss::result];
                BEAST_EXPECT(rs[jss::error] == "invalidParams");
            }
            {
                // Both passphrase and seed specified.
                Json::Value args{Json::objectValue};
                args[jss::amount] = "2000";
                args[jss::channel_id] = chan1Str;
                args[jss::key_type] = "secp256k1";
                args[jss::passphrase] = "passphrase_can_be_anything";
                args[jss::seed] = "seed can be anything";
                rs = env.rpc(
                    "json",
                    "channel_authorize",
                    args.toStyledString())[jss::result];
                BEAST_EXPECT(rs[jss::error] == "invalidParams");
            }
            {
                // channel_id is not exact hex.
                Json::Value args{Json::objectValue};
                args[jss::amount] = "2000";
                args[jss::channel_id] = chan1Str + "1";
                args[jss::key_type] = "secp256k1";
                args[jss::passphrase] = "passphrase_can_be_anything";
                rs = env.rpc(
                    "json",
                    "channel_authorize",
                    args.toStyledString())[jss::result];
                BEAST_EXPECT(rs[jss::error] == "channelMalformed");
            }
            {
                // amount is not a string
                Json::Value args{Json::objectValue};
                args[jss::amount] = 2000;
                args[jss::channel_id] = chan1Str;
                args[jss::key_type] = "secp256k1";
                args[jss::passphrase] = "passphrase_can_be_anything";
                rs = env.rpc(
                    "json",
                    "channel_authorize",
                    args.toStyledString())[jss::result];
                BEAST_EXPECT(rs[jss::error] == "channelAmtMalformed");
            }
            {
                // Amount is not a decimal string.
                Json::Value args{Json::objectValue};
                args[jss::amount] = "TwoThousand";
                args[jss::channel_id] = chan1Str;
                args[jss::key_type] = "secp256k1";
                args[jss::passphrase] = "passphrase_can_be_anything";
                rs = env.rpc(
                    "json",
                    "channel_authorize",
                    args.toStyledString())[jss::result];
                BEAST_EXPECT(rs[jss::error] == "channelAmtMalformed");
            }
            {
                // Amount is string. tecSUCCESS
                Json::Value args{Json::objectValue};
                args[jss::amount] = "1000";
                args[jss::channel_id] = chan1Str;
                args[jss::key_type] = "secp256k1";
                args[jss::passphrase] = "passphrase_can_be_anything";
                rs = env.rpc(
                    "json",
                    "channel_authorize",
                    args.toStyledString())[jss::result];
            }
        }
    }

    void
    testOptionalFields(FeatureBitset features)
    {
        testcase("Optional Fields");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const dan = Account("dan");
        env.fund(XRP(10000), alice, bob, carol, dan);
        auto const pk = alice.pk();
        auto const settleDelay = 3600s;
        auto const channelFunds = XRP(1000);

        std::optional<NetClock::time_point> cancelAfter;

        {
            auto const chan = to_string(channel(alice, bob, env.seq(alice)));
            env(create(alice, bob, channelFunds, settleDelay, pk));
            auto const r =
                env.rpc("account_channels", alice.human(), bob.human());
            BEAST_EXPECT(r[jss::result][jss::channels].size() == 1);
            BEAST_EXPECT(
                r[jss::result][jss::channels][0u][jss::channel_id] == chan);
            BEAST_EXPECT(!r[jss::result][jss::channels][0u].isMember(
                jss::destination_tag));
        }
        {
            std::uint32_t dstTag = 42;
            auto const chan = to_string(channel(alice, carol, env.seq(alice)));
            env(create(
                alice,
                carol,
                channelFunds,
                settleDelay,
                pk,
                cancelAfter,
                dstTag));
            auto const r =
                env.rpc("account_channels", alice.human(), carol.human());
            BEAST_EXPECT(r[jss::result][jss::channels].size() == 1);
            BEAST_EXPECT(
                r[jss::result][jss::channels][0u][jss::channel_id] == chan);
            BEAST_EXPECT(
                r[jss::result][jss::channels][0u][jss::destination_tag] ==
                dstTag);
        }
    }

    void
    testMalformedPK(FeatureBitset features)
    {
        testcase("malformed pk");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.fund(XRP(10000), alice, bob);
        auto const pk = alice.pk();
        auto const settleDelay = 100s;

        auto const chan = channel(alice, bob, env.seq(alice));
        auto jv = create(alice, bob, XRP(1000), settleDelay, pk);
        auto const pkHex = strHex(pk.slice());
        jv["PublicKey"] = pkHex.substr(2, pkHex.size() - 2);
        env(jv, ter(temMALFORMED));
        jv["PublicKey"] = pkHex.substr(0, pkHex.size() - 2);
        env(jv, ter(temMALFORMED));
        auto badPrefix = pkHex;
        badPrefix[0] = 'f';
        badPrefix[1] = 'f';
        jv["PublicKey"] = badPrefix;
        env(jv, ter(temMALFORMED));

        jv["PublicKey"] = pkHex;
        env(jv);

        auto const authAmt = XRP(100);
        auto const sig = signClaimAuth(alice.pk(), alice.sk(), chan, authAmt);
        jv = claim(
            bob,
            chan,
            authAmt.value(),
            authAmt.value(),
            Slice(sig),
            alice.pk());
        jv["PublicKey"] = pkHex.substr(2, pkHex.size() - 2);
        env(jv, ter(temMALFORMED));
        jv["PublicKey"] = pkHex.substr(0, pkHex.size() - 2);
        env(jv, ter(temMALFORMED));
        badPrefix = pkHex;
        badPrefix[0] = 'f';
        badPrefix[1] = 'f';
        jv["PublicKey"] = badPrefix;
        env(jv, ter(temMALFORMED));

        // missing public key
        jv.removeMember("PublicKey");
        env(jv, ter(temMALFORMED));

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
    testMetaAndOwnership(FeatureBitset features)
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
            return std::find(ownerDir.begin(), ownerDir.end(), chan) !=
                ownerDir.end();
        };

        auto ownerDirCount = [](ReadView const& view,
                                Account const& acc) -> std::size_t {
            ripple::Dir const ownerDir(view, keylet::ownerDir(acc.id()));
            return std::distance(ownerDir.begin(), ownerDir.end());
        };

        {
            // Test without adding the paychan to the recipient's owner
            // directory
            Env env(*this, features - fixPayChanRecipientOwnerDir);
            env.fund(XRP(10000), alice, bob);
            env(create(alice, bob, XRP(1000), settleDelay, pk));
            env.close();
            auto const [chan, chanSle] =
                channelKeyAndSle(*env.current(), alice, bob);
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
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob);
            env(create(alice, bob, XRP(1000), settleDelay, pk));
            env.close();
            auto const [chan, chanSle] =
                channelKeyAndSle(*env.current(), alice, bob);
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
            Env env(*this, features - fixPayChanRecipientOwnerDir);
            env.fund(XRP(10000), alice, bob);
            // create the channel before the amendment activates
            env(create(alice, bob, XRP(1000), settleDelay, pk));
            env.close();
            auto const [chan, chanSle] =
                channelKeyAndSle(*env.current(), alice, bob);
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
    testAccountDelete(FeatureBitset features)
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
            {
            }

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
                ? features
                : features - fixPayChanRecipientOwnerDir;
            Env env{*this, amd};
            env.fund(XRP(10000), alice, bob, carol);
            env.close();
            auto const feeDrops = env.current()->fees().base;

            // Create a channel from alice to bob
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = channel(alice, bob, env.seq(alice));
            env(create(alice, bob, XRP(1000), settleDelay, pk));
            env.close();
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
            Env env{*this, features - fixPayChanRecipientOwnerDir};
            env.fund(XRP(10000), alice, bob, carol);
            env.close();
            auto const feeDrops = env.current()->fees().base;

            // Create a channel from alice to bob
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = channel(alice, bob, env.seq(alice));
            env(create(alice, bob, XRP(1000), settleDelay, pk));
            env.close();
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
    testUsingTickets(FeatureBitset features)
    {
        testcase("using tickets");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.fund(XRP(10000), alice, bob);

        // alice and bob grab enough tickets for all of the following
        // transactions.  Note that once the tickets are acquired alice's
        // and bob's account sequence numbers should not advance.
        std::uint32_t aliceTicketSeq{env.seq(alice) + 1};
        env(ticket::create(alice, 10));
        std::uint32_t const aliceSeq{env.seq(alice)};

        std::uint32_t bobTicketSeq{env.seq(bob) + 1};
        env(ticket::create(bob, 10));
        std::uint32_t const bobSeq{env.seq(bob)};

        auto const pk = alice.pk();
        auto const settleDelay = 100s;
        auto const chan = channel(alice, bob, aliceTicketSeq);

        env(create(alice, bob, XRP(1000), settleDelay, pk),
            ticket::use(aliceTicketSeq++));

        env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        BEAST_EXPECT(channelBalance(*env.current(), chan) == XRP(0));
        BEAST_EXPECT(channelAmount(*env.current(), chan) == XRP(1000));

        {
            auto const preAlice = env.balance(alice);
            env(fund(alice, chan, XRP(1000)), ticket::use(aliceTicketSeq++));

            env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
            BEAST_EXPECT(env.seq(alice) == aliceSeq);

            auto const feeDrops = env.current()->fees().base;
            BEAST_EXPECT(env.balance(alice) == preAlice - XRP(1000) - feeDrops);
        }

        auto chanBal = channelBalance(*env.current(), chan);
        auto chanAmt = channelAmount(*env.current(), chan);
        BEAST_EXPECT(chanBal == XRP(0));
        BEAST_EXPECT(chanAmt == XRP(2000));

        {
            // No signature needed since the owner is claiming
            auto const preBob = env.balance(bob);
            auto const delta = XRP(500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = reqBal + XRP(100);
            assert(reqBal <= chanAmt);
            env(claim(alice, chan, reqBal, authAmt),
                ticket::use(aliceTicketSeq++));

            env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
            BEAST_EXPECT(env.seq(alice) == aliceSeq);

            BEAST_EXPECT(channelBalance(*env.current(), chan) == reqBal);
            BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
            BEAST_EXPECT(env.balance(bob) == preBob + delta);
            chanBal = reqBal;
        }
        {
            // Claim with signature
            auto preBob = env.balance(bob);
            auto const delta = XRP(500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = reqBal + XRP(100);
            assert(reqBal <= chanAmt);
            auto const sig =
                signClaimAuth(alice.pk(), alice.sk(), chan, authAmt);
            env(claim(bob, chan, reqBal, authAmt, Slice(sig), alice.pk()),
                ticket::use(bobTicketSeq++));

            env.require(tickets(bob, env.seq(bob) - bobTicketSeq));
            BEAST_EXPECT(env.seq(bob) == bobSeq);

            BEAST_EXPECT(channelBalance(*env.current(), chan) == reqBal);
            BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
            auto const feeDrops = env.current()->fees().base;
            BEAST_EXPECT(env.balance(bob) == preBob + delta - feeDrops);
            chanBal = reqBal;

            // claim again
            preBob = env.balance(bob);
            // A transaction that generates a tec still consumes its ticket.
            env(claim(bob, chan, reqBal, authAmt, Slice(sig), alice.pk()),
                ticket::use(bobTicketSeq++),
                ter(tecINSUFFICIENT_FUNDS));

            env.require(tickets(bob, env.seq(bob) - bobTicketSeq));
            BEAST_EXPECT(env.seq(bob) == bobSeq);

            BEAST_EXPECT(channelBalance(*env.current(), chan) == chanBal);
            BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
            BEAST_EXPECT(env.balance(bob) == preBob - feeDrops);
        }
        {
            // Try to claim more than authorized
            auto const preBob = env.balance(bob);
            STAmount const authAmt = chanBal + XRP(500);
            STAmount const reqAmt = authAmt + drops(1);
            assert(reqAmt <= chanAmt);
            // Note that since claim() returns a tem (neither tec nor tes),
            // the ticket is not consumed.  So we don't increment bobTicket.
            auto const sig =
                signClaimAuth(alice.pk(), alice.sk(), chan, authAmt);
            env(claim(bob, chan, reqAmt, authAmt, Slice(sig), alice.pk()),
                ticket::use(bobTicketSeq),
                ter(temBAD_AMOUNT));

            env.require(tickets(bob, env.seq(bob) - bobTicketSeq));
            BEAST_EXPECT(env.seq(bob) == bobSeq);

            BEAST_EXPECT(channelBalance(*env.current(), chan) == chanBal);
            BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
            BEAST_EXPECT(env.balance(bob) == preBob);
        }

        // Dst tries to fund the channel
        env(fund(bob, chan, XRP(1000)),
            ticket::use(bobTicketSeq++),
            ter(tecNO_PERMISSION));

        env.require(tickets(bob, env.seq(bob) - bobTicketSeq));
        BEAST_EXPECT(env.seq(bob) == bobSeq);

        BEAST_EXPECT(channelBalance(*env.current(), chan) == chanBal);
        BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);

        {
            // Dst closes channel
            auto const preAlice = env.balance(alice);
            auto const preBob = env.balance(bob);
            env(claim(bob, chan),
                txflags(tfClose),
                ticket::use(bobTicketSeq++));

            env.require(tickets(bob, env.seq(bob) - bobTicketSeq));
            BEAST_EXPECT(env.seq(bob) == bobSeq);

            BEAST_EXPECT(!channelExists(*env.current(), chan));
            auto const feeDrops = env.current()->fees().base;
            auto const delta = chanAmt - chanBal;
            assert(delta > beast::zero);
            BEAST_EXPECT(env.balance(alice) == preAlice + delta);
            BEAST_EXPECT(env.balance(bob) == preBob - feeDrops);
        }
        env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
        BEAST_EXPECT(env.seq(alice) == aliceSeq);
        env.require(tickets(bob, env.seq(bob) - bobTicketSeq));
        BEAST_EXPECT(env.seq(bob) == bobSeq);
    }

    void
    testICSimple(FeatureBitset features)
    {
        testcase("ic simple");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(10000), alice, bob, carol, gw);
        env.close();
        env.trust(USD(100000), alice, bob);
        env.close();
        env(pay(gw, alice, USD(10000)));
        env(pay(gw, bob, USD(10000)));
        env.close();

        auto const pk = alice.pk();
        auto const settleDelay = 100s;
        auto const chan = channel(alice, bob, env.seq(alice));
        env(create(alice, bob, USD(1000), settleDelay, pk));
        BEAST_EXPECT(channelBalance(*env.current(), chan) == USD(0));
        BEAST_EXPECT(channelAmount(*env.current(), chan) == USD(1000));

        {
            auto const preAlice = env.balance(alice, USD.issue());
            auto const preAliceXrp = env.balance(alice);
            auto const preLocked = -lockedAmount(env, alice, gw, USD);
            env(fund(alice, chan, USD(1000)));
            auto const feeDrops = env.current()->fees().base;
            auto const postLocked = -lockedAmount(env, alice, gw, USD);
            BEAST_EXPECT(env.balance(alice) == preAliceXrp - feeDrops);
            BEAST_EXPECT(
                env.balance(alice, USD.issue()) ==
                (preAlice.value() - preLocked) + USD(1000));
            BEAST_EXPECT(postLocked == preLocked + USD(1000));
        }

        auto chanBal = channelBalance(*env.current(), chan);
        auto chanAmt = channelAmount(*env.current(), chan);
        BEAST_EXPECT(chanBal == USD(0));
        BEAST_EXPECT(chanAmt == USD(2000));

        {
            // bad amounts (negative amounts)
            env(create(alice, bob, USD(-1000), settleDelay, pk),
                ter(temBAD_AMOUNT));
            env(fund(alice, chan, USD(-1000)), ter(temBAD_AMOUNT));
        }

        // invalid account
        env(create(alice, "noAccount", USD(1000), settleDelay, pk),
            ter(tecNO_DST));
        // can't create channel to the same account
        env(create(alice, alice, USD(1000), settleDelay, pk),
            ter(temDST_IS_SRC));
        // invalid channel

        env(fund(
                alice,
                channel(alice, "noAccount", env.seq(alice) - 1),
                USD(1000)),
            ter(tecNO_ENTRY));
        // not enough funds
        env(create(alice, bob, USD(10000), settleDelay, pk),
            ter(tecINSUFFICIENT_FUNDS));

        {
            // No signature claim with bad amounts (negative)
            auto const negIC = USD(-100).value();
            auto const posIC = USD(100).value();
            env(claim(alice, chan, negIC, negIC), ter(temBAD_AMOUNT));
            env(claim(alice, chan, posIC, negIC), ter(temBAD_AMOUNT));
            env(claim(alice, chan, negIC, posIC), ter(temBAD_AMOUNT));
        }
        {
            // No signature claim more than authorized
            auto const delta = USD(500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = reqBal + USD(-100);
            assert(reqBal <= chanAmt);
            env(claim(alice, chan, reqBal, authAmt), ter(temBAD_AMOUNT));
        }
        {
            // No signature needed since the owner is claiming
            auto const preBob = env.balance(bob, USD.issue());
            auto const preLocked = -lockedAmount(env, alice, gw, USD);
            auto const delta = USD(500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = reqBal + USD(100);
            assert(reqBal <= chanAmt);
            env(claim(alice, chan, reqBal, authAmt));
            auto const postLocked = -lockedAmount(env, alice, gw, USD);
            BEAST_EXPECT(channelBalance(*env.current(), chan) == reqBal);
            BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
            BEAST_EXPECT(
                env.balance(bob, USD.issue()).value() ==
                preBob.value() + delta);
            BEAST_EXPECT(postLocked == preLocked - delta);
            chanBal = reqBal;
        }
        {
            // Claim with signature
            auto preBob = env.balance(bob, USD.issue());
            auto preLocked = -lockedAmount(env, alice, gw, USD);
            auto const delta = USD(500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = reqBal + USD(100);
            assert(reqBal <= chanAmt);
            auto const sig =
                signClaimICAuth(alice.pk(), alice.sk(), chan, authAmt);
            env(claim(bob, chan, reqBal, authAmt, Slice(sig), alice.pk()));
            auto postLocked = -lockedAmount(env, alice, gw, USD);
            BEAST_EXPECT(channelBalance(*env.current(), chan) == reqBal);
            BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
            BEAST_EXPECT(
                env.balance(bob, USD.issue()).value() ==
                preBob.value() + delta);
            BEAST_EXPECT(postLocked == preLocked - delta);
            chanBal = reqBal;

            // claim again
            preBob = env.balance(bob, USD.issue());
            preLocked = -lockedAmount(env, alice, gw, USD);
            env(claim(bob, chan, reqBal, authAmt, Slice(sig), alice.pk()),
                ter(tecINSUFFICIENT_FUNDS));
            postLocked = -lockedAmount(env, alice, gw, USD);
            BEAST_EXPECT(channelBalance(*env.current(), chan) == chanBal);
            BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
            BEAST_EXPECT(env.balance(bob, USD.issue()) == preBob.value());
            BEAST_EXPECT(postLocked == preLocked);
        }
        {
            // Try to claim more than authorized
            auto const preBob = env.balance(bob, USD.issue());
            auto const preLocked = -lockedAmount(env, alice, gw, USD);
            STAmount const authAmt = chanBal + USD(500);
            STAmount const reqAmt = authAmt + USD(1);
            assert(reqAmt <= chanAmt);
            auto const sig =
                signClaimICAuth(alice.pk(), alice.sk(), chan, authAmt);
            env(claim(bob, chan, reqAmt, authAmt, Slice(sig), alice.pk()),
                ter(temBAD_AMOUNT));
            auto const postLocked = -lockedAmount(env, alice, gw, USD);
            BEAST_EXPECT(channelBalance(*env.current(), chan) == chanBal);
            BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
            BEAST_EXPECT(env.balance(bob, USD.issue()) == preBob.value());
            BEAST_EXPECT(postLocked == preLocked);
        }

        // Dst tries to fund the channel
        env(fund(bob, chan, USD(1000)), ter(tecNO_PERMISSION));
        BEAST_EXPECT(channelBalance(*env.current(), chan) == chanBal);
        BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);

        {
            // Wrong signing key
            auto const sig =
                signClaimICAuth(bob.pk(), bob.sk(), chan, USD(1500));
            env(claim(
                    bob,
                    chan,
                    USD(1500).value(),
                    USD(1500).value(),
                    Slice(sig),
                    bob.pk()),
                ter(temBAD_SIGNER));
            BEAST_EXPECT(channelBalance(*env.current(), chan) == chanBal);
            BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
        }
        {
            // Bad signature
            auto const sig =
                signClaimICAuth(bob.pk(), bob.sk(), chan, USD(1500));
            env(claim(
                    bob,
                    chan,
                    USD(1500).value(),
                    USD(1500).value(),
                    Slice(sig),
                    alice.pk()),
                ter(temBAD_SIGNATURE));
            BEAST_EXPECT(channelBalance(*env.current(), chan) == chanBal);
            BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
        }
        {
            // Dst closes channel
            auto const preAlice = env.balance(alice, USD.issue());
            auto const preBob = env.balance(bob, USD.issue());
            auto const preBobXrp = env.balance(bob);
            auto const preLocked = -lockedAmount(env, alice, gw, USD);
            env(claim(bob, chan), txflags(tfClose));
            BEAST_EXPECT(!channelExists(*env.current(), chan));
            auto const feeDrops = env.current()->fees().base;
            auto const postLocked = -lockedAmount(env, alice, gw, USD);
            auto const delta = chanAmt - chanBal;
            assert(delta > beast::zero);
            BEAST_EXPECT(
                env.balance(alice, USD.issue()) ==
                (preAlice.value() - preLocked) + delta);
            BEAST_EXPECT(env.balance(bob) == preBobXrp - feeDrops);
            BEAST_EXPECT(env.balance(bob, USD.issue()) == preBob.value());
            BEAST_EXPECT(postLocked == USD(0));
        }
    }

    void
    testICCancelAfter(FeatureBitset features)
    {
        testcase("ic cancel after");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        {
            // If dst claims after cancel after, channel closes
            Env env{*this, features};

            env.fund(XRP(10000), alice, bob, gw);
            env.close();
            env.trust(USD(100000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            NetClock::time_point const cancelAfter =
                env.current()->info().parentCloseTime + 3600s;
            auto const channelFunds = USD(1000);
            auto const chan = channel(alice, bob, env.seq(alice));
            env(create(alice, bob, channelFunds, settleDelay, pk, cancelAfter));
            BEAST_EXPECT(channelExists(*env.current(), chan));

            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            BEAST_EXPECT(chanBal == USD(0));
            BEAST_EXPECT(chanAmt == USD(1000));

            env.close(cancelAfter);
            {
                // dst cannot claim after cancelAfter
                auto const chanBal = channelBalance(*env.current(), chan);
                auto const chanAmt = channelAmount(*env.current(), chan);
                auto preAlice = env.balance(alice, USD.issue());
                auto preBobXrp = env.balance(bob);
                auto const preLocked = -lockedAmount(env, alice, gw, USD);
                auto const delta = USD(500);
                auto const reqBal = chanBal + delta;
                auto const authAmt = reqBal + USD(100);
                assert(reqBal <= chanAmt);
                auto const sig =
                    signClaimICAuth(alice.pk(), alice.sk(), chan, authAmt);
                env(claim(bob, chan, reqBal, authAmt, Slice(sig), alice.pk()));
                auto const feeDrops = env.current()->fees().base;
                auto const postLocked = -lockedAmount(env, alice, gw, USD);
                BEAST_EXPECT(!channelExists(*env.current(), chan));
                BEAST_EXPECT(env.balance(bob) == preBobXrp - feeDrops);
                BEAST_EXPECT(
                    env.balance(alice, USD.issue()) ==
                    (preAlice.value() - preLocked) + channelFunds);
                BEAST_EXPECT(preLocked == channelFunds);
                BEAST_EXPECT(postLocked == USD(0));
            }
        }
        {
            // Third party can close after cancel after
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, carol, gw);
            env.close();
            env.trust(USD(100000), alice, bob, carol);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env(pay(gw, carol, USD(10000)));
            env.close();
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            NetClock::time_point const cancelAfter =
                env.current()->info().parentCloseTime + 3600s;
            auto const channelFunds = USD(1000);
            auto const chan = channel(alice, bob, env.seq(alice));
            env(create(alice, bob, channelFunds, settleDelay, pk, cancelAfter));
            BEAST_EXPECT(channelExists(*env.current(), chan));
            // third party close before cancelAfter
            env(claim(carol, chan), txflags(tfClose), ter(tecNO_PERMISSION));
            BEAST_EXPECT(channelExists(*env.current(), chan));
            env.close(cancelAfter);
            // third party close after cancelAfter
            auto const preAlice = env.balance(alice, USD.issue());
            auto const preLocked = -lockedAmount(env, alice, gw, USD);
            env(claim(carol, chan), txflags(tfClose));
            auto const postLocked = -lockedAmount(env, alice, gw, USD);
            BEAST_EXPECT(!channelExists(*env.current(), chan));
            BEAST_EXPECT(preLocked == channelFunds);
            BEAST_EXPECT(postLocked == USD(0));
            BEAST_EXPECT(
                env.balance(alice, USD.issue()).value() ==
                (preAlice.value() - preLocked) + channelFunds);
        }
    }

    void
    testICSettleDelay(FeatureBitset features)
    {
        testcase("ic settle delay");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(USD(100000), alice, bob);
        env.close();
        env(pay(gw, alice, USD(10000)));
        env(pay(gw, bob, USD(10000)));
        env.close();

        auto const pk = alice.pk();
        auto const settleDelay = 3600s;
        NetClock::time_point const settleTimepoint =
            env.current()->info().parentCloseTime + settleDelay;
        auto const channelFunds = USD(1000);
        auto const chan = channel(alice, bob, env.seq(alice));
        env(create(alice, bob, channelFunds, settleDelay, pk));
        BEAST_EXPECT(channelExists(*env.current(), chan));
        // Owner closes, will close after settleDelay
        env(claim(alice, chan), txflags(tfClose));
        BEAST_EXPECT(channelExists(*env.current(), chan));
        env.close(settleTimepoint - settleDelay / 2);
        {
            // receiver can still claim
            auto const chanBal = channelBalance(*env.current(), chan);
            auto const chanAmt = channelAmount(*env.current(), chan);
            auto preAlice = env.balance(alice, USD.issue());
            auto preBob = env.balance(bob, USD.issue());
            auto preBobXrp = env.balance(bob);
            auto const preLocked = -lockedAmount(env, alice, gw, USD);
            auto const delta = USD(500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = reqBal + USD(100);
            assert(reqBal <= chanAmt);
            auto const sig =
                signClaimICAuth(alice.pk(), alice.sk(), chan, authAmt);
            env(claim(bob, chan, reqBal, authAmt, Slice(sig), alice.pk()));
            BEAST_EXPECT(channelBalance(*env.current(), chan) == reqBal);
            BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
            auto const feeDrops = env.current()->fees().base;
            BEAST_EXPECT(env.balance(bob) == preBobXrp - feeDrops);
            BEAST_EXPECT(
                env.balance(bob, USD.issue()).value() ==
                preBob.value() + delta);
            BEAST_EXPECT(preLocked == channelFunds);
            auto const postLocked = -lockedAmount(env, alice, gw, USD);
            BEAST_EXPECT(postLocked == delta);
        }
        env.close(settleTimepoint);
        {
            // past settleTime, channel will close
            auto const chanBal = channelBalance(*env.current(), chan);
            auto const chanAmt = channelAmount(*env.current(), chan);
            auto const preAlice = env.balance(alice, USD.issue());
            auto preBob = env.balance(bob, USD.issue());
            auto preBobXRP = env.balance(bob);
            auto const preLocked = -lockedAmount(env, alice, gw, USD);
            auto const delta = USD(500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = reqBal + USD(100);
            assert(reqBal <= chanAmt);
            auto const sig =
                signClaimICAuth(alice.pk(), alice.sk(), chan, authAmt);
            env(claim(bob, chan, reqBal, authAmt, Slice(sig), alice.pk()));
            auto const feeDrops = env.current()->fees().base;
            auto const postLocked = -lockedAmount(env, alice, gw, USD);
            BEAST_EXPECT(!channelExists(*env.current(), chan));
            BEAST_EXPECT(postLocked == USD(0));
            BEAST_EXPECT(env.balance(bob) == preBobXRP - feeDrops);
            BEAST_EXPECT(
                env.balance(alice, USD.issue()) ==
                (preAlice - preLocked) + chanAmt - chanBal);
        }
    }

    void
    testICExpiration(FeatureBitset features)
    {
        testcase("ic expiration");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(10000), alice, bob, carol, gw);
        env.close();
        env.trust(USD(100000), alice, bob, carol);
        env.close();
        env(pay(gw, alice, USD(10000)));
        env(pay(gw, bob, USD(10000)));
        env(pay(gw, carol, USD(10000)));
        env.close();
        auto const pk = alice.pk();
        auto const settleDelay = 3600s;
        auto const closeTime = env.current()->info().parentCloseTime;
        auto const minExpiration = closeTime + settleDelay;
        NetClock::time_point const cancelAfter = closeTime + 7200s;
        auto const channelFunds = USD(1000);
        auto const chan = channel(alice, bob, env.seq(alice));
        env(create(alice, bob, channelFunds, settleDelay, pk, cancelAfter));
        BEAST_EXPECT(channelExists(*env.current(), chan));
        BEAST_EXPECT(!channelExpiration(*env.current(), chan));
        // Owner closes, will close after settleDelay
        env(claim(alice, chan), txflags(tfClose));
        auto counts = [](auto const& t) {
            return t.time_since_epoch().count();
        };
        BEAST_EXPECT(
            *channelExpiration(*env.current(), chan) == counts(minExpiration));
        // increase the expiration time
        env(fund(
            alice, chan, USD(1), NetClock::time_point{minExpiration + 100s}));
        BEAST_EXPECT(
            *channelExpiration(*env.current(), chan) ==
            counts(minExpiration) + 100);
        // decrease the expiration, but still above minExpiration
        env(fund(
            alice, chan, USD(1), NetClock::time_point{minExpiration + 50s}));
        BEAST_EXPECT(
            *channelExpiration(*env.current(), chan) ==
            counts(minExpiration) + 50);
        // decrease the expiration below minExpiration
        env(fund(
                alice, chan, USD(1), NetClock::time_point{minExpiration - 50s}),
            ter(temBAD_EXPIRATION));
        BEAST_EXPECT(
            *channelExpiration(*env.current(), chan) ==
            counts(minExpiration) + 50);
        env(claim(bob, chan), txflags(tfRenew), ter(tecNO_PERMISSION));
        BEAST_EXPECT(
            *channelExpiration(*env.current(), chan) ==
            counts(minExpiration) + 50);
        env(claim(alice, chan), txflags(tfRenew));
        BEAST_EXPECT(!channelExpiration(*env.current(), chan));
        // decrease the expiration below minExpiration
        env(fund(
                alice, chan, USD(1), NetClock::time_point{minExpiration - 50s}),
            ter(temBAD_EXPIRATION));
        BEAST_EXPECT(!channelExpiration(*env.current(), chan));
        env(fund(alice, chan, USD(1), NetClock::time_point{minExpiration}));
        env.close(minExpiration);
        // Try to extend the expiration after the expiration has already passed
        env(fund(
            alice, chan, USD(1), NetClock::time_point{minExpiration + 1000s}));
        BEAST_EXPECT(!channelExists(*env.current(), chan));
    }

    void
    testICCloseDry(FeatureBitset features)
    {
        testcase("ic close dry");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(USD(100000), alice, bob);
        env.close();
        env(pay(gw, alice, USD(10000)));
        env(pay(gw, bob, USD(10000)));
        env.close();
        auto const pk = alice.pk();
        auto const settleDelay = 3600s;
        auto const channelFunds = USD(1000);
        auto const chan = channel(alice, bob, env.seq(alice));
        env(create(alice, bob, channelFunds, settleDelay, pk));
        BEAST_EXPECT(channelExists(*env.current(), chan));
        // Owner tries to close channel, but it will remain open (settle delay)
        env(claim(alice, chan), txflags(tfClose));
        BEAST_EXPECT(channelExists(*env.current(), chan));
        {
            // claim the entire amount
            auto const preBob = env.balance(bob, USD.issue());
            env(claim(alice, chan, channelFunds.value(), channelFunds.value()));
            BEAST_EXPECT(channelBalance(*env.current(), chan) == channelFunds);
            BEAST_EXPECT(
                env.balance(bob, USD.issue()).value() ==
                preBob.value() + channelFunds);
        }
        auto const preAliceUsd = env.balance(alice, USD.issue());
        auto const preAliceXrp = env.balance(alice);
        // Channel is now dry, can close before expiration date
        env(claim(alice, chan), txflags(tfClose));
        BEAST_EXPECT(!channelExists(*env.current(), chan));
        auto const feeDrops = env.current()->fees().base;
        BEAST_EXPECT(env.balance(alice) == preAliceXrp - feeDrops);
        BEAST_EXPECT(
            env.balance(alice, USD.issue()).value() == preAliceUsd.value());
    }

    void
    testICDefaultAmount(FeatureBitset features)
    {
        // auth amount defaults to balance if not present
        testcase("ic default amount");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(USD(100000), alice, bob);
        env.close();
        env(pay(gw, alice, USD(10000)));
        env(pay(gw, bob, USD(10000)));
        env.close();
        auto const pk = alice.pk();
        auto const settleDelay = 3600s;
        auto const channelFunds = USD(1000);
        auto const chan = channel(alice, bob, env.seq(alice));
        env(create(alice, bob, channelFunds, settleDelay, pk));
        BEAST_EXPECT(channelExists(*env.current(), chan));
        // Owner tries to close channel, but it will remain open (settle delay)
        env(claim(alice, chan), txflags(tfClose));
        BEAST_EXPECT(channelExists(*env.current(), chan));
        {
            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            auto const preBob = env.balance(bob, USD.issue());
            auto const preBobXrp = env.balance(bob);
            auto const preLocked = -lockedAmount(env, alice, gw, USD);

            auto const delta = USD(500);
            auto const reqBal = chanBal + delta;
            assert(reqBal <= chanAmt);
            auto const sig =
                signClaimICAuth(alice.pk(), alice.sk(), chan, reqBal);
            env(claim(bob, chan, reqBal, std::nullopt, Slice(sig), alice.pk()));
            auto const feeDrops = env.current()->fees().base;
            auto const postLocked = -lockedAmount(env, alice, gw, USD);
            BEAST_EXPECT(channelBalance(*env.current(), chan) == reqBal);
            BEAST_EXPECT(env.balance(bob) == preBobXrp - feeDrops);
            BEAST_EXPECT(
                env.balance(bob, USD.issue()).value() ==
                preBob.value() + delta);
            BEAST_EXPECT(preLocked == channelFunds);
            BEAST_EXPECT(postLocked == USD(500));
            chanBal = reqBal;
        }
        {
            // Claim again
            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            auto const preBob = env.balance(bob, USD.issue());
            auto const preBobXrp = env.balance(bob);
            auto const preLocked = -lockedAmount(env, alice, gw, USD);

            auto const delta = USD(500);
            auto const reqBal = chanBal + delta;
            assert(reqBal <= chanAmt);
            auto const sig =
                signClaimICAuth(alice.pk(), alice.sk(), chan, reqBal);
            env(claim(bob, chan, reqBal, std::nullopt, Slice(sig), alice.pk()));
            auto const feeDrops = env.current()->fees().base;
            auto const postLocked = -lockedAmount(env, alice, gw, USD);
            BEAST_EXPECT(channelBalance(*env.current(), chan) == reqBal);
            BEAST_EXPECT(env.balance(alice) == preBobXrp - feeDrops);
            BEAST_EXPECT(
                env.balance(bob, USD.issue()).value() ==
                preBob.value() + delta);
            BEAST_EXPECT(preLocked == (chanAmt - chanBal));
            BEAST_EXPECT(postLocked == USD(0));
            chanBal = reqBal;
        }
    }

    void
    testICDisallowXRP(FeatureBitset features)
    {
        // auth amount defaults to balance if not present
        testcase("IC Disallow XRP");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        {
            // Ignore the flag since it this is Issued Currency
            Env env(*this, supported_amendments() - featureDepositAuth);
            env.fund(XRP(10000), alice, bob, gw);
            env.close();
            env.trust(USD(100000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();
            env(fset(bob, asfDisallowXRP));
            auto const chan = channel(alice, bob, env.seq(alice));
            env(create(alice, bob, USD(1000), 3600s, alice.pk()));
            BEAST_EXPECT(channelExists(*env.current(), chan));
        }
        {
            // Ignore the flag since it this is Issued Currency
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, gw);
            env.close();
            env.trust(USD(100000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();
            env(fset(bob, asfDisallowXRP));
            auto const chan = channel(alice, bob, env.seq(alice));
            env(create(alice, bob, USD(1000), 3600s, alice.pk()));
            BEAST_EXPECT(channelExists(*env.current(), chan));
        }

        {
            // Ignore the flag since it this is Issued Currency
            Env env(*this, supported_amendments() - featureDepositAuth);
            env.fund(XRP(10000), alice, bob, gw);
            env.close();
            env.trust(USD(100000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();
            auto const chan = channel(alice, bob, env.seq(alice));
            env(create(alice, bob, USD(1000), 3600s, alice.pk()));
            BEAST_EXPECT(channelExists(*env.current(), chan));

            env(fset(bob, asfDisallowXRP));
            auto const reqBal = USD(500).value();
            env(claim(alice, chan, reqBal, reqBal));
        }
        {
            // Ignore the flag since it this is Issued Currency
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, gw);
            env.close();
            env.trust(USD(100000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();
            auto const chan = channel(alice, bob, env.seq(alice));
            env(create(alice, bob, USD(1000), 3600s, alice.pk()));
            BEAST_EXPECT(channelExists(*env.current(), chan));

            env(fset(bob, asfDisallowXRP));
            auto const reqBal = USD(500).value();
            env(claim(alice, chan, reqBal, reqBal));
        }
    }

    void
    testICDstTag(FeatureBitset features)
    {
        // auth amount defaults to balance if not present
        testcase("IC Dst Tag");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        // Create a channel where dst disallows XRP
        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(USD(100000), alice, bob);
        env.close();
        env(pay(gw, alice, USD(10000)));
        env(pay(gw, bob, USD(10000)));
        env.close();

        env(fset(bob, asfRequireDest));
        auto const pk = alice.pk();
        auto const settleDelay = 3600s;
        auto const channelFunds = USD(1000);
        {
            auto const chan = channel(alice, bob, env.seq(alice));
            env(create(alice, bob, channelFunds, settleDelay, pk),
                ter(tecDST_TAG_NEEDED));
            BEAST_EXPECT(!channelExists(*env.current(), chan));
        }
        {
            auto const chan = channel(alice, bob, env.seq(alice));
            env(create(
                alice, bob, channelFunds, settleDelay, pk, std::nullopt, 1));
            BEAST_EXPECT(channelExists(*env.current(), chan));
        }
    }

    void
    testICDepositAuth(FeatureBitset features)
    {
        testcase("IC Deposit Authorization");
        using namespace jtx;
        using namespace std::literals::chrono_literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        {
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, carol, gw);
            env.close();
            env.trust(USD(100000), alice, bob, carol);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env(pay(gw, carol, USD(10000)));
            env.close();
            env(fset(bob, asfDepositAuth));
            env.close();
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = channel(alice, bob, env.seq(alice));
            env(create(alice, bob, USD(1000), settleDelay, pk));
            env.close();

            BEAST_EXPECT(channelBalance(*env.current(), chan) == USD(0));
            BEAST_EXPECT(channelAmount(*env.current(), chan) == USD(1000));

            // alice can add more funds to the channel even though bob has
            // asfDepositAuth set.
            env(fund(alice, chan, USD(1000)));
            env.close();

            // alice claims. Fails because bob's lsfDepositAuth flag is set.
            env(claim(alice, chan, USD(500).value(), USD(500).value()),
                ter(tecNO_PERMISSION));
            env.close();

            // Claim with signature
            auto const baseFee = env.current()->fees().base;
            auto const preBob = env.balance(bob, USD.issue());
            auto const preBobXrp = env.balance(bob);
            {
                auto const delta = USD(500).value();
                auto const sig = signClaimICAuth(pk, alice.sk(), chan, delta);

                // alice claims with signature.  Fails since bob has
                // lsfDepositAuth flag set.
                env(claim(alice, chan, delta, delta, Slice(sig), pk),
                    ter(tecNO_PERMISSION));
                env.close();
                BEAST_EXPECT(env.balance(bob) == preBobXrp);
                BEAST_EXPECT(env.balance(bob, USD.issue()) == preBob.value());

                // bob claims but omits the signature.  Fails because only
                // alice can claim without a signature.
                env(claim(bob, chan, delta, delta), ter(temBAD_SIGNATURE));
                env.close();

                // bob claims with signature.  Succeeds even though bob's
                // lsfDepositAuth flag is set since bob submitted the
                // transaction.
                env(claim(bob, chan, delta, delta, Slice(sig), pk));
                env.close();
                BEAST_EXPECT(env.balance(bob) == preBobXrp - baseFee);
                BEAST_EXPECT(
                    env.balance(bob, USD.issue()) == preBob.value() + delta);
            }
            {
                // Explore the limits of deposit preauthorization.
                auto const delta = USD(600).value();
                auto const sig = signClaimICAuth(pk, alice.sk(), chan, delta);

                // carol claims and fails.  Only channel participants (bob or
                // alice) may claim.
                env(claim(carol, chan, delta, delta, Slice(sig), pk),
                    ter(tecNO_PERMISSION));
                env.close();

                // bob preauthorizes carol for deposit.  But after that carol
                // still can't claim since only channel participants may claim.
                env(deposit::auth(bob, carol));
                env.close();

                env(claim(carol, chan, delta, delta, Slice(sig), pk),
                    ter(tecNO_PERMISSION));

                // Since alice is not preauthorized she also may not claim
                // for bob.
                env(claim(alice, chan, delta, delta, Slice(sig), pk),
                    ter(tecNO_PERMISSION));
                env.close();

                // However if bob preauthorizes alice for deposit then she can
                // successfully submit a claim.
                env(deposit::auth(bob, alice));
                env.close();

                env(claim(alice, chan, delta, delta, Slice(sig), pk));
                env.close();

                BEAST_EXPECT(env.balance(bob) == preBobXrp - (3 * baseFee));
                BEAST_EXPECT(
                    env.balance(bob, USD.issue()) == preBob.value() + delta);
            }
            {
                // bob removes preauthorization of alice.  Once again she
                // cannot submit a claim.
                auto const delta = USD(800).value();

                env(deposit::unauth(bob, alice));
                env.close();

                // alice claims and fails since she is no longer preauthorized.
                env(claim(alice, chan, delta, delta), ter(tecNO_PERMISSION));
                env.close();

                // bob clears lsfDepositAuth.  Now alice can claim.
                env(fclear(bob, asfDepositAuth));
                env.close();

                // alice claims successfully.
                env(claim(alice, chan, delta, delta));
                env.close();
                BEAST_EXPECT(env.balance(bob) == preBobXrp - (5 * baseFee));
                BEAST_EXPECT(
                    env.balance(bob, USD.issue()) == preBob.value() + USD(800));
            }
        }
    }

    void
    testICMultiple(FeatureBitset features)
    {
        // auth amount defaults to balance if not present
        testcase("IC Multiple channels to the same account");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(USD(100000), alice, bob);
        env.close();
        env(pay(gw, alice, USD(10000)));
        env(pay(gw, bob, USD(10000)));
        env.close();
        auto const pk = alice.pk();
        auto const settleDelay = 3600s;
        auto const channelFunds = USD(1000);
        auto const chan1 = channel(alice, bob, env.seq(alice));
        env(create(alice, bob, channelFunds, settleDelay, pk));
        BEAST_EXPECT(channelExists(*env.current(), chan1));
        auto const chan2 = channel(alice, bob, env.seq(alice));
        env(create(alice, bob, channelFunds, settleDelay, pk));
        BEAST_EXPECT(channelExists(*env.current(), chan2));
        BEAST_EXPECT(chan1 != chan2);
    }

    void
    testICAccountChannelsRPC(FeatureBitset features)
    {
        testcase("IC AccountChannels RPC");

        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const charlie = Account("charlie", KeyType::ed25519);
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(10000), alice, bob, charlie, gw);
        env.close();
        env.trust(USD(100000), alice, bob, charlie);
        env.close();
        env(pay(gw, alice, USD(10000)));
        env(pay(gw, bob, USD(10000)));
        env(pay(gw, charlie, USD(10000)));
        env.close();
        auto const pk = alice.pk();
        auto const settleDelay = 3600s;
        auto const channelFunds = USD(1000);
        auto const chan1Str = to_string(channel(alice, bob, env.seq(alice)));
        env(create(alice, bob, channelFunds, settleDelay, pk));
        env.close();
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
        auto const chan2Str = to_string(channel(alice, bob, env.seq(alice)));
        env(create(alice, bob, channelFunds, settleDelay, pk));
        env.close();
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
    testICAccountChannelsRPCMarkers(FeatureBitset features)
    {
        testcase("IC Account channels RPC markers");

        using namespace test::jtx;
        using namespace std::literals;
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

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

        Env env{*this, features};
        env.fund(XRP(10000), alice, gw);
        env.close();
        env.trust(USD(100000), alice);
        env.close();
        env(pay(gw, alice, USD(10000)));
        env.close();
        for (auto const& a : bobs)
        {
            env.fund(XRP(10000), a);
            env.close();
            env.trust(USD(100000), a);
            env.close();
            env(pay(gw, a, USD(10000)));
            env.close();
        }

        {
            // create a channel from alice to every bob account
            auto const settleDelay = 3600s;
            auto const channelFunds = USD(1);
            for (auto const& b : bobs)
            {
                env(create(alice, b, channelFunds, settleDelay, alice.pk()));
            }
        }

        auto testLimit = [](test::jtx::Env& env,
                            test::jtx::Account const& src,
                            std::optional<int> limit = std::nullopt,
                            Json::Value const& marker = Json::nullValue,
                            std::optional<test::jtx::Account> const& dst =
                                std::nullopt) {
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
            BEAST_EXPECT(r.isMember(jss::error_message));
        }
    }

    void
    testICAccountChannelsRPCSenderOnly(FeatureBitset features)
    {
        // Check that the account_channels command only returns channels owned
        // by the account
        testcase("IC Account channels RPC owner only");

        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        Env env{*this, features};
        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(USD(100000), alice, bob);
        env.close();
        env(pay(gw, alice, USD(10000)));
        env(pay(gw, bob, USD(10000)));
        env.close();
        // Create a channel from alice to bob and from bob to alice
        // When retrieving alice's channels, it should only retrieve the
        // channels where alice is the source, not the destination
        auto const settleDelay = 3600s;
        auto const channelFunds = USD(1000);
        env(create(alice, bob, channelFunds, settleDelay, alice.pk()));
        env(create(bob, alice, channelFunds, settleDelay, bob.pk()));

        auto const r = [&] {
            Json::Value jvc;
            jvc[jss::account] = alice.human();

            return env.rpc(
                "json", "account_channels", to_string(jvc))[jss::result];
        }();
        BEAST_EXPECT(r.isMember(jss::channels));
        BEAST_EXPECT(r[jss::channels].size() == 1);
        BEAST_EXPECT(
            r[jss::channels][0u][jss::destination_account].asString() ==
            bob.human());
    }

    void
    testICAuthVerifyRPC(FeatureBitset features)
    {
        testcase("IC PayChan Auth/Verify RPC");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const charlie = Account("charlie", KeyType::ed25519);
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(10000), alice, bob, charlie, gw);
        env.close();
        env.trust(USD(100000), alice, bob, charlie);
        env.close();
        env(pay(gw, alice, USD(10000)));
        env(pay(gw, bob, USD(10000)));
        env(pay(gw, charlie, USD(10000)));
        env.close();
        auto const pk = alice.pk();
        auto const settleDelay = 3600s;
        auto const channelFunds = USD(1000);
        auto const chan1Str = to_string(channel(alice, bob, env.seq(alice)));
        env(create(alice, bob, channelFunds, settleDelay, pk));
        env.close();
        std::string chan1PkStr;
        {
            auto const r =
                env.rpc("account_channels", alice.human(), bob.human());
            BEAST_EXPECT(r[jss::result][jss::channels].size() == 1);
            BEAST_EXPECT(
                r[jss::result][jss::channels][0u][jss::channel_id] == chan1Str);
            BEAST_EXPECT(r[jss::result][jss::validated]);
            chan1PkStr =
                r[jss::result][jss::channels][0u][jss::public_key].asString();
        }
        {
            auto const r = env.rpc("account_channels", alice.human());
            BEAST_EXPECT(r[jss::result][jss::channels].size() == 1);
            BEAST_EXPECT(
                r[jss::result][jss::channels][0u][jss::channel_id] == chan1Str);
            BEAST_EXPECT(r[jss::result][jss::validated]);
            chan1PkStr =
                r[jss::result][jss::channels][0u][jss::public_key].asString();
        }
        {
            auto const r =
                env.rpc("account_channels", bob.human(), alice.human());
            BEAST_EXPECT(r[jss::result][jss::channels].size() == 0);
            BEAST_EXPECT(r[jss::result][jss::validated]);
        }
        auto const chan2Str = to_string(channel(alice, bob, env.seq(alice)));
        env(create(alice, bob, channelFunds, settleDelay, pk));
        env.close();
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
            auto const rs = env.rpc(
                "channel_authorize",
                "alice",
                chan1Str,
                "1000/USD/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA");
            auto const sig = rs[jss::result][jss::signature].asString();
            BEAST_EXPECT(!sig.empty());
            {
                auto const rv = env.rpc(
                    "channel_verify",
                    chan1PkStr,
                    chan1Str,
                    "1000/USD/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
                    sig);
                BEAST_EXPECT(rv[jss::result][jss::signature_verified].asBool());
            }

            {
                // use pk hex to verify
                auto const pkAsHex = sliceToHex(pk.slice());
                auto const rv = env.rpc(
                    "channel_verify",
                    pkAsHex,
                    chan1Str,
                    "1000/USD/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
                    sig);
                BEAST_EXPECT(rv[jss::result][jss::signature_verified].asBool());
            }
            {
                // malformed currency
                auto const pkAsHex = sliceToHex(pk.slice());
                auto rv = env.rpc(
                    "channel_verify",
                    pkAsHex,
                    chan1Str,
                    "1000/x/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
                    sig);
                BEAST_EXPECT(rv[jss::error] == "channelAmtMalformed");

                // malformed issuer
                rv = env.rpc(
                    "channel_verify", pkAsHex, chan1Str, "1000/USD/", sig);
                BEAST_EXPECT(rv[jss::error] == "channelAmtMalformed");

                // malformed value
                rv = env.rpc(
                    "channel_verify",
                    pkAsHex,
                    chan1Str,
                    "1000x/USD/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
                    sig);
                BEAST_EXPECT(rv[jss::error] == "channelAmtMalformed");
                rv = env.rpc(
                    "channel_verify",
                    pkAsHex,
                    chan1Str,
                    "1000 /USD/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
                    sig);
                BEAST_EXPECT(rv[jss::error] == "channelAmtMalformed");
                rv = env.rpc(
                    "channel_verify",
                    pkAsHex,
                    chan1Str,
                    "x1000/USD/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
                    sig);
                BEAST_EXPECT(rv[jss::error] == "channelAmtMalformed");
                rv = env.rpc(
                    "channel_verify",
                    pkAsHex,
                    chan1Str,
                    "x/USD/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
                    sig);
                BEAST_EXPECT(rv[jss::error] == "channelAmtMalformed");
                rv = env.rpc(
                    "channel_verify",
                    pkAsHex,
                    chan1Str,
                    " /USD/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
                    sig);
                BEAST_EXPECT(rv[jss::error] == "channelAmtMalformed");
                rv = env.rpc(
                    "channel_verify",
                    pkAsHex,
                    chan1Str,
                    "1000 1000/USD/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
                    sig);
                BEAST_EXPECT(rv[jss::error] == "channelAmtMalformed");
                rv = env.rpc(
                    "channel_verify",
                    pkAsHex,
                    chan1Str,
                    "1,000/USD/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
                    sig);
                BEAST_EXPECT(rv[jss::error] == "channelAmtMalformed");
                rv = env.rpc(
                    "channel_verify",
                    pkAsHex,
                    chan1Str,
                    " 1000/USD/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
                    sig);
                BEAST_EXPECT(rv[jss::error] == "channelAmtMalformed");
                rv = env.rpc(
                    "channel_verify",
                    pkAsHex,
                    chan1Str,
                    "/USD/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
                    sig);
                BEAST_EXPECT(rv[jss::error] == "channelAmtMalformed");
            }
            {
                // malformed channel
                auto const pkAsHex = sliceToHex(pk.slice());
                auto chan1StrBad = chan1Str;
                chan1StrBad.pop_back();
                auto rv = env.rpc(
                    "channel_verify",
                    pkAsHex,
                    chan1StrBad,
                    "1000/USD/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
                    sig);
                BEAST_EXPECT(rv[jss::error] == "channelMalformed");
                rv = env.rpc(
                    "channel_authorize",
                    "alice",
                    chan1StrBad,
                    "1000/USD/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA");
                BEAST_EXPECT(rv[jss::error] == "channelMalformed");

                chan1StrBad = chan1Str;
                chan1StrBad.push_back('0');
                rv = env.rpc(
                    "channel_verify",
                    pkAsHex,
                    chan1StrBad,
                    "1000/USD/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
                    sig);
                BEAST_EXPECT(rv[jss::error] == "channelMalformed");
                rv = env.rpc(
                    "channel_authorize",
                    "alice",
                    chan1StrBad,
                    "1000/USD/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA");
                BEAST_EXPECT(rv[jss::error] == "channelMalformed");

                chan1StrBad = chan1Str;
                chan1StrBad.back() = 'x';
                rv = env.rpc(
                    "channel_verify",
                    pkAsHex,
                    chan1StrBad,
                    "1000/USD/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
                    sig);
                BEAST_EXPECT(rv[jss::error] == "channelMalformed");
                rv = env.rpc(
                    "channel_authorize",
                    "alice",
                    chan1StrBad,
                    "1000/USD/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA");
                BEAST_EXPECT(rv[jss::error] == "channelMalformed");
            }
            {
                // give an ill formed base 58 public key
                auto illFormedPk = chan1PkStr.substr(0, chan1PkStr.size() - 1);
                auto const rv = env.rpc(
                    "channel_verify",
                    illFormedPk,
                    chan1Str,
                    "1000/USD/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
                    sig);
                BEAST_EXPECT(
                    !rv[jss::result][jss::signature_verified].asBool());
            }
            {
                // give an ill formed hex public key
                auto const pkAsHex = sliceToHex(pk.slice());
                auto illFormedPk = pkAsHex.substr(0, chan1PkStr.size() - 1);
                auto const rv = env.rpc(
                    "channel_verify",
                    illFormedPk,
                    chan1Str,
                    "1000/USD/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
                    sig);
                BEAST_EXPECT(
                    !rv[jss::result][jss::signature_verified].asBool());
            }
        }
        {
            // Try to verify chan2 auth with chan1 key
            auto const rs = env.rpc(
                "channel_authorize",
                "alice",
                chan2Str,
                "1000/USD/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA");
            auto const sig = rs[jss::result][jss::signature].asString();
            BEAST_EXPECT(!sig.empty());
            {
                auto const rv = env.rpc(
                    "channel_verify",
                    chan1PkStr,
                    chan1Str,
                    "1000/USD/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
                    sig);
                BEAST_EXPECT(
                    !rv[jss::result][jss::signature_verified].asBool());
            }
            {
                // use pk hex to verify
                auto const pkAsHex = sliceToHex(pk.slice());
                auto const rv = env.rpc(
                    "channel_verify",
                    pkAsHex,
                    chan1Str,
                    "1000/USD/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
                    sig);
                BEAST_EXPECT(
                    !rv[jss::result][jss::signature_verified].asBool());
            }
        }
        {
            // Try to explicitly specify secp256k1 and Ed25519 keys:
            auto const chan =
                to_string(channel(charlie, alice, env.seq(charlie)));
            env(create(
                charlie, alice, channelFunds, settleDelay, charlie.pk()));
            env.close();

            std::string cpk;
            {
                auto const r =
                    env.rpc("account_channels", charlie.human(), alice.human());
                BEAST_EXPECT(r[jss::result][jss::channels].size() == 1);
                BEAST_EXPECT(
                    r[jss::result][jss::channels][0u][jss::channel_id] == chan);
                BEAST_EXPECT(r[jss::result][jss::validated]);
                cpk = r[jss::result][jss::channels][0u][jss::public_key]
                          .asString();
            }

            // Try to authorize without specifying a key type, expect an
            // error:
            auto const rs = env.rpc(
                "channel_authorize",
                "charlie",
                chan,
                "1000/USD/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA");
            auto const sig = rs[jss::result][jss::signature].asString();
            BEAST_EXPECT(!sig.empty());
            {
                auto const rv = env.rpc(
                    "channel_verify",
                    cpk,
                    chan,
                    "1000/USD/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
                    sig);
                BEAST_EXPECT(
                    !rv[jss::result][jss::signature_verified].asBool());
            }

            // Try to authorize using an unknown key type, except an error:
            auto const rs1 = env.rpc(
                "channel_authorize",
                "charlie",
                "nyx",
                chan,
                "1000/USD/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA");
            BEAST_EXPECT(rs1[jss::error] == "badKeyType");

            // Try to authorize using secp256k1; the authorization _should_
            // succeed but the verification should fail:
            auto const rs2 = env.rpc(
                "channel_authorize",
                "charlie",
                "secp256k1",
                chan,
                "1000/USD/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA");
            auto const sig2 = rs2[jss::result][jss::signature].asString();
            BEAST_EXPECT(!sig2.empty());
            {
                auto const rv = env.rpc(
                    "channel_verify",
                    cpk,
                    chan,
                    "1000/USD/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
                    sig2);
                BEAST_EXPECT(
                    !rv[jss::result][jss::signature_verified].asBool());
            }

            // Try to authorize using Ed25519; expect success:
            auto const rs3 = env.rpc(
                "channel_authorize",
                "charlie",
                "ed25519",
                chan,
                "1000/USD/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA");
            auto const sig3 = rs3[jss::result][jss::signature].asString();
            BEAST_EXPECT(!sig3.empty());
            {
                auto const rv = env.rpc(
                    "channel_verify",
                    cpk,
                    chan,
                    "1000/USD/rnUy2SHTrB9DubsPmkJZUXTf5FcNDGrYEA",
                    sig3);
                BEAST_EXPECT(rv[jss::result][jss::signature_verified].asBool());
            }
        }

        {
            Json::Value args{Json::objectValue};
            args[jss::amount][jss::currency] = "USD";
            args[jss::amount][jss::issuer] = gw.human();
            args[jss::amount][jss::value] = 1000;
            args[jss::channel_id] = chan1Str;
            args[jss::key_type] = "secp256k1";
            args[jss::passphrase] = "passphrase_can_be_anything";

            // send malformed amounts rpc requests
            args[jss::amount][jss::value] = "1000x";
            auto rs = env.rpc(
                "json",
                "channel_authorize",
                args.toStyledString())[jss::result];
            BEAST_EXPECT(rs[jss::error] == "channelAmtMalformed");
            args[jss::amount][jss::value] = "x1000";
            rs = env.rpc(
                "json",
                "channel_authorize",
                args.toStyledString())[jss::result];
            BEAST_EXPECT(rs[jss::error] == "channelAmtMalformed");
            args[jss::amount][jss::value] = "x";
            rs = env.rpc(
                "json",
                "channel_authorize",
                args.toStyledString())[jss::result];
            BEAST_EXPECT(rs[jss::error] == "channelAmtMalformed");
            {
                // Missing channel_id
                Json::Value args{Json::objectValue};
                args[jss::amount][jss::currency] = "USD";
                args[jss::amount][jss::issuer] = gw.human();
                args[jss::amount][jss::value] = 1000;
                args[jss::key_type] = "secp256k1";
                args[jss::passphrase] = "passphrase_can_be_anything";
                rs = env.rpc(
                    "json",
                    "channel_authorize",
                    args.toStyledString())[jss::result];
                BEAST_EXPECT(rs[jss::error] == "invalidParams");
            }
            {
                // Missing amount
                Json::Value args{Json::objectValue};
                args[jss::channel_id] = chan1Str;
                args[jss::key_type] = "secp256k1";
                args[jss::passphrase] = "passphrase_can_be_anything";
                rs = env.rpc(
                    "json",
                    "channel_authorize",
                    args.toStyledString())[jss::result];
                BEAST_EXPECT(rs[jss::error] == "invalidParams");
            }
            {
                // Missing key_type and no secret.
                Json::Value args{Json::objectValue};
                args[jss::amount][jss::currency] = "USD";
                args[jss::amount][jss::issuer] = gw.human();
                args[jss::amount][jss::value] = 1000;
                args[jss::channel_id] = chan1Str;
                args[jss::passphrase] = "passphrase_can_be_anything";
                rs = env.rpc(
                    "json",
                    "channel_authorize",
                    args.toStyledString())[jss::result];
                BEAST_EXPECT(rs[jss::error] == "invalidParams");
            }
            {
                // Both passphrase and seed specified.
                Json::Value args{Json::objectValue};
                args[jss::amount][jss::currency] = "USD";
                args[jss::amount][jss::issuer] = gw.human();
                args[jss::amount][jss::value] = 1000;
                args[jss::channel_id] = chan1Str;
                args[jss::key_type] = "secp256k1";
                args[jss::passphrase] = "passphrase_can_be_anything";
                args[jss::seed] = "seed can be anything";
                rs = env.rpc(
                    "json",
                    "channel_authorize",
                    args.toStyledString())[jss::result];
                BEAST_EXPECT(rs[jss::error] == "invalidParams");
            }
            {
                // channel_id is not exact hex.
                Json::Value args{Json::objectValue};
                args[jss::amount][jss::currency] = "USD";
                args[jss::amount][jss::issuer] = gw.human();
                args[jss::amount][jss::value] = 1000;
                args[jss::channel_id] = chan1Str + "1";
                args[jss::key_type] = "secp256k1";
                args[jss::passphrase] = "passphrase_can_be_anything";
                rs = env.rpc(
                    "json",
                    "channel_authorize",
                    args.toStyledString())[jss::result];
                BEAST_EXPECT(rs[jss::error] == "channelMalformed");
            }
            {
                // Amount is not a decimal string.
                Json::Value args{Json::objectValue};
                args[jss::amount][jss::currency] = "USD";
                args[jss::amount][jss::issuer] = gw.human();
                args[jss::amount][jss::value] = "TwoThousand";
                args[jss::channel_id] = chan1Str;
                args[jss::key_type] = "secp256k1";
                args[jss::passphrase] = "passphrase_can_be_anything";
                rs = env.rpc(
                    "json",
                    "channel_authorize",
                    args.toStyledString())[jss::result];
                BEAST_EXPECT(rs[jss::error] == "channelAmtMalformed");
            }
            {
                // Amount is object. tecSUCCESS
                Json::Value args{Json::objectValue};
                args[jss::amount][jss::currency] = "USD";
                args[jss::amount][jss::issuer] = gw.human();
                args[jss::amount][jss::value] = 1000;
                args[jss::channel_id] = chan1Str;
                args[jss::key_type] = "secp256k1";
                args[jss::passphrase] = "passphrase_can_be_anything";
                rs = env.rpc(
                    "json",
                    "channel_authorize",
                    args.toStyledString())[jss::result];
            }
        }
    }

    void
    testICOptionalFields(FeatureBitset features)
    {
        testcase("IC Optional Fields");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const dan = Account("dan");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(10000), alice, bob, carol, dan, gw);
        env.close();
        env.trust(USD(100000), alice, bob, carol, dan);
        env.close();
        env(pay(gw, alice, USD(10000)));
        env(pay(gw, bob, USD(10000)));
        env(pay(gw, carol, USD(10000)));
        env(pay(gw, dan, USD(10000)));
        env.close();
        auto const pk = alice.pk();
        auto const settleDelay = 3600s;
        auto const channelFunds = USD(1000);

        std::optional<NetClock::time_point> cancelAfter;

        {
            auto const chan = to_string(channel(alice, bob, env.seq(alice)));
            env(create(alice, bob, channelFunds, settleDelay, pk));
            auto const r =
                env.rpc("account_channels", alice.human(), bob.human());
            BEAST_EXPECT(r[jss::result][jss::channels].size() == 1);
            BEAST_EXPECT(
                r[jss::result][jss::channels][0u][jss::channel_id] == chan);
            BEAST_EXPECT(!r[jss::result][jss::channels][0u].isMember(
                jss::destination_tag));
        }
        {
            std::uint32_t dstTag = 42;
            auto const chan = to_string(channel(alice, carol, env.seq(alice)));
            env(create(
                alice,
                carol,
                channelFunds,
                settleDelay,
                pk,
                cancelAfter,
                dstTag));
            auto const r =
                env.rpc("account_channels", alice.human(), carol.human());
            BEAST_EXPECT(r[jss::result][jss::channels].size() == 1);
            BEAST_EXPECT(
                r[jss::result][jss::channels][0u][jss::channel_id] == chan);
            BEAST_EXPECT(
                r[jss::result][jss::channels][0u][jss::destination_tag] ==
                dstTag);
        }
    }

    void
    testICMalformedPK(FeatureBitset features)
    {
        testcase("ic malformed pk");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(USD(100000), alice, bob);
        env.close();
        env(pay(gw, alice, USD(10000)));
        env(pay(gw, bob, USD(10000)));
        env.close();
        auto const pk = alice.pk();
        auto const settleDelay = 100s;

        auto const chan = channel(alice, bob, env.seq(alice));
        auto jv = create(alice, bob, USD(1000), settleDelay, pk);
        auto const pkHex = strHex(pk.slice());
        jv["PublicKey"] = pkHex.substr(2, pkHex.size() - 2);
        env(jv, ter(temMALFORMED));
        jv["PublicKey"] = pkHex.substr(0, pkHex.size() - 2);
        env(jv, ter(temMALFORMED));
        auto badPrefix = pkHex;
        badPrefix[0] = 'f';
        badPrefix[1] = 'f';
        jv["PublicKey"] = badPrefix;
        env(jv, ter(temMALFORMED));

        jv["PublicKey"] = pkHex;
        env(jv);

        auto const authAmt = USD(100);
        auto const sig = signClaimICAuth(alice.pk(), alice.sk(), chan, authAmt);
        jv = claim(
            bob,
            chan,
            authAmt.value(),
            authAmt.value(),
            Slice(sig),
            alice.pk());
        jv["PublicKey"] = pkHex.substr(2, pkHex.size() - 2);
        env(jv, ter(temMALFORMED));
        jv["PublicKey"] = pkHex.substr(0, pkHex.size() - 2);
        env(jv, ter(temMALFORMED));
        badPrefix = pkHex;
        badPrefix[0] = 'f';
        badPrefix[1] = 'f';
        jv["PublicKey"] = badPrefix;
        env(jv, ter(temMALFORMED));

        // missing public key
        jv.removeMember("PublicKey");
        env(jv, ter(temMALFORMED));

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
    testICMetaAndOwnership(FeatureBitset features)
    {
        testcase("IC Metadata & Ownership");

        using namespace jtx;
        using namespace std::literals::chrono_literals;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        auto const settleDelay = 100s;
        auto const pk = alice.pk();

        auto inOwnerDir = [](ReadView const& view,
                             Account const& acc,
                             std::shared_ptr<SLE const> const& chan) -> bool {
            ripple::Dir const ownerDir(view, keylet::ownerDir(acc.id()));
            return std::find(ownerDir.begin(), ownerDir.end(), chan) !=
                ownerDir.end();
        };

        auto ownerDirCount = [](ReadView const& view,
                                Account const& acc) -> std::size_t {
            ripple::Dir const ownerDir(view, keylet::ownerDir(acc.id()));
            return std::distance(ownerDir.begin(), ownerDir.end());
        };

        {
            // Test without adding the paychan to the recipient's owner
            // directory
            Env env(
                *this, supported_amendments() - fixPayChanRecipientOwnerDir);
            env.fund(XRP(10000), alice, bob, gw);
            env.close();
            env.trust(USD(100000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();
            env(create(alice, bob, USD(1000), settleDelay, pk));
            env.close();
            auto const [chan, chanSle] =
                channelKeyAndSle(*env.current(), alice, bob);
            BEAST_EXPECT(inOwnerDir(*env.current(), alice, chanSle));
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 2);
            BEAST_EXPECT(!inOwnerDir(*env.current(), bob, chanSle));
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 1);
            BEAST_EXPECT(-lockedAmount(env, alice, gw, USD) == USD(1000));

            // close the channel
            env(claim(bob, chan), txflags(tfClose));
            BEAST_EXPECT(!channelExists(*env.current(), chan));
            BEAST_EXPECT(!inOwnerDir(*env.current(), alice, chanSle));
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 1);
            BEAST_EXPECT(!inOwnerDir(*env.current(), bob, chanSle));
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 1);
            BEAST_EXPECT(-lockedAmount(env, alice, gw, USD) == USD(0));
        }

        {
            // Test with adding the paychan to the recipient's owner directory
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, gw);
            env.close();
            env.trust(USD(100000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();
            env(create(alice, bob, USD(1000), settleDelay, pk));
            env.close();
            auto const [chan, chanSle] =
                channelKeyAndSle(*env.current(), alice, bob);
            BEAST_EXPECT(inOwnerDir(*env.current(), alice, chanSle));
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 2);
            BEAST_EXPECT(inOwnerDir(*env.current(), bob, chanSle));
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 2);
            BEAST_EXPECT(-lockedAmount(env, alice, gw, USD) == USD(1000));
            // close the channel
            env(claim(bob, chan), txflags(tfClose));
            BEAST_EXPECT(!channelExists(*env.current(), chan));
            BEAST_EXPECT(!inOwnerDir(*env.current(), alice, chanSle));
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 1);
            BEAST_EXPECT(!inOwnerDir(*env.current(), bob, chanSle));
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 1);
            BEAST_EXPECT(-lockedAmount(env, alice, gw, USD) == USD(0));
        }

        {
            // Test removing paychans created before adding to the recipient's
            // owner directory
            Env env(
                *this, supported_amendments() - fixPayChanRecipientOwnerDir);
            env.fund(XRP(10000), alice, bob, gw);
            env.close();
            env.trust(USD(100000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();
            // create the channel before the amendment activates
            env(create(alice, bob, USD(1000), settleDelay, pk));
            env.close();
            auto const [chan, chanSle] =
                channelKeyAndSle(*env.current(), alice, bob);
            BEAST_EXPECT(inOwnerDir(*env.current(), alice, chanSle));
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 2);
            BEAST_EXPECT(!inOwnerDir(*env.current(), bob, chanSle));
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 1);
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
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 1);

            // close the channel after the amendment activates
            env(claim(bob, chan), txflags(tfClose));
            BEAST_EXPECT(!channelExists(*env.current(), chan));
            BEAST_EXPECT(!inOwnerDir(*env.current(), alice, chanSle));
            BEAST_EXPECT(ownerDirCount(*env.current(), alice) == 1);
            BEAST_EXPECT(!inOwnerDir(*env.current(), bob, chanSle));
            BEAST_EXPECT(ownerDirCount(*env.current(), bob) == 1);
        }
    }

    void
    testICAccountDelete(FeatureBitset features)
    {
        testcase("IC Account Delete");
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
            {
            }

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
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        for (bool const withOwnerDirFix : {false, true})
        {
            auto const amd = withOwnerDirFix
                ? supported_amendments()
                : supported_amendments() - fixPayChanRecipientOwnerDir;
            Env env{*this, amd};

            env.fund(XRP(10000), alice, bob, carol, gw);
            env.close();
            env.trust(USD(100000), alice, bob, carol);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env(pay(gw, carol, USD(10000)));
            env.close();

            auto const feeDrops = env.current()->fees().base;

            // Create a channel from alice to bob
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = channel(alice, bob, env.seq(alice));
            env(create(alice, bob, USD(1000), settleDelay, pk));
            env.close();
            BEAST_EXPECT(channelBalance(*env.current(), chan) == USD(0));
            BEAST_EXPECT(channelAmount(*env.current(), chan) == USD(1000));

            rmAccount(env, alice, carol, tecHAS_OBLIGATIONS);
            // can only remove bob if the channel isn't in their owner direcotry

            if (!withOwnerDirFix)
                env.trust(USD(0), bob);
            env.close();
            env(pay(bob, gw, USD(10000)));
            env.close();

            rmAccount(
                env,
                bob,
                carol,
                withOwnerDirFix ? TER(tecHAS_OBLIGATIONS) : TER(tesSUCCESS));

            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            BEAST_EXPECT(chanBal == USD(0));
            BEAST_EXPECT(chanAmt == USD(1000));

            auto preBob = env.balance(bob, USD.issue());
            auto preBobXrp = env.balance(bob);
            auto const delta = USD(50);
            auto reqBal = chanBal + delta;
            auto authAmt = reqBal + USD(100);
            assert(reqBal <= chanAmt);

            // claim should fail if the dst was removed
            if (withOwnerDirFix)
            {
                env(claim(alice, chan, reqBal, authAmt));
                env.close();
                BEAST_EXPECT(channelBalance(*env.current(), chan) == reqBal);
                BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
                BEAST_EXPECT(
                    env.balance(bob, USD.issue()) == preBob.value() + delta);
                chanBal = reqBal;
            }
            else
            {
                auto const preAlice = env.balance(alice, USD.issue());
                auto const preAliceXrp = env.balance(alice);
                env(claim(alice, chan, reqBal, authAmt), ter(tecNO_DST));
                env.close();
                BEAST_EXPECT(channelBalance(*env.current(), chan) == chanBal);
                BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
                BEAST_EXPECT(env.balance(bob, USD.issue()) == preBob.value());
                BEAST_EXPECT(env.balance(alice) == preAliceXrp - feeDrops);
                BEAST_EXPECT(
                    env.balance(alice, USD.issue()) == preAlice.value());
            }

            // fund should fail if the dst was removed
            if (withOwnerDirFix)
            {
                auto const preAlice = env.balance(alice, USD.issue());
                auto const preAliceXrp = env.balance(alice);
                auto const preLocked = -lockedAmount(env, alice, gw, USD);
                env(fund(alice, chan, USD(1000)));
                env.close();
                auto const postLocked = -lockedAmount(env, alice, gw, USD);
                BEAST_EXPECT(env.balance(alice) == preAliceXrp - feeDrops);
                BEAST_EXPECT(
                    env.balance(alice, USD.issue()) == preAlice.value());
                BEAST_EXPECT(postLocked == preLocked + USD(1000));
                BEAST_EXPECT(
                    channelAmount(*env.current(), chan) == chanAmt + USD(1000));
                chanAmt = chanAmt + USD(1000);
            }
            else
            {
                auto const preAlice = env.balance(alice, USD.issue());
                auto const preAliceXrp = env.balance(alice);
                env(fund(alice, chan, USD(1000)), ter(tecNO_DST));
                env.close();
                BEAST_EXPECT(env.balance(alice) == preAliceXrp - feeDrops);
                BEAST_EXPECT(env.balance(alice, USD.issue()) == preAlice);
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
            Env env{
                *this, supported_amendments() - fixPayChanRecipientOwnerDir};

            env.fund(XRP(10000), alice, bob, carol, gw);
            env.close();
            env.trust(USD(100000), alice, bob, carol);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env(pay(gw, carol, USD(10000)));
            env.close();

            auto const feeDrops = env.current()->fees().base;

            // Create a channel from alice to bob
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = channel(alice, bob, env.seq(alice));
            env(create(alice, bob, USD(1000), settleDelay, pk));
            env.close();
            BEAST_EXPECT(channelBalance(*env.current(), chan) == USD(0));
            BEAST_EXPECT(channelAmount(*env.current(), chan) == USD(1000));

            // Since `fixPayChanRecipientOwnerDir` is not active, can remove bob
            env.trust(USD(0), bob);
            env.close();
            env(pay(bob, gw, USD(10000)));
            env.close();
            rmAccount(env, bob, carol);
            BEAST_EXPECT(!env.closed()->exists(keylet::account(bob.id())));

            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            BEAST_EXPECT(chanBal == USD(0));
            BEAST_EXPECT(chanAmt == USD(1000));
            auto preBob = env.balance(bob, USD.issue());
            auto preBobXrp = env.balance(bob);
            auto const delta = USD(50);
            auto reqBal = chanBal + delta;
            auto authAmt = reqBal + USD(100);
            assert(reqBal <= chanAmt);

            {
                // claim should fail, since bob doesn't exist
                auto const preAlice = env.balance(alice, USD.issue());
                auto const preAliceXrp = env.balance(alice);
                env(claim(alice, chan, reqBal, authAmt), ter(tecNO_DST));
                env.close();
                BEAST_EXPECT(channelBalance(*env.current(), chan) == chanBal);
                BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
                BEAST_EXPECT(env.balance(bob, USD.issue()) == preBob.value());
                BEAST_EXPECT(env.balance(alice) == preAliceXrp - feeDrops);
                BEAST_EXPECT(
                    env.balance(alice, USD.issue()) == preAlice.value());
            }

            {
                // fund should fail, sincebob doesn't exist
                auto const preAlice = env.balance(alice, USD.issue());
                auto const preAliceXrp = env.balance(alice);
                env(fund(alice, chan, USD(1000)), ter(tecNO_DST));
                env.close();
                BEAST_EXPECT(env.balance(alice) == preAliceXrp - feeDrops);
                BEAST_EXPECT(
                    env.balance(alice, USD.issue()) == preAlice.value());
                BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
            }

            // resurrect bob
            env(pay(alice, bob, XRP(20)));
            env.close();
            env.trust(USD(100000), bob);
            env.close();
            env(pay(gw, bob, USD(10000)));
            env.close();
            BEAST_EXPECT(env.closed()->exists(keylet::account(bob.id())));
            {
                // alice should be able to claim
                preBob = env.balance(bob, USD.issue());
                preBobXrp = env.balance(bob);
                auto const preLocked = -lockedAmount(env, alice, gw, USD);
                reqBal = chanBal + delta;
                authAmt = reqBal + USD(100);
                env(claim(alice, chan, reqBal, authAmt));
                auto const postLocked = -lockedAmount(env, alice, gw, USD);
                BEAST_EXPECT(channelBalance(*env.current(), chan) == reqBal);
                BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
                BEAST_EXPECT(
                    env.balance(bob, USD.issue()) == preBob.value() + delta);
                BEAST_EXPECT(postLocked == preLocked - delta);
                chanBal = reqBal;
            }

            {
                // bob should be able to claim
                preBob = env.balance(bob, USD.issue());
                preBobXrp = env.balance(bob);
                auto const preLocked = -lockedAmount(env, alice, gw, USD);
                reqBal = chanBal + delta;
                authAmt = reqBal + USD(100);
                auto const sig =
                    signClaimICAuth(alice.pk(), alice.sk(), chan, authAmt);
                env(claim(bob, chan, reqBal, authAmt, Slice(sig), alice.pk()));
                auto const postLocked = -lockedAmount(env, alice, gw, USD);
                BEAST_EXPECT(channelBalance(*env.current(), chan) == reqBal);
                BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
                BEAST_EXPECT(env.balance(bob) == preBobXrp - feeDrops);
                BEAST_EXPECT(
                    env.balance(bob, USD.issue()) == preBob.value() + delta);
                BEAST_EXPECT(postLocked == preLocked - delta);
                chanBal = reqBal;
            }

            {
                // alice should be able to fund
                auto const preAlice = env.balance(alice, USD.issue());
                auto const preAliceXrp = env.balance(alice);
                auto const preLocked = -lockedAmount(env, alice, gw, USD);
                env(fund(alice, chan, USD(1000)));
                auto const postLocked = -lockedAmount(env, alice, gw, USD);
                BEAST_EXPECT(env.balance(alice) == preAliceXrp - feeDrops);
                BEAST_EXPECT(
                    env.balance(alice, USD.issue()) == preAlice.value());
                BEAST_EXPECT(
                    channelAmount(*env.current(), chan) == chanAmt + USD(1000));
                chanAmt = chanAmt + USD(1000);
                BEAST_EXPECT(postLocked == preLocked + USD(1000));
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
    testICUsingTickets(FeatureBitset features)
    {
        testcase("ic using tickets");
        using namespace jtx;
        using namespace std::literals::chrono_literals;
        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(USD(100000), alice, bob);
        env.close();
        env(pay(gw, alice, USD(10000)));
        env(pay(gw, bob, USD(10000)));
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

        auto const pk = alice.pk();
        auto const settleDelay = 100s;
        auto const chan = channel(alice, bob, aliceTicketSeq);

        env(create(alice, bob, USD(1000), settleDelay, pk),
            ticket::use(aliceTicketSeq++));

        env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
        BEAST_EXPECT(env.seq(alice) == aliceSeq);

        BEAST_EXPECT(channelBalance(*env.current(), chan) == USD(0));
        BEAST_EXPECT(channelAmount(*env.current(), chan) == USD(1000));

        {
            auto const preAlice = env.balance(alice, USD.issue());
            auto const preAliceXrp = env.balance(alice);
            auto const preLocked = -lockedAmount(env, alice, gw, USD);
            env(fund(alice, chan, USD(1000)), ticket::use(aliceTicketSeq++));

            env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
            BEAST_EXPECT(env.seq(alice) == aliceSeq);

            auto const feeDrops = env.current()->fees().base;
            BEAST_EXPECT(env.balance(alice) == preAliceXrp - feeDrops);
            BEAST_EXPECT(
                env.balance(alice, USD.issue()) ==
                (preAlice.value() - preLocked) + USD(1000));
        }

        auto chanBal = channelBalance(*env.current(), chan);
        auto chanAmt = channelAmount(*env.current(), chan);
        BEAST_EXPECT(chanBal == USD(0));
        BEAST_EXPECT(chanAmt == USD(2000));

        {
            // No signature needed since the owner is claiming
            auto const preBob = env.balance(bob, USD.issue());
            auto const delta = USD(500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = reqBal + USD(100);
            assert(reqBal <= chanAmt);
            env(claim(alice, chan, reqBal, authAmt),
                ticket::use(aliceTicketSeq++));

            env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
            BEAST_EXPECT(env.seq(alice) == aliceSeq);

            BEAST_EXPECT(channelBalance(*env.current(), chan) == reqBal);
            BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
            BEAST_EXPECT(
                env.balance(bob, USD.issue()) == preBob.value() + delta);
            chanBal = reqBal;
        }
        {
            // Claim with signature
            auto preBob = env.balance(bob, USD.issue());
            auto preBobXrp = env.balance(bob);
            auto const delta = USD(500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = reqBal + USD(100);
            assert(reqBal <= chanAmt);
            auto const sig =
                signClaimICAuth(alice.pk(), alice.sk(), chan, authAmt);
            env(claim(bob, chan, reqBal, authAmt, Slice(sig), alice.pk()),
                ticket::use(bobTicketSeq++));

            env.require(tickets(bob, env.seq(bob) - bobTicketSeq));
            BEAST_EXPECT(env.seq(bob) == bobSeq);

            BEAST_EXPECT(channelBalance(*env.current(), chan) == reqBal);
            BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
            auto const feeDrops = env.current()->fees().base;
            BEAST_EXPECT(env.balance(bob) == preBobXrp - feeDrops);
            BEAST_EXPECT(
                env.balance(bob, USD.issue()) == preBob.value() + delta);
            chanBal = reqBal;

            // claim again
            preBob = env.balance(bob, USD.issue());
            preBobXrp = env.balance(bob);
            // A transaction that generates a tec still consumes its ticket.
            env(claim(bob, chan, reqBal, authAmt, Slice(sig), alice.pk()),
                ticket::use(bobTicketSeq++),
                ter(tecINSUFFICIENT_FUNDS));

            env.require(tickets(bob, env.seq(bob) - bobTicketSeq));
            BEAST_EXPECT(env.seq(bob) == bobSeq);

            BEAST_EXPECT(channelBalance(*env.current(), chan) == chanBal);
            BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
            BEAST_EXPECT(env.balance(bob) == preBobXrp - feeDrops);
            BEAST_EXPECT(env.balance(bob, USD.issue()) == preBob.value());
        }
        {
            // Try to claim more than authorized
            auto const preBob = env.balance(bob, USD.issue());
            STAmount const authAmt = chanBal + USD(500);
            STAmount const reqAmt = authAmt + USD(1);
            assert(reqAmt <= chanAmt);
            // Note that since claim() returns a tem (neither tec nor tes),
            // the ticket is not consumed.  So we don't increment bobTicket.
            auto const sig =
                signClaimICAuth(alice.pk(), alice.sk(), chan, authAmt);
            env(claim(bob, chan, reqAmt, authAmt, Slice(sig), alice.pk()),
                ticket::use(bobTicketSeq),
                ter(temBAD_AMOUNT));

            env.require(tickets(bob, env.seq(bob) - bobTicketSeq));
            BEAST_EXPECT(env.seq(bob) == bobSeq);

            BEAST_EXPECT(channelBalance(*env.current(), chan) == chanBal);
            BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
            BEAST_EXPECT(env.balance(bob, USD.issue()) == preBob.value());
        }

        // Dst tries to fund the channel
        env(fund(bob, chan, USD(1000)),
            ticket::use(bobTicketSeq++),
            ter(tecNO_PERMISSION));

        env.require(tickets(bob, env.seq(bob) - bobTicketSeq));
        BEAST_EXPECT(env.seq(bob) == bobSeq);

        BEAST_EXPECT(channelBalance(*env.current(), chan) == chanBal);
        BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
        {
            // Dst closes channel
            auto const preAlice = env.balance(alice, USD.issue());
            auto const preBob = env.balance(bob, USD.issue());
            auto const preBobXrp = env.balance(bob);
            auto const preLocked = -lockedAmount(env, alice, gw, USD);
            env(claim(bob, chan),
                txflags(tfClose),
                ticket::use(bobTicketSeq++));

            env.require(tickets(bob, env.seq(bob) - bobTicketSeq));
            BEAST_EXPECT(env.seq(bob) == bobSeq);

            BEAST_EXPECT(!channelExists(*env.current(), chan));
            auto const feeDrops = env.current()->fees().base;
            auto const delta = chanAmt - chanBal;
            assert(delta > beast::zero);
            BEAST_EXPECT(
                env.balance(alice, USD.issue()) ==
                (preAlice.value() - preLocked) + delta);
            BEAST_EXPECT(env.balance(bob) == preBobXrp - feeDrops);
            BEAST_EXPECT(env.balance(bob, USD.issue()) == preBob.value());
        }
        env.require(tickets(alice, env.seq(alice) - aliceTicketSeq));
        BEAST_EXPECT(env.seq(alice) == aliceSeq);
        env.require(tickets(bob, env.seq(bob) - bobTicketSeq));
        BEAST_EXPECT(env.seq(bob) == bobSeq);
    }

    void
    testICAutoTL(FeatureBitset features)
    {
        testcase("IC Auto Trust Line");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        {
            // test trust line missing
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, gw);
            env.close();
            env.trust(USD(100000), alice);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env.close();
            auto const feeDrops = env.current()->fees().base;
            // Create a channel from alice to bob
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = channel(alice, bob, env.seq(alice));
            env(create(alice, bob, USD(1000), settleDelay, pk));
            env.close();
            BEAST_EXPECT(channelBalance(*env.current(), chan) == USD(0));
            BEAST_EXPECT(channelAmount(*env.current(), chan) == USD(1000));

            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            BEAST_EXPECT(chanBal == USD(0));
            BEAST_EXPECT(chanAmt == USD(1000));
            auto preBob = env.balance(bob);
            auto const delta = USD(50);
            auto reqBal = chanBal + delta;
            auto authAmt = reqBal + USD(100);
            assert(reqBal <= chanAmt);
            {
                // claim should fail, since bob doesn't have tl
                // and alice cannot create tl for bob
                auto const preAlice = env.balance(alice, USD.issue());
                auto const preBob = env.balance(bob, USD.issue());
                auto const preAliceXrp = env.balance(alice);
                env(claim(alice, chan, reqBal, authAmt), ter(tecNO_LINE));
                env.close();
                BEAST_EXPECT(channelBalance(*env.current(), chan) == chanBal);
                BEAST_EXPECT(channelAmount(*env.current(), chan) == chanAmt);
                BEAST_EXPECT(env.balance(bob, USD.issue()) == preBob);
                BEAST_EXPECT(env.balance(alice) == preAliceXrp - feeDrops);
                BEAST_EXPECT(env.balance(alice, USD.issue()) == preAlice);
            }
            {
                // claim should succeed, since the tl will auto add
                auto const preAlice = env.balance(alice, USD.issue());
                auto const preBob = env.balance(bob, USD.issue());
                auto const preBobXrp = env.balance(bob);
                auto const sig =
                    signClaimICAuth(alice.pk(), alice.sk(), chan, authAmt);
                env(claim(bob, chan, reqBal, authAmt, Slice(sig), alice.pk()));
                env.close();
                BEAST_EXPECT(preBob.value() == USD(0));
                BEAST_EXPECT(
                    env.balance(alice, USD.issue()) == preAlice - delta);
                BEAST_EXPECT(env.balance(bob) == preBobXrp - feeDrops);
                BEAST_EXPECT(
                    env.balance(bob, USD.issue()) == preBob.value() + delta);
            }
        }
    }

    void
    testICRippleState(FeatureBitset features)
    {
        testcase("IC RippleState");
        using namespace test::jtx;
        using namespace std::literals;

        //
        // USE balance(env, ...) over env.balance(...)
        // I did this to check the exact sign "-/+"
        //

        // src > dst
        // src > issuer
        // dest no trustline
        // negative locked/tl balance
        {
            auto const src = Account("alice2");
            auto const dst = Account("bob0");
            auto const gw = Account{"gw0"};
            auto const USD = gw["USD"];

            Env env{*this, features};
            env.fund(XRP(5000), src, dst, gw);
            env.close();
            env.trust(USD(100000), src);
            env.close();
            env(pay(gw, src, USD(10000)));
            env.close();

            // src can create paychan
            auto const pk = src.pk();
            auto const settleDelay = 100s;
            auto const chan = channel(src, dst, env.seq(src));
            auto preLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(preLocked == USD(0));
            env(create(src, dst, USD(1000), settleDelay, pk));
            env.close();

            preLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(preLocked == -USD(1000));

            // dst can claim paychan
            auto const preSrc = lineBalance(env, src, gw, USD);
            auto const preDst = lineBalance(env, dst, gw, USD);
            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            auto const delta = USD(500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = USD(1000);
            auto const sig = signClaimICAuth(src.pk(), src.sk(), chan, authAmt);
            env(claim(dst, chan, reqBal, authAmt, Slice(sig), src.pk()));
            env.close();
            auto postLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(lineBalance(env, src, gw, USD) == preSrc + delta);
            BEAST_EXPECT(lineBalance(env, dst, gw, USD) == preDst - delta);
            BEAST_EXPECT(preLocked == postLocked - delta);
            // src claim fails because trust limit is 0
            env(claim(src, chan, authAmt, authAmt), ter(tecPATH_DRY));
        }
        // src < dst
        // src < issuer
        // dest no trustline
        // positive locked/tl balance
        {
            auto const src = Account("carol0");
            auto const dst = Account("dan1");
            auto const gw = Account{"gw1"};
            auto const USD = gw["USD"];

            Env env{*this, features};
            env.fund(XRP(5000), src, dst, gw);
            env.close();
            env.trust(USD(100000), src);
            env.close();
            env(pay(gw, src, USD(10000)));
            env.close();

            // src can create paychan
            auto const pk = src.pk();
            auto const settleDelay = 100s;
            auto const chan = channel(src, dst, env.seq(src));
            auto preLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(preLocked == USD(0));
            env(create(src, dst, USD(1000), settleDelay, pk));
            env.close();

            preLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(preLocked == USD(1000));

            // dst can claim paychan
            auto const preSrc = lineBalance(env, src, gw, USD);
            auto const preDst = lineBalance(env, dst, gw, USD);
            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            auto const delta = USD(500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = USD(1000);
            auto const sig = signClaimICAuth(src.pk(), src.sk(), chan, authAmt);
            env(claim(dst, chan, reqBal, authAmt, Slice(sig), src.pk()));
            env.close();
            auto postLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(lineBalance(env, src, gw, USD) == preSrc - delta);
            BEAST_EXPECT(lineBalance(env, dst, gw, USD) == preDst + delta);
            BEAST_EXPECT(preLocked == postLocked + delta);
            // src claim fails because trust limit is 0
            env(claim(src, chan, authAmt, authAmt), ter(tecPATH_DRY));
        }
        // dst > src
        // dst > issuer
        // dest no trustline
        // negative locked/tl balance
        {
            auto const src = Account("dan1");
            auto const dst = Account("alice2");
            auto const gw = Account{"gw0"};
            auto const USD = gw["USD"];

            Env env{*this, features};
            env.fund(XRP(5000), src, dst, gw);
            env.close();
            env.trust(USD(100000), src);
            env.close();
            env(pay(gw, src, USD(10000)));
            env.close();

            // src can create paychan
            auto const pk = src.pk();
            auto const settleDelay = 100s;
            auto const chan = channel(src, dst, env.seq(src));
            auto preLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(preLocked == USD(0));
            env(create(src, dst, USD(1000), settleDelay, pk));
            env.close();

            preLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(preLocked == -USD(1000));

            // dst can claim paychan
            auto const preSrc = lineBalance(env, src, gw, USD);
            auto const preDst = lineBalance(env, dst, gw, USD);
            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            auto const delta = USD(500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = USD(1000);
            auto const sig = signClaimICAuth(src.pk(), src.sk(), chan, authAmt);
            env(claim(dst, chan, reqBal, authAmt, Slice(sig), src.pk()));
            env.close();
            auto postLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(lineBalance(env, src, gw, USD) == preSrc + delta);
            BEAST_EXPECT(lineBalance(env, dst, gw, USD) == preDst - delta);
            BEAST_EXPECT(preLocked == postLocked - delta);
            // src claim fails because trust limit is 0
            env(claim(src, chan, authAmt, authAmt), ter(tecPATH_DRY));
        }
        // dst < src
        // dst < issuer
        // dest no trustline
        // positive locked/tl balance
        {
            auto const src = Account("bob0");
            auto const dst = Account("carol0");
            auto const gw = Account{"gw1"};
            auto const USD = gw["USD"];

            Env env{*this, features};
            env.fund(XRP(5000), src, dst, gw);
            env.close();
            env.trust(USD(100000), src);
            env.close();
            env(pay(gw, src, USD(10000)));
            env.close();

            // src can create paychan
            auto const pk = src.pk();
            auto const settleDelay = 100s;
            auto const chan = channel(src, dst, env.seq(src));
            auto preLocked = -lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(preLocked == USD(0));
            env(create(src, dst, USD(1000), settleDelay, pk));
            env.close();

            preLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(preLocked == USD(1000));

            // dst can claim paychan
            auto const preSrc = lineBalance(env, src, gw, USD);
            auto const preDst = lineBalance(env, dst, gw, USD);
            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            auto const delta = USD(500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = USD(1000);
            auto const sig = signClaimICAuth(src.pk(), src.sk(), chan, authAmt);
            env(claim(dst, chan, reqBal, authAmt, Slice(sig), src.pk()));
            env.close();
            auto postLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(lineBalance(env, src, gw, USD) == preSrc - delta);
            BEAST_EXPECT(lineBalance(env, dst, gw, USD) == preDst + delta);
            BEAST_EXPECT(preLocked == postLocked + delta);
            // src claim fails because trust limit is 0
            env(claim(src, chan, authAmt, authAmt), ter(tecPATH_DRY));
        }
        // src > dst
        // src > issuer
        // dest trustline
        // negative locked/tl balance
        {
            auto const src = Account("alice2");
            auto const dst = Account("bob0");
            auto const gw = Account{"gw0"};
            auto const USD = gw["USD"];

            Env env{*this, features};
            env.fund(XRP(10000), src, dst, gw);
            env.close();
            env.trust(USD(100000), src, dst);
            env.close();
            env(pay(gw, src, USD(10000)));
            env(pay(gw, dst, USD(10000)));
            env.close();

            // src can create paychan
            auto const pk = src.pk();
            auto const settleDelay = 100s;
            auto const chan = channel(src, dst, env.seq(src));
            auto preLocked = -lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(preLocked == USD(0));
            env(create(src, dst, USD(1000), settleDelay, pk));
            env.close();

            preLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(preLocked == -USD(1000));

            // dst can claim paychan
            auto const preSrc = lineBalance(env, src, gw, USD);
            auto const preDst = lineBalance(env, dst, gw, USD);
            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            auto const delta = USD(500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = USD(1000);
            auto const sig = signClaimICAuth(src.pk(), src.sk(), chan, authAmt);
            env(claim(dst, chan, reqBal, authAmt, Slice(sig), src.pk()));
            env.close();
            auto postLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(lineBalance(env, src, gw, USD) == preSrc + delta);
            BEAST_EXPECT(lineBalance(env, dst, gw, USD) == preDst - delta);
            BEAST_EXPECT(preLocked == postLocked - delta);
            env(claim(src, chan, authAmt, authAmt));
            env.close();
            postLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(lineBalance(env, src, gw, USD) == preSrc + authAmt);
            BEAST_EXPECT(lineBalance(env, dst, gw, USD) == preDst - authAmt);
            BEAST_EXPECT(preLocked == postLocked - authAmt);
        }
        // src < dst
        // src < issuer
        // dest trustline
        // positive locked/tl balance
        {
            auto const src = Account("carol0");
            auto const dst = Account("dan1");
            auto const gw = Account{"gw1"};
            auto const USD = gw["USD"];

            Env env{*this, features};
            env.fund(XRP(10000), src, dst, gw);
            env.close();
            env.trust(USD(100000), src, dst);
            env.close();
            env(pay(gw, src, USD(10000)));
            env(pay(gw, dst, USD(10000)));
            env.close();

            // src can create paychan
            auto const pk = src.pk();
            auto const settleDelay = 100s;
            auto const chan = channel(src, dst, env.seq(src));
            auto preLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(preLocked == USD(0));
            env(create(src, dst, USD(1000), settleDelay, pk));
            env.close();

            preLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(preLocked == USD(1000));

            // dst can claim paychan
            auto const preSrc = lineBalance(env, src, gw, USD);
            auto const preDst = lineBalance(env, dst, gw, USD);
            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            auto const delta = USD(500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = USD(1000);
            auto const sig = signClaimICAuth(src.pk(), src.sk(), chan, authAmt);
            env(claim(dst, chan, reqBal, authAmt, Slice(sig), src.pk()));
            env.close();
            auto postLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(lineBalance(env, src, gw, USD) == preSrc - delta);
            BEAST_EXPECT(lineBalance(env, dst, gw, USD) == preDst + delta);
            BEAST_EXPECT(preLocked == postLocked + delta);
            env(claim(src, chan, authAmt, authAmt));
            env.close();
            postLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(lineBalance(env, src, gw, USD) == preSrc - authAmt);
            BEAST_EXPECT(lineBalance(env, dst, gw, USD) == preDst + authAmt);
            BEAST_EXPECT(preLocked == postLocked + authAmt);
        }
        // dst > src
        // dst > issuer
        // dest trustline
        // negative locked/tl balance
        {
            auto const src = Account("dan1");
            auto const dst = Account("alice2");
            auto const gw = Account{"gw0"};
            auto const USD = gw["USD"];

            Env env{*this, features};
            env.fund(XRP(10000), src, dst, gw);
            env.close();
            env.trust(USD(100000), src, dst);
            env.close();
            env(pay(gw, src, USD(10000)));
            env(pay(gw, dst, USD(10000)));
            env.close();

            // src can create paychan
            auto const pk = src.pk();
            auto const settleDelay = 100s;
            auto const chan = channel(src, dst, env.seq(src));
            auto preLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(preLocked == USD(0));
            env(create(src, dst, USD(1000), settleDelay, pk));
            env.close();

            preLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(preLocked == -USD(1000));

            // dst can claim paychan
            auto const preSrc = lineBalance(env, src, gw, USD);
            auto const preDst = lineBalance(env, dst, gw, USD);
            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            auto const delta = USD(500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = USD(1000);
            auto const sig = signClaimICAuth(src.pk(), src.sk(), chan, authAmt);
            env(claim(dst, chan, reqBal, authAmt, Slice(sig), src.pk()));
            env.close();
            auto postLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(lineBalance(env, src, gw, USD) == preSrc + delta);
            BEAST_EXPECT(lineBalance(env, dst, gw, USD) == preDst - delta);
            BEAST_EXPECT(preLocked == postLocked - delta);
            env(claim(src, chan, authAmt, authAmt));
            env.close();
            postLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(lineBalance(env, src, gw, USD) == preSrc + authAmt);
            BEAST_EXPECT(lineBalance(env, dst, gw, USD) == preDst - authAmt);
            BEAST_EXPECT(preLocked == postLocked - authAmt);
        }
        // dst < src
        // dst < issuer
        // dest trustline
        // positive locked/tl balance
        {
            auto const src = Account("bob0");
            auto const dst = Account("carol0");
            auto const gw = Account{"gw1"};
            auto const USD = gw["USD"];

            Env env{*this, features};
            env.fund(XRP(10000), src, dst, gw);
            env.close();
            env.trust(USD(100000), src, dst);
            env.close();
            env(pay(gw, src, USD(10000)));
            env(pay(gw, dst, USD(10000)));
            env.close();

            // src can create paychan
            auto const pk = src.pk();
            auto const settleDelay = 100s;
            auto const chan = channel(src, dst, env.seq(src));
            auto preLocked = -lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(preLocked == USD(0));
            env(create(src, dst, USD(1000), settleDelay, pk));
            env.close();

            preLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(preLocked == USD(1000));

            // dst can claim paychan
            auto const preSrc = lineBalance(env, src, gw, USD);
            auto const preDst = lineBalance(env, dst, gw, USD);
            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            auto const delta = USD(500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = USD(1000);
            auto const sig = signClaimICAuth(src.pk(), src.sk(), chan, authAmt);
            env(claim(dst, chan, reqBal, authAmt, Slice(sig), src.pk()));
            env.close();
            auto postLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(lineBalance(env, src, gw, USD) == preSrc - delta);
            BEAST_EXPECT(lineBalance(env, dst, gw, USD) == preDst + delta);
            BEAST_EXPECT(preLocked == postLocked + delta);
            env(claim(src, chan, authAmt, authAmt));
            env.close();
            postLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(lineBalance(env, src, gw, USD) == preSrc - authAmt);
            BEAST_EXPECT(lineBalance(env, dst, gw, USD) == preDst + authAmt);
            BEAST_EXPECT(preLocked == postLocked + authAmt);
        }
    }

    void
    testICGateway(FeatureBitset features)
    {
        testcase("IC Gateway");
        using namespace test::jtx;
        using namespace std::literals;

        // issuer -> src
        // src > issuer
        // dest no trustline
        // negative locked/tl balance
        {
            auto const src = Account("alice2");
            auto const gw = Account{"gw0"};
            auto const USD = gw["USD"];

            Env env{*this, features};
            env.fund(XRP(10000), src, gw);
            env.close();

            // issuer can create paychan
            auto const pk = gw.pk();
            auto const settleDelay = 100s;
            auto const chan = channel(gw, src, env.seq(gw));
            env(create(gw, src, USD(1000), settleDelay, pk));
            env.close();

            // src can claim paychan without trustline
            auto const preSrc = lineBalance(env, src, gw, USD);
            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            auto const delta = USD(500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = reqBal + USD(100);
            auto const sig = signClaimICAuth(gw.pk(), gw.sk(), chan, authAmt);
            env(claim(src, chan, reqBal, authAmt, Slice(sig), gw.pk()));
            env.close();
            BEAST_EXPECT(preSrc == USD(0));
            BEAST_EXPECT(lineBalance(env, src, gw, USD) == -USD(500));
        }
        // issuer -> src
        // src < issuer
        // dest no trustline
        // positive locked/tl balance
        {
            auto const src = Account("carol0");
            auto const gw = Account{"gw1"};
            auto const USD = gw["USD"];

            Env env{*this, features};
            env.fund(XRP(10000), src, gw);
            env.close();

            // issuer can create paychan
            auto const pk = gw.pk();
            auto const settleDelay = 100s;
            auto const chan = channel(gw, src, env.seq(gw));
            env(create(gw, src, USD(1000), settleDelay, pk));
            env.close();

            // src can claim paychan without trustline
            auto const preSrc = lineBalance(env, src, gw, USD);
            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            auto const delta = USD(500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = reqBal + USD(100);
            auto const sig = signClaimICAuth(gw.pk(), gw.sk(), chan, authAmt);
            env(claim(src, chan, reqBal, authAmt, Slice(sig), gw.pk()));
            env.close();
            BEAST_EXPECT(preSrc == USD(0));
            BEAST_EXPECT(lineBalance(env, src, gw, USD) == USD(500));
        }
        // issuer -> src
        // dst > issuer
        // dest no trustline
        // negative locked/tl balance
        {
            auto const src = Account("dan1");
            auto const gw = Account{"gw0"};
            auto const USD = gw["USD"];

            Env env{*this, features};
            env.fund(XRP(10000), src, gw);
            env.close();

            // issuer can create paychan
            auto const pk = gw.pk();
            auto const settleDelay = 100s;
            auto const chan = channel(gw, src, env.seq(gw));
            env(create(gw, src, USD(1000), settleDelay, pk));
            env.close();

            // src can claim paychan without trustline
            auto const preSrc = lineBalance(env, src, gw, USD);
            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            auto const delta = USD(500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = reqBal + USD(100);
            auto const sig = signClaimICAuth(gw.pk(), gw.sk(), chan, authAmt);
            env(claim(src, chan, reqBal, authAmt, Slice(sig), gw.pk()));
            env.close();
            BEAST_EXPECT(preSrc == USD(0));
            BEAST_EXPECT(lineBalance(env, src, gw, USD) == -USD(500));
        }
        // issuer -> src
        // dst < issuer
        // dest no trustline
        // positive locked/tl balance
        {
            auto const src = Account("bob0");
            auto const gw = Account{"gw1"};
            auto const USD = gw["USD"];

            Env env{*this, features};
            env.fund(XRP(10000), src, gw);
            env.close();

            // issuer can create paychan
            auto const pk = gw.pk();
            auto const settleDelay = 100s;
            auto const chan = channel(gw, src, env.seq(gw));
            env(create(gw, src, USD(1000), settleDelay, pk));
            env.close();

            // src can claim paychan without trustline
            auto const preSrc = lineBalance(env, src, gw, USD);
            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            auto const delta = USD(500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = reqBal + USD(100);
            auto const sig = signClaimICAuth(gw.pk(), gw.sk(), chan, authAmt);
            env(claim(src, chan, reqBal, authAmt, Slice(sig), gw.pk()));
            env.close();
            BEAST_EXPECT(preSrc == USD(0));
            BEAST_EXPECT(lineBalance(env, src, gw, USD) == USD(500));
        }
        // issuer -> src
        // src > issuer
        // dest has trustline
        // negative locked/tl balance
        {
            auto const src = Account("alice2");
            auto const gw = Account{"gw0"};
            auto const USD = gw["USD"];

            Env env{*this, features};
            env.fund(XRP(10000), src, gw);
            env.close();
            env.trust(USD(100000), src);
            env.close();
            env(pay(gw, src, USD(10000)));
            env.close();

            // issuer can create paychan
            auto const pk = gw.pk();
            auto const settleDelay = 100s;
            auto const chan = channel(gw, src, env.seq(gw));
            env(create(gw, src, USD(1000), settleDelay, pk));
            env.close();

            // src can claim paychan without trustline
            auto const preSrc = lineBalance(env, src, gw, USD);
            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            auto const delta = USD(500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = reqBal + USD(100);
            auto const sig = signClaimICAuth(gw.pk(), gw.sk(), chan, authAmt);
            env(claim(src, chan, reqBal, authAmt, Slice(sig), gw.pk()));
            env.close();
            BEAST_EXPECT(preSrc == -USD(10000));
            BEAST_EXPECT(lineBalance(env, src, gw, USD) == -USD(10500));
        }
        // issuer -> src
        // src < issuer
        // dest has trustline
        // positive locked/tl balance
        {
            auto const src = Account("carol0");
            auto const gw = Account{"gw1"};
            auto const USD = gw["USD"];

            Env env{*this, features};
            env.fund(XRP(10000), src, gw);
            env.close();
            env.trust(USD(100000), src);
            env.close();
            env(pay(gw, src, USD(10000)));
            env.close();

            // issuer can create paychan
            auto const pk = gw.pk();
            auto const settleDelay = 100s;
            auto const chan = channel(gw, src, env.seq(gw));
            env(create(gw, src, USD(1000), settleDelay, pk));
            env.close();

            // src can claim paychan without trustline
            auto const preSrc = lineBalance(env, src, gw, USD);
            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            auto const delta = USD(500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = reqBal + USD(100);
            auto const sig = signClaimICAuth(gw.pk(), gw.sk(), chan, authAmt);
            env(claim(src, chan, reqBal, authAmt, Slice(sig), gw.pk()));
            env.close();
            BEAST_EXPECT(preSrc == USD(10000));
            BEAST_EXPECT(lineBalance(env, src, gw, USD) == USD(10500));
        }
        // issuer -> src
        // dst > issuer
        // dest has trustline
        // negative locked/tl balance
        {
            auto const src = Account("dan1");
            auto const gw = Account{"gw0"};
            auto const USD = gw["USD"];

            Env env{*this, features};
            env.fund(XRP(10000), src, gw);
            env.close();
            env.trust(USD(100000), src);
            env.close();
            env(pay(gw, src, USD(10000)));
            env.close();

            // issuer can create paychan
            auto const pk = gw.pk();
            auto const settleDelay = 100s;
            auto const chan = channel(gw, src, env.seq(gw));
            env(create(gw, src, USD(1000), settleDelay, pk));
            env.close();

            // src can claim paychan without trustline
            auto const preSrc = lineBalance(env, src, gw, USD);
            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            auto const delta = USD(500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = reqBal + USD(100);
            auto const sig = signClaimICAuth(gw.pk(), gw.sk(), chan, authAmt);
            env(claim(src, chan, reqBal, authAmt, Slice(sig), gw.pk()));
            env.close();
            BEAST_EXPECT(preSrc == -USD(10000));
            BEAST_EXPECT(lineBalance(env, src, gw, USD) == -USD(10500));
        }
        // issuer -> src
        // dst < issuer
        // dest has trustline
        // positive locked/tl balance
        {
            auto const src = Account("bob0");
            auto const gw = Account{"gw1"};
            auto const USD = gw["USD"];

            Env env{*this, features};
            env.fund(XRP(10000), src, gw);
            env.close();
            env.trust(USD(100000), src);
            env.close();
            env(pay(gw, src, USD(10000)));
            env.close();

            // issuer can create paychan
            auto const pk = gw.pk();
            auto const settleDelay = 100s;
            auto const chan = channel(gw, src, env.seq(gw));
            env(create(gw, src, USD(1000), settleDelay, pk));
            env.close();

            // src can claim paychan without trustline
            auto const preSrc = lineBalance(env, src, gw, USD);
            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            auto const delta = USD(500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = reqBal + USD(100);
            auto const sig = signClaimICAuth(gw.pk(), gw.sk(), chan, authAmt);
            env(claim(src, chan, reqBal, authAmt, Slice(sig), gw.pk()));
            env.close();
            BEAST_EXPECT(preSrc == USD(10000));
            BEAST_EXPECT(lineBalance(env, src, gw, USD) == USD(10500));
        }
        // alice -> issuer
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];

            env.fund(XRP(10000), alice, gw);
            env.close();
            env.trust(USD(100000), alice);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env.close();

            // alice can create paychan
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = channel(alice, gw, env.seq(alice));
            env(create(alice, gw, USD(1000), settleDelay, pk));
            env.close();

            // issuer can claim paychan
            auto const preAlice = env.balance(alice, USD.issue());
            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            auto const delta = USD(500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = reqBal + USD(100);
            auto const sig =
                signClaimICAuth(alice.pk(), alice.sk(), chan, authAmt);
            env(claim(gw, chan, reqBal, authAmt, Slice(sig), alice.pk()));
            env.close();
            BEAST_EXPECT(preAlice == USD(10000));
            BEAST_EXPECT(env.balance(alice, USD.issue()) == preAlice - delta);
            std::cout << "PRE ALICE: " << preAlice << "\n";
            std::cout << "POST ALICE: " << env.balance(alice, USD.issue())
                      << "\n";
        }
    }

    void
    testICLockedRate(FeatureBitset features)
    {
        testcase("IC Locked Rate");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        // test locked rate
        {
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, gw);
            env(rate(gw, 1.25));
            env.close();
            env.trust(USD(100000), alice);
            env.trust(USD(100000), bob);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();

            // alice can create paychan w/ xfer rate
            auto const preAlice = env.balance(alice, USD.issue());
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = channel(alice, bob, env.seq(alice));
            env(create(alice, bob, USD(1000), settleDelay, pk));
            env.close();
            auto const transferRate = channelRate(*env.current(), chan);
            BEAST_EXPECT(
                transferRate.value == std::uint32_t(1000000000 * 1.25));

            // alice can claim paychan
            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            auto const delta = USD(125);
            auto reqBal = chanBal + delta;
            auto authAmt = reqBal + USD(500);
            env(claim(alice, chan, reqBal, authAmt));
            env.close();
            auto const postLocked = -lockedAmount(env, alice, gw, USD);
            BEAST_EXPECT(postLocked == USD(875));
            BEAST_EXPECT(env.balance(alice, USD.issue()) == preAlice - delta);
            BEAST_EXPECT(env.balance(bob, USD.issue()) == USD(10100));
        }
        // test rate change
        {
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, gw);
            env(rate(gw, 1.25));
            env.close();
            env.trust(USD(100000), alice);
            env.trust(USD(100000), bob);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();

            // alice can create paychan w/ xfer rate
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = channel(alice, bob, env.seq(alice));
            env(create(alice, bob, USD(1000), settleDelay, pk));
            env.close();
            auto transferRate = channelRate(*env.current(), chan);
            BEAST_EXPECT(
                transferRate.value == std::uint32_t(1000000000 * 1.25));

            auto const preAlice = env.balance(alice, USD.issue());
            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            auto const delta = USD(100);
            auto reqBal = chanBal + delta;
            auto authAmt = reqBal + USD(500);

            // alice can fund paychan
            env(fund(alice, chan, USD(1000)));
            env.close();
            transferRate = channelRate(*env.current(), chan);
            // no change in rate
            BEAST_EXPECT(
                transferRate.value == std::uint32_t(1000000000 * 1.25));

            // issuer changes rate lower
            env(rate(gw, 1.00));
            env.close();

            // alice can fund after issuer rate change
            env(fund(alice, chan, USD(1000)));
            env.close();
            transferRate = channelRate(*env.current(), chan);
            // rate changed to new lower rate
            BEAST_EXPECT(
                transferRate.value == std::uint32_t(1000000000 * 1.00));

            // issuer changes rate higher
            env(rate(gw, 1.01));
            env.close();

            // alice cant fund after issuer rate change
            env(fund(alice, chan, USD(1000)), ter(temBAD_TRANSFER_RATE));
            env.close();
            transferRate = channelRate(*env.current(), chan);
            // issuer rate stays at lower rate
            BEAST_EXPECT(
                transferRate.value == std::uint32_t(1000000000 * 1.00));
        }
        // test issuer doesnt pay own rate
        {
            Env env{*this, features};
            env.fund(XRP(10000), alice, gw);
            env(rate(gw, 1.25));
            env.close();
            env.trust(USD(100000), alice);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env.close();

            // issuer with rate can create paychan
            auto const pk = gw.pk();
            auto const settleDelay = 100s;
            auto const chan = channel(gw, alice, env.seq(gw));
            env(create(gw, alice, USD(1000), settleDelay, pk));
            env.close();

            // issuer can claim paychan, alice has trustline
            auto const preLocked = -lockedAmount(env, alice, gw, USD);
            auto const preAlice = env.balance(alice, USD.issue());
            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            auto const delta = USD(500);
            auto const reqBal = chanBal + delta;
            auto const authAmt = reqBal + USD(100);
            env(claim(gw, chan, reqBal, authAmt));
            env.close();
            auto const postLocked = -lockedAmount(env, alice, gw, USD);
            BEAST_EXPECT(postLocked == USD(0));
            BEAST_EXPECT(env.balance(alice, USD.issue()) == preAlice + delta);
        }
    }

    void
    testICTLLimitAmount(FeatureBitset features)
    {
        testcase("IC Trustline Limit Amount");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        auto const aliceUSD = alice["USD"];
        auto const bobUSD = bob["USD"];

        // test LimitAmount
        {
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, gw);
            env.close();
            env.trust(USD(1000), alice);
            env.trust(USD(1000), bob);
            env.close();
            env(pay(gw, alice, USD(1000)));
            env(pay(gw, bob, USD(1000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = channel(alice, bob, env.seq(alice));
            env(create(alice, bob, USD(1000), settleDelay, pk));
            env.close();
            BEAST_EXPECT(channelBalance(*env.current(), chan) == USD(0));
            BEAST_EXPECT(channelAmount(*env.current(), chan) == USD(1000));
            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            BEAST_EXPECT(chanBal == USD(0));
            BEAST_EXPECT(chanAmt == USD(1000));
            auto preBob = env.balance(bob);
            auto const delta = USD(50);
            auto reqBal = chanBal + delta;
            auto authAmt = reqBal + USD(100);
            assert(reqBal <= chanAmt);
            auto const preLocked = -lockedAmount(env, alice, gw, USD);
            BEAST_EXPECT(preLocked == USD(1000));
            // alice cannot claim because bobs amount would be > than limit
            env(claim(alice, chan, reqBal, authAmt), ter(tecPATH_DRY));

            // bob can claim, increasing the limit amount
            // auto const preBobLimit = limitAmount(env, bob, gw, USD);
            auto const sig =
                signClaimICAuth(alice.pk(), alice.sk(), chan, authAmt);
            env(claim(bob, chan, reqBal, authAmt, Slice(sig), alice.pk()));
            env.close();
            // auto const postBobLimit = limitAmount(env, bob, gw, USD);
            // bobs limit is NOT changed
            // BEAST_EXPECT(postBobLimit == preBobLimit);
        }
    }

    void
    testICTLRequireAuth(FeatureBitset features)
    {
        testcase("IC Trustline Require Auth");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        auto const aliceUSD = alice["USD"];
        auto const bobUSD = bob["USD"];

        // test asfRequireAuth
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, gw);
            env(fset(gw, asfRequireAuth));
            env.close();
            env(trust(gw, aliceUSD(10000)), txflags(tfSetfAuth));
            env(trust(alice, USD(10000)));
            env.close();
            env(pay(gw, alice, USD(1000)));
            env.close();

            // auto const gwLimit = limitAmount(env, gw, alice, aliceUSD);
            // auto const aliceLimit = limitAmount(env, alice, gw, USD);

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto chan = channel(alice, bob, env.seq(alice));
            // alice cannot create because bob's trustline is not authorized
            // all parties must be authorized
            env(create(alice, bob, USD(1000), settleDelay, pk),
                ter(tecNO_AUTH));
            env.close();

            env(trust(gw, bobUSD(10000)), txflags(tfSetfAuth));
            env(trust(bob, USD(10000)));
            env.close();
            env(pay(gw, bob, USD(1000)));
            env.close();

            // alice can now create because bob's trustline is authorized
            chan = channel(alice, bob, env.seq(alice));
            env(create(alice, bob, USD(1000), settleDelay, pk));
            env.close();

            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            auto const delta = USD(50);
            auto const reqBal = delta;
            auto const authAmt = reqBal + USD(100);
            // alice can claim
            env(claim(alice, chan, reqBal, authAmt));
            env.close();

            // bob can claim
            chanBal = channelBalance(*env.current(), chan);
            chanAmt = channelAmount(*env.current(), chan);
            auto const newReqBal = chanBal + delta;
            auto const newAuthAmt = newReqBal + USD(100);
            auto const sig =
                signClaimICAuth(alice.pk(), alice.sk(), chan, newAuthAmt);
            env(claim(
                bob, chan, newReqBal, newAuthAmt, Slice(sig), alice.pk()));
            env.close();
        }
    }
    void
    testICTLFreeze(FeatureBitset features)
    {
        testcase("IC Trustline Freeze");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        auto const aliceUSD = alice["USD"];
        auto const bobUSD = bob["USD"];
        // test Global Freeze
        {
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, gw);
            env.close();
            env.trust(USD(100000), alice);
            env.trust(USD(100000), bob);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();
            env(fset(gw, asfGlobalFreeze));
            env.close();
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto chan = channel(alice, bob, env.seq(alice));
            env(create(alice, bob, USD(1000), settleDelay, pk), ter(tecFROZEN));
            env.close();
            env(fclear(gw, asfGlobalFreeze));
            env.close();
            chan = channel(alice, bob, env.seq(alice));
            env(create(alice, bob, USD(1000), settleDelay, pk));
            env.close();
            env(fset(gw, asfGlobalFreeze));
            env.close();
            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            auto const delta = USD(10);
            auto reqBal = chanBal + delta;
            auto authAmt = reqBal + USD(100);
            env(claim(alice, chan, reqBal, authAmt), ter(tecFROZEN));
            auto sig = signClaimICAuth(alice.pk(), alice.sk(), chan, authAmt);
            env(claim(bob, chan, reqBal, authAmt, Slice(sig), alice.pk()),
                ter(tecFROZEN));
            env.close();
            env(fclear(gw, asfGlobalFreeze));
            env.close();
            env(claim(alice, chan, reqBal, authAmt));
            env.close();
            chanBal = channelBalance(*env.current(), chan);
            chanAmt = channelAmount(*env.current(), chan);
            reqBal = chanBal + delta;
            authAmt = reqBal + USD(100);
            sig = signClaimICAuth(alice.pk(), alice.sk(), chan, authAmt);
            env(claim(bob, chan, reqBal, authAmt, Slice(sig), alice.pk()));
            env.close();
        }
        // test Individual Freeze
        {
            // Env Setup
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, gw);
            env.close();
            env(trust(alice, USD(100000)));
            env(trust(bob, USD(100000)));
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();

            // set freeze on alice trustline
            env(trust(gw, USD(100000), alice, tfSetFreeze));
            env.close();

            // setup transaction
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto chan = channel(alice, bob, env.seq(alice));

            // create paychan fails - frozen trustline
            env(create(alice, bob, USD(1000), settleDelay, pk), ter(tecFROZEN));
            env.close();

            // clear freeze on alice trustline
            env(trust(gw, USD(100000), alice, tfClearFreeze));
            env.close();

            // create paychan success
            chan = channel(alice, bob, env.seq(alice));
            env(create(alice, bob, USD(1000), settleDelay, pk));
            env.close();

            // set freeze on alice trustline
            env(trust(gw, USD(100000), alice, tfSetFreeze));
            env.close();

            // paychan fields
            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            auto const delta = USD(10);
            auto reqBal = chanBal + delta;
            auto authAmt = reqBal + USD(100);

            // alice claim paychan fails - frozen trustline
            env(claim(alice, chan, reqBal, authAmt), ter(tecFROZEN));

            // bob claim paychan fails - frozen trustline
            auto sig = signClaimICAuth(alice.pk(), alice.sk(), chan, authAmt);
            env(claim(bob, chan, reqBal, authAmt, Slice(sig), alice.pk()),
                ter(tecFROZEN));
            env.close();

            // clear freeze on bob trustline
            env(trust(gw, USD(100000), bob, tfClearFreeze));
            // clear freeze on alice trustline
            env(trust(gw, USD(100000), alice, tfClearFreeze));
            env.close();

            // alice claim paychan success
            env(claim(alice, chan, reqBal, authAmt));
            env.close();

            // paychan fields
            chanBal = channelBalance(*env.current(), chan);
            chanAmt = channelAmount(*env.current(), chan);
            reqBal = chanBal + delta;
            authAmt = reqBal + USD(100);

            // bob claim paychan success
            sig = signClaimICAuth(alice.pk(), alice.sk(), chan, authAmt);
            env(claim(bob, chan, reqBal, authAmt, Slice(sig), alice.pk()));
            env.close();
        }
    }
    void
    testICTLINSF(FeatureBitset features)
    {
        testcase("IC Trustline Insuficient Funds");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        auto const aliceUSD = alice["USD"];
        auto const bobUSD = bob["USD"];
        {
            // test pay more than locked amount
            // ie. has 10000, lock 1000 then try to pay 10000
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, gw);
            env.close();
            env.trust(USD(100000), alice);
            env.trust(USD(100000), bob);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = channel(alice, bob, env.seq(alice));
            env(create(alice, bob, USD(1000), settleDelay, pk));
            env.close();
            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            BEAST_EXPECT(chanBal == USD(0));
            BEAST_EXPECT(chanAmt == USD(1000));
            auto preBob = env.balance(bob);
            auto const delta = USD(50);
            auto reqBal = chanBal + delta;
            auto authAmt = reqBal + USD(100);
            assert(reqBal <= chanAmt);
            auto const preLocked = -lockedAmount(env, alice, gw, USD);
            BEAST_EXPECT(preLocked == USD(1000));
            env(pay(alice, gw, USD(10000)), ter(tecPATH_PARTIAL));
        }
        {
            // test lock more than balance + locked
            // ie. has 10000 lock 1000 then try to lock 10000
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, gw);
            env.close();
            env.trust(USD(100000), alice);
            env.trust(USD(100000), bob);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = channel(alice, bob, env.seq(alice));
            env(create(alice, bob, USD(1000), settleDelay, pk));
            env.close();
            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            BEAST_EXPECT(chanBal == USD(0));
            BEAST_EXPECT(chanAmt == USD(1000));
            auto preBob = env.balance(bob);
            auto const delta = USD(50);
            auto reqBal = chanBal + delta;
            auto authAmt = reqBal + USD(100);
            assert(reqBal <= chanAmt);
            auto const preLocked = -lockedAmount(env, alice, gw, USD);
            BEAST_EXPECT(preLocked == USD(1000));
            env(create(alice, bob, USD(10000), settleDelay, pk),
                ter(tecINSUFFICIENT_FUNDS));
        }
    }

    void
    testICMismatchFunding(FeatureBitset features)
    {
        testcase("IC Mismatch Funding");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        auto const EUR = gw["EUR"];

        {
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, gw);
            env.close();
            env.trust(USD(100000), alice);
            env.trust(USD(100000), bob);
            env.trust(EUR(100000), alice);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env(pay(gw, alice, EUR(10000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = channel(alice, bob, env.seq(alice));
            env(create(alice, bob, USD(1000), settleDelay, pk));
            env.close();
            env(fund(alice, chan, EUR(1000)), ter(temBAD_CURRENCY));
        }
    }

    void
    testICPrecisionLoss(FeatureBitset features)
    {
        testcase("IC Precision Loss");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        // test max trustline precision loss
        {
            Env env(*this, features);
            env.fund(XRP(10000), alice, bob, gw);
            env.close();
            env.trust(USD(100000000000000000), alice);
            env.trust(USD(100000000000000000), bob);
            env.close();
            env(pay(gw, alice, USD(10000000000000000)));
            env(pay(gw, bob, USD(10000000000000000)));
            env.close();

            // setup tx
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = channel(alice, bob, env.seq(alice));

            // create paychan fails - precision loss
            env(create(alice, bob, USD(10000000000000000), settleDelay, pk));
            env.close();

            // alice cannot fund again - precision loss
            env(fund(alice, chan, USD(1)), ter(tecPRECISION_LOSS));
            env.close();

            // setup tx
            auto const preAlice = env.balance(alice, USD.issue());
            auto const preLocked = -lockedAmount(env, alice, gw, USD);
            auto chanBal = channelBalance(*env.current(), chan);
            auto chanAmt = channelAmount(*env.current(), chan);
            auto const delta = USD(10000000000000000);
            auto reqBal = chanBal + delta;

            // create paychan success
            env(claim(alice, chan, reqBal, reqBal));
            env.close();
            auto postLocked = -lockedAmount(env, alice, gw, USD);
            BEAST_EXPECT(postLocked == USD(0));

            // alice can fund again
            env(pay(gw, alice, USD(1)));
            env.close();
            env(fund(alice, chan, USD(1)));
            env.close();
            postLocked = -lockedAmount(env, alice, gw, USD);
            BEAST_EXPECT(postLocked == USD(1));
        }
        // test min create precision loss
        {
            Env env(*this, features);
            env.fund(XRP(10000), alice, bob, gw);
            env.close();
            env.trust(USD(1000000000000000000), alice);
            env.trust(USD(1000000000000000000), bob);
            env.close();
            env(pay(gw, alice, USD(100000000000000000)));
            env(pay(gw, bob, USD(1)));
            env.close();
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            // alice cannot create paychan for 1/10/100 token - precision loss
            env(create(alice, bob, USD(1), settleDelay, pk),
                ter(tecPRECISION_LOSS));
            env.close();
            // alice can create paychan for 1000 token
            auto const chan = channel(alice, bob, env.seq(alice));
            env(create(alice, bob, USD(10000), settleDelay, pk));
            env.close();

            auto const chanBal = channelBalance(*env.current(), chan);
            auto const chanAmt = channelAmount(*env.current(), chan);
            auto reqBal = USD(1000);
            auto authAmt = reqBal + USD(1000);
            auto sig = signClaimICAuth(alice.pk(), alice.sk(), chan, authAmt);
            env(claim(bob, chan, reqBal, authAmt, Slice(sig), alice.pk()),
                ter(tecPRECISION_LOSS));

            reqBal = USD(10000);
            authAmt = reqBal + USD(10000);
            sig = signClaimICAuth(alice.pk(), alice.sk(), chan, authAmt);
            env(claim(bob, chan, reqBal, authAmt, Slice(sig), alice.pk()));
        }
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testSimple(features);
        testCancelAfter(features);
        testSettleDelay(features);
        testExpiration(features);
        testCloseDry(features);
        testDefaultAmount(features);
        testDisallowXRP(features);
        testDstTag(features);
        testDepositAuth(features);
        testMultiple(features);
        testAccountChannelsRPC(features);
        testAccountChannelsRPCMarkers(features);
        testAccountChannelsRPCSenderOnly(features);
        testAuthVerifyRPC(features);
        testOptionalFields(features);
        testMalformedPK(features);
        testMetaAndOwnership(features);
        testAccountDelete(features);
        testUsingTickets(features);
        testICSimple(features);
        testICCancelAfter(features);
        testICSettleDelay(features);
        testICExpiration(features);
        testICCloseDry(features);
        testICDefaultAmount(features);
        testICDisallowXRP(features);
        testICDstTag(features);
        testICDepositAuth(features);
        testICMultiple(features);
        testICAccountChannelsRPC(features);
        testICAccountChannelsRPCMarkers(features);
        testICAccountChannelsRPCSenderOnly(features);
        testICAuthVerifyRPC(features);
        testICOptionalFields(features);
        testICMalformedPK(features);
        testICMetaAndOwnership(features);
        testICAccountDelete(features);
        testICUsingTickets(features);
        testICAutoTL(features);
        testICRippleState(features);
        testICGateway(features);
        testICLockedRate(features);
        testICTLRequireAuth(features);
        testICTLFreeze(features);
        testICTLINSF(features);
        testICMismatchFunding(features);
        testICPrecisionLoss(features);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{supported_amendments()};
        testWithFeats(all - disallowIncoming);
        testWithFeats(all);
    }
};

BEAST_DEFINE_TESTSUITE(PayChan, app, ripple);
}  // namespace test
}  // namespace ripple

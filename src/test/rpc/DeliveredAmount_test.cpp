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

#include <ripple/basics/chrono.h>
#include <ripple/beast/unit_test.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/jss.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/DeliveredAmount.h>
#include <test/jtx.h>
#include <test/jtx/WSClient.h>

namespace ripple {
namespace test {

// Helper class to track the expected number `delivered_amount` results.
class CheckDeliveredAmount
{
    // If the test occurs before or after the switch time
    bool afterSwitchTime_;
    // number of payments expected 'delivered_amount' available
    int numExpectedAvailable_ = 0;
    // Number of payments with field with `delivered_amount` set to the
    // string "unavailable"
    int numExpectedSetUnavailable_ = 0;
    // Number of payments with no `delivered_amount` field
    int numExpectedNotSet_ = 0;

    // Increment one of the expected numExpected{Available_, Unavailable_,
    // NotSet_} values. Which value to increment depends on: 1) If the ledger is
    // before or after the switch time 2) If the tx is a partial payment 3) If
    // the payment is successful or not
    void
    adjCounters(bool success, bool partial)
    {
        if (!success)
        {
            ++numExpectedNotSet_;
            return;
        }
        if (!afterSwitchTime_)
        {
            if (partial)
                ++numExpectedAvailable_;
            else
                ++numExpectedSetUnavailable_;
            return;
        }
        // normal case: after switch time & successful transaction
        ++numExpectedAvailable_;
    }

public:
    explicit CheckDeliveredAmount(bool afterSwitchTime)
        : afterSwitchTime_(afterSwitchTime)
    {
    }

    void
    adjCountersSuccess()
    {
        adjCounters(true, false);
    }

    void
    adjCountersFail()
    {
        adjCounters(false, false);
    }
    void
    adjCountersPartialPayment()
    {
        adjCounters(true, true);
    }

    // After all the txns are checked, all the `numExpected` variables should be
    // zero. The `checkTxn` function decrements these variables.
    bool
    checkExpectedCounters() const
    {
        return !numExpectedAvailable_ && !numExpectedNotSet_ &&
            !numExpectedSetUnavailable_;
    }

    // Check if the transaction has `delivered_amount` in the metaData as
    // expected from our rules. Decrements the appropriate `numExpected`
    // variable. After all the txns are checked, all the `numExpected` variables
    // should be zero.
    bool
    checkTxn(Json::Value const& t, Json::Value const& metaData)
    {
        if (t[jss::TransactionType].asString() != jss::Payment)
            return true;

        bool isSet = metaData.isMember(jss::delivered_amount);
        bool isSetUnavailable = false;
        bool isSetAvailable = false;
        if (isSet)
        {
            if (metaData[jss::delivered_amount] != "unavailable")
                isSetAvailable = true;
            else
                isSetUnavailable = true;
        }
        if (isSetAvailable)
            --numExpectedAvailable_;
        else if (isSetUnavailable)
            --numExpectedSetUnavailable_;
        else if (!isSet)
            --numExpectedNotSet_;

        if (isSet)
        {
            if (metaData.isMember(sfDeliveredAmount.jsonName))
            {
                if (metaData[jss::delivered_amount] !=
                    metaData[sfDeliveredAmount.jsonName])
                    return false;
            }
            else
            {
                if (afterSwitchTime_)
                {
                    if (metaData[jss::delivered_amount] != t[jss::Amount])
                        return false;
                }
                else
                {
                    if (metaData[jss::delivered_amount] != "unavailable")
                        return false;
                }
            }
        }

        if (metaData[sfTransactionResult.jsonName] != "tesSUCCESS")
        {
            if (isSet)
                return false;
        }
        else
        {
            if (afterSwitchTime_)
            {
                if (!isSetAvailable)
                    return false;
            }
            else
            {
                if (metaData.isMember(sfDeliveredAmount.jsonName))
                {
                    if (!isSetAvailable)
                        return false;
                }
                else
                {
                    if (!isSetUnavailable)
                        return false;
                }
            }
        }
        return true;
    }
};

class DeliveredAmount_test : public beast::unit_test::suite
{
    void
    testAccountDeliveredAmountSubscribe()
    {
        testcase("Ledger Request Subscribe DeliveredAmount");

        using namespace test::jtx;
        using namespace std::chrono_literals;

        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        auto const gw = Account("gateway");
        auto const USD = gw["USD"];

        for (bool const afterSwitchTime : {true, false})
        {
            Env env{*this};
            env.fund(XRP(10000), alice, bob, carol, gw);
            env.trust(USD(1000), alice, bob, carol);
            if (afterSwitchTime)
                env.close(NetClock::time_point{446000000s});
            else
                env.close();

            CheckDeliveredAmount checkDeliveredAmount{afterSwitchTime};
            {
                // add payments, but do no close until subscribed

                // normal payments
                env(pay(gw, alice, USD(50)));
                checkDeliveredAmount.adjCountersSuccess();
                env(pay(gw, alice, XRP(50)));
                checkDeliveredAmount.adjCountersSuccess();

                // partial payment
                env(pay(gw, bob, USD(9999999)), txflags(tfPartialPayment));
                checkDeliveredAmount.adjCountersPartialPayment();
                env.require(balance(bob, USD(1000)));

                // failed payment
                env(pay(bob, carol, USD(9999999)), ter(tecPATH_PARTIAL));
                checkDeliveredAmount.adjCountersFail();
                env.require(balance(carol, USD(0)));
            }

            auto wsc = makeWSClient(env.app().config());

            {
                Json::Value stream;
                // RPC subscribe to ledger stream
                stream[jss::streams] = Json::arrayValue;
                stream[jss::streams].append("ledger");
                stream[jss::accounts] = Json::arrayValue;
                stream[jss::accounts].append(toBase58(alice.id()));
                stream[jss::accounts].append(toBase58(bob.id()));
                stream[jss::accounts].append(toBase58(carol.id()));
                auto jv = wsc->invoke("subscribe", stream);
                if (wsc->version() == 2)
                {
                    BEAST_EXPECT(
                        jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                    BEAST_EXPECT(
                        jv.isMember(jss::ripplerpc) &&
                        jv[jss::ripplerpc] == "2.0");
                    BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
                }
                BEAST_EXPECT(jv[jss::result][jss::ledger_index] == 3);
            }
            {
                env.close();
                // Check stream update
                while (true)
                {
                    auto const r = wsc->findMsg(1s, [&](auto const& jv) {
                        return jv[jss::ledger_index] == 4;
                    });
                    if (!r)
                        break;

                    if (!r->isMember(jss::transaction))
                        continue;

                    BEAST_EXPECT(checkDeliveredAmount.checkTxn(
                        (*r)[jss::transaction], (*r)[jss::meta]));
                }
            }
            BEAST_EXPECT(checkDeliveredAmount.checkExpectedCounters());
        }
    }

    void
    testTxDeliveredAmountRPC()
    {
        testcase("Ledger Request RPC DeliveredAmount");

        using namespace test::jtx;
        using namespace std::chrono_literals;

        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        auto const gw = Account("gateway");
        auto const USD = gw["USD"];

        for (bool const afterSwitchTime : {true, false})
        {
            Env env{*this};
            env.fund(XRP(10000), alice, bob, carol, gw);
            env.trust(USD(1000), alice, bob, carol);
            if (afterSwitchTime)
                env.close(NetClock::time_point{446000000s});
            else
                env.close();

            CheckDeliveredAmount checkDeliveredAmount{afterSwitchTime};
            // normal payments
            env(pay(gw, alice, USD(50)));
            checkDeliveredAmount.adjCountersSuccess();
            env(pay(gw, alice, XRP(50)));
            checkDeliveredAmount.adjCountersSuccess();

            // partial payment
            env(pay(gw, bob, USD(9999999)), txflags(tfPartialPayment));
            checkDeliveredAmount.adjCountersPartialPayment();
            env.require(balance(bob, USD(1000)));

            // failed payment
            env(pay(gw, carol, USD(9999999)), ter(tecPATH_PARTIAL));
            checkDeliveredAmount.adjCountersFail();
            env.require(balance(carol, USD(0)));

            env.close();
            std::string index;
            Json::Value jvParams;
            jvParams[jss::ledger_index] = 4u;
            jvParams[jss::transactions] = true;
            jvParams[jss::expand] = true;
            auto const jtxn = env.rpc(
                "json",
                "ledger",
                to_string(
                    jvParams))[jss::result][jss::ledger][jss::transactions];
            for (auto const& t : jtxn)
                BEAST_EXPECT(
                    checkDeliveredAmount.checkTxn(t, t[jss::metaData]));
            BEAST_EXPECT(checkDeliveredAmount.checkExpectedCounters());
        }
    }

    void
    testGetDeliveredAmount()
    {
        testcase("getDeliveredAmount");

        using namespace test::jtx;
        using namespace std::chrono_literals;

        Account const alice("alice");
        Account const brynn("brynn");
        Account const carol("carol");
        Account const gw("gateway");
        auto const USD = gw["USD"];
        auto const BTC = gw["BTC"];

        Env env{*this};

        // Build an RPC::Context we can use to call the entry point.
        Resource::Charge charge = Resource::feeReferenceRPC;
        Resource::Consumer consumer;
        RPC::Context const context{
            env.app().journal("DeliveredAmountTest"),
            env.app(),
            charge,
            env.app().getOPs(),
            env.app().getLedgerMaster(),
            consumer,
            Role::GUEST,
            nullptr,
            nullptr,
            1};

        env.fund(XRP(10000), alice, brynn, carol, gw);

        // Make sure the ledger close time is after the DeliveredAmount code
        // went live.
        env.close(NetClock::time_point{446000000s});

        // trust 2e90)
        env(trust(alice, STAmount(USD.issue(), 2, 90)));
        env(trust(brynn, STAmount(BTC.issue(), 2, 90)));
        env.close();

        // initial balance 1e90
        env(pay(gw, alice, STAmount(USD.issue(), 1, 90)));
        env(pay(gw, brynn, STAmount(BTC.issue(), 1, 90)));
        env.close();

        // Make a payment so small relative to the current trust line that
        // no funds actually move.
        {
            env(pay(gw, alice, USD(1)));
            std::shared_ptr<STTx const> tx = env.tx();
            std::shared_ptr<STObject const> meta = env.meta();
            env.close();

            LedgerIndex const ledgerIndex = env.closed()->seq();
            if (BEAST_EXPECT(tx && meta))
            {
                {
                    // Expect a nullopt because the tx has no DeliverMin and
                    // no Amount.
                    auto hackedTx = std::make_shared<STTx>(*tx);
                    hackedTx->makeFieldAbsent(sfAmount);
                    TxMeta const txMeta(
                        hackedTx->getTransactionID(), ledgerIndex, *meta);
                    BEAST_EXPECT(
                        getDeliveredAmount(
                            context, hackedTx, txMeta, ledgerIndex) ==
                        std::nullopt);
                }
                {
                    // Expect a nullopt because the tx has no Destination.
                    auto hackedTx = std::make_shared<STTx>(*tx);
                    hackedTx->makeFieldAbsent(sfDestination);
                    TxMeta const txMeta(
                        hackedTx->getTransactionID(), ledgerIndex, *meta);
                    BEAST_EXPECT(
                        getDeliveredAmount(
                            context, hackedTx, txMeta, ledgerIndex) ==
                        std::nullopt);
                }
                {
                    // Expect a nullopt because the trust line did not
                    // actually change.
                    TxMeta const txMeta(
                        tx->getTransactionID(), ledgerIndex, *meta);
                    BEAST_EXPECT(
                        getDeliveredAmount(context, tx, txMeta, ledgerIndex) ==
                        std::nullopt);
                }
            }
        }
        // Cash a check so small relative to the current trust line that no
        // funds actually move.
        {
            auto const checkId = keylet::check(gw, env.seq(gw)).key;
            env(check::create(gw, brynn, BTC(1)));
            env.close();

            env(check::cash(brynn, checkId, check::DeliverMin(BTC(1))));
            std::shared_ptr<STTx const> tx = env.tx();
            std::shared_ptr<STObject const> meta = env.meta();
            env.close();

            LedgerIndex const ledgerIndex = env.closed()->seq();
            if (BEAST_EXPECT(tx && meta))
            {
                // Expect a nullopt because the cashed check was too small to
                // modify the trust line.
                TxMeta const txMeta(tx->getTransactionID(), ledgerIndex, *meta);
                BEAST_EXPECT(
                    getDeliveredAmount(context, tx, txMeta, ledgerIndex) ==
                    std::nullopt);
            }
        }
        // Make sure that a reasonable check cash that creates a trust line
        // returns the correct delivered_amount.
        {
            auto const checkId = keylet::check(brynn, env.seq(brynn)).key;
            env(check::create(brynn, carol, BTC(10)));
            env.close();

            env(check::cash(carol, checkId, check::DeliverMin(BTC(10))));
            std::shared_ptr<STTx const> tx = env.tx();
            std::shared_ptr<STObject const> meta = env.meta();
            env.close();

            LedgerIndex const ledgerIndex = env.closed()->seq();
            if (BEAST_EXPECT(tx && meta))
            {
                TxMeta const txMeta(tx->getTransactionID(), ledgerIndex, *meta);
                std::optional<STAmount> const delivered =
                    getDeliveredAmount(context, tx, txMeta, ledgerIndex);
                BEAST_EXPECT(delivered && delivered.value() == BTC(10));
            }
        }
        // Test getDeliveredAmount with a multi-hop payment.
        {
            // carol creates an offer selling BTC for USD.  alice uses that
            // offer in a multi-step payment.
            env(offer(carol, USD(1), BTC(1)));
            env.close();

            env(pay(alice, brynn, BTC(1)), path(~BTC), sendmax(USD(1)));
            std::shared_ptr<STTx const> tx = env.tx();
            std::shared_ptr<STObject const> meta = env.meta();
            env.close();

            // We can show that carol actually sent BTC(1) to brynn, even
            // though tbrynn's balance did not change, because carol's
            // balance is now BTC(9).
            env.require(balance(carol, USD(1)));
            env.require(balance(carol, BTC(9)));

            LedgerIndex const ledgerIndex = env.closed()->seq();
            if (BEAST_EXPECT(tx && meta))
            {
                // Expect a nullopt because the BTC that brynn received
                // is too small to register in brynn's trust line.
                TxMeta const txMeta(tx->getTransactionID(), ledgerIndex, *meta);
                BEAST_EXPECT(
                    getDeliveredAmount(context, tx, txMeta, ledgerIndex) ==
                    std::nullopt);
            }
        }
    }

public:
    void
    run() override
    {
        testAccountDeliveredAmountSubscribe();
        testTxDeliveredAmountRPC();
        testGetDeliveredAmount();
    }
};

BEAST_DEFINE_TESTSUITE(DeliveredAmount, app, ripple);

}  // namespace test
}  // namespace ripple

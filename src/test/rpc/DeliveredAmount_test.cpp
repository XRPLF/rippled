#include <test/jtx.h>
#include <test/jtx/WSClient.h>

#include <xrpl/beast/unit_test.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/jss.h>

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
            auto cfg = envconfig();
            cfg->FEES.reference_fee = 10;
            Env env(*this, std::move(cfg));
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
            auto cfg = envconfig();
            cfg->FEES.reference_fee = 10;
            Env env(*this, std::move(cfg));
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
    testMPTDeliveredAmountRPC(FeatureBitset features)
    {
        testcase("MPT DeliveredAmount");

        using namespace jtx;
        Account const alice("alice");
        Account const carol("carol");
        Account const bob("bob");
        Env env{*this, features};

        MPTTester mptAlice(
            env, alice, {.holders = {bob, carol}, .close = false});

        mptAlice.create(
            {.transferFee = 25000,
             .ownerCount = 1,
             .holderCount = 0,
             .flags = tfMPTCanTransfer});
        auto const MPT = mptAlice["MPT"];

        mptAlice.authorize({.account = bob});
        mptAlice.authorize({.account = carol});

        // issuer to holder
        mptAlice.pay(alice, bob, 10000);

        // holder to holder
        env(pay(bob, carol, mptAlice.mpt(1000)), txflags(tfPartialPayment));
        env.close();

        // Get the hash for the most recent transaction.
        std::string txHash{
            env.tx()->getJson(JsonOptions::none)[jss::hash].asString()};
        Json::Value meta = env.rpc("tx", txHash)[jss::result][jss::meta];

        if (features[fixMPTDeliveredAmount])
        {
            BEAST_EXPECT(
                meta[sfDeliveredAmount.jsonName] ==
                STAmount{MPT(800)}.getJson(JsonOptions::none));
            BEAST_EXPECT(
                meta[jss::delivered_amount] ==
                STAmount{MPT(800)}.getJson(JsonOptions::none));
        }
        else
        {
            BEAST_EXPECT(!meta.isMember(sfDeliveredAmount.jsonName));
            BEAST_EXPECT(
                meta[jss::delivered_amount] = Json::Value("unavailable"));
        }

        env(pay(bob, carol, MPT(1000)),
            sendmax(MPT(1200)),
            txflags(tfPartialPayment));
        env.close();

        txHash = env.tx()->getJson(JsonOptions::none)[jss::hash].asString();
        meta = env.rpc("tx", txHash)[jss::result][jss::meta];

        if (features[fixMPTDeliveredAmount])
        {
            BEAST_EXPECT(
                meta[sfDeliveredAmount.jsonName] ==
                STAmount{MPT(960)}.getJson(JsonOptions::none));
            BEAST_EXPECT(
                meta[jss::delivered_amount] ==
                STAmount{MPT(960)}.getJson(JsonOptions::none));
        }
        else
        {
            BEAST_EXPECT(!meta.isMember(sfDeliveredAmount.jsonName));
            BEAST_EXPECT(
                meta[jss::delivered_amount] = Json::Value("unavailable"));
        }
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{testable_amendments()};

        testTxDeliveredAmountRPC();
        testAccountDeliveredAmountSubscribe();

        testMPTDeliveredAmountRPC(all - fixMPTDeliveredAmount);
        testMPTDeliveredAmountRPC(all);
    }
};

BEAST_DEFINE_TESTSUITE(DeliveredAmount, rpc, ripple);

}  // namespace test
}  // namespace ripple

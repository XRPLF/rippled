#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>  // IWYU pragma: keep
#include <test/jtx/envconfig.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/pay.h>
#include <test/jtx/seq.h>
#include <test/jtx/sig.h>
#include <test/jtx/tags.h>
#include <test/jtx/trust.h>

#include <xrpld/app/misc/TxQ.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/DecayingSample.h>
#include <xrpl/beast/clock/abstract_clock.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/tokens.h>
#include <xrpl/resource/ResourceManager.h>
#include <xrpl/resource/detail/Entry.h>
#include <xrpl/resource/detail/Tuning.h>

#include <boost/algorithm/string/predicate.hpp>

#include <chrono>
#include <string>

namespace xrpl {

class NoRippleCheck_test : public beast::unit_test::Suite
{
    void
    testBadInput()
    {
        testcase("Bad input to noripple_check");

        using namespace test::jtx;
        Env env{*this};

        auto const alice = Account{"alice"};
        env.fund(XRP(10000), alice);
        env.close();

        {  // missing account field
            auto const result = env.rpc("json", "noripple_check", "{}")[jss::result];
            BEAST_EXPECT(result[jss::error] == "invalidParams");
            BEAST_EXPECT(result[jss::error_message] == "Missing field 'account'.");
        }

        {  // missing role field
            json::Value params;
            params[jss::account] = alice.human();
            auto const result = env.rpc("json", "noripple_check", to_string(params))[jss::result];
            BEAST_EXPECT(result[jss::error] == "invalidParams");
            BEAST_EXPECT(result[jss::error_message] == "Missing field 'role'.");
        }

        // test account non-string
        {
            auto testInvalidAccountParam = [&](auto const& param) {
                json::Value params;
                params[jss::account] = param;
                params[jss::role] = "user";
                auto jrr = env.rpc("json", "noripple_check", to_string(params))[jss::result];
                BEAST_EXPECT(jrr[jss::error] == "invalidParams");
                BEAST_EXPECT(jrr[jss::error_message] == "Invalid field 'account'.");
            };

            testInvalidAccountParam(1);
            testInvalidAccountParam(1.1);
            testInvalidAccountParam(true);
            testInvalidAccountParam(json::Value(json::ValueType::Null));
            testInvalidAccountParam(json::Value(json::ValueType::Object));
            testInvalidAccountParam(json::Value(json::ValueType::Array));
        }

        {  // invalid role field
            json::Value params;
            params[jss::account] = alice.human();
            params[jss::role] = "not_a_role";
            auto const result = env.rpc("json", "noripple_check", to_string(params))[jss::result];
            BEAST_EXPECT(result[jss::error] == "invalidParams");
            BEAST_EXPECT(result[jss::error_message] == "Invalid field 'role'.");
        }

        {  // invalid limit
            json::Value params;
            params[jss::account] = alice.human();
            params[jss::role] = "user";
            params[jss::limit] = -1;
            auto const result = env.rpc("json", "noripple_check", to_string(params))[jss::result];
            BEAST_EXPECT(result[jss::error] == "invalidParams");
            BEAST_EXPECT(
                result[jss::error_message] == "Invalid field 'limit', not unsigned integer.");
        }

        {  // invalid ledger (hash)
            json::Value params;
            params[jss::account] = alice.human();
            params[jss::role] = "user";
            params[jss::ledger_hash] = 1;
            auto const result = env.rpc("json", "noripple_check", to_string(params))[jss::result];
            BEAST_EXPECT(result[jss::error] == "invalidParams");
            BEAST_EXPECT(
                result[jss::error_message] == "Invalid field 'ledger_hash', not hex string.");
        }

        {  // account not found
            json::Value params;
            params[jss::account] = Account{"nobody"}.human();
            params[jss::role] = "user";
            params[jss::ledger] = "current";
            auto const result = env.rpc("json", "noripple_check", to_string(params))[jss::result];
            BEAST_EXPECT(result[jss::error] == "actNotFound");
            BEAST_EXPECT(result[jss::error_message] == "Account not found.");
        }

        {  // passing an account private key will cause
           // parsing as a seed to fail
            json::Value params;
            params[jss::account] = toBase58(TokenType::NodePrivate, alice.sk());
            params[jss::role] = "user";
            params[jss::ledger] = "current";
            auto const result = env.rpc("json", "noripple_check", to_string(params))[jss::result];
            BEAST_EXPECT(result[jss::error] == "actMalformed");
            BEAST_EXPECT(result[jss::error_message] == "Account malformed.");
        }

        {
            // ledger and ledger_hash are included
            json::Value params;
            params[jss::account] = Account{"nobody"}.human();
            params[jss::role] = "user";
            params[jss::ledger] = "current";
            params[jss::ledger_hash] = "ABCDEF";
            auto const result = env.rpc("json", "noripple_check", to_string(params))[jss::result];
            BEAST_EXPECT(result[jss::error] == "invalidParams");
            BEAST_EXPECT(
                result[jss::error_message] ==
                "Exactly one of 'ledger', 'ledger_hash', or 'ledger_index' can "
                "be specified.");
        }

        {
            // invalid ledger
            json::Value params;
            params[jss::account] = Account{"nobody"}.human();
            params[jss::role] = "user";
            params[jss::ledger] = json::ValueType::Object;
            auto const result = env.rpc("json", "noripple_check", to_string(params))[jss::result];
            BEAST_EXPECT(result[jss::error] == "invalidParams");
            BEAST_EXPECT(
                result[jss::error_message] == "Invalid field 'ledger', not string or number.");
        }
    }

    void
    testBasic(bool user, bool problems)
    {
        testcase << "Request noripple_check for " << (user ? "user" : "gateway") << " role, expect"
                 << (problems ? "" : " no") << " problems";

        using namespace test::jtx;
        Env env{*this};

        auto const gw = Account{"gw"};
        auto const alice = Account{"alice"};

        env.fund(XRP(10000), gw, alice);
        if ((user && problems) || (!user && !problems))
        {
            env(fset(alice, asfDefaultRipple));
            env(trust(alice, gw["USD"](100)));
        }
        else
        {
            env(fclear(alice, asfDefaultRipple));
            env(trust(alice, gw["USD"](100), gw, tfSetNoRipple));
        }
        env.close();

        json::Value params;
        params[jss::account] = alice.human();
        params[jss::role] = (user ? "user" : "gateway");
        params[jss::ledger] = "current";
        auto result = env.rpc("json", "noripple_check", to_string(params))[jss::result];

        auto const pa = result["problems"];
        if (!BEAST_EXPECT(pa.isArray()))
            return;

        if (problems)
        {
            if (!BEAST_EXPECT(pa.size() == 2))
                return;

            if (user)
            {
                BEAST_EXPECT(boost::starts_with(pa[0u].asString(), "You appear to have set"));
                BEAST_EXPECT(boost::starts_with(pa[1u].asString(), "You should probably set"));
            }
            else
            {
                BEAST_EXPECT(boost::starts_with(pa[0u].asString(), "You should immediately set"));
                BEAST_EXPECT(boost::starts_with(pa[1u].asString(), "You should clear"));
            }
        }
        else
        {
            BEAST_EXPECT(pa.size() == 0);
        }

        // now make a second request asking for the relevant transactions this
        // time.
        params[jss::transactions] = true;
        result = env.rpc("json", "noripple_check", to_string(params))[jss::result];
        if (!BEAST_EXPECT(result[jss::transactions].isArray()))
            return;

        auto const txs = result[jss::transactions];
        if (problems)
        {
            if (!BEAST_EXPECT(txs.size() == (user ? 1 : 2)))
                return;

            if (!user)
            {
                BEAST_EXPECT(txs[0u][jss::Account] == alice.human());
                BEAST_EXPECT(txs[0u][jss::TransactionType] == jss::AccountSet);
            }

            BEAST_EXPECT(result[jss::transactions][txs.size() - 1][jss::Account] == alice.human());
            BEAST_EXPECT(
                result[jss::transactions][txs.size() - 1][jss::TransactionType] == jss::TrustSet);
            BEAST_EXPECT(
                result[jss::transactions][txs.size() - 1][jss::LimitAmount] ==
                gw["USD"](100).value().getJson(JsonOptions::Values::None));
        }
        else
        {
            BEAST_EXPECT(txs.size() == 0);
        }
    }

public:
    void
    run() override
    {
        testBadInput();
        for (auto user : {true, false})
        {
            for (auto problem : {true, false})
                testBasic(user, problem);
        }
    }
};

class NoRippleCheckLimits_test : public beast::unit_test::Suite
{
    void
    testLimits(bool admin)
    {
        testcase << "Check limits in returned data, " << (admin ? "admin" : "non-admin");

        using namespace test::jtx;

        Env env{*this, admin ? envconfig() : envconfig(noAdmin)};

        auto const alice = Account{"alice"};
        env.fund(XRP(100000), alice);
        env(fset(alice, asfDefaultRipple));
        env.close();

        auto checkBalance = [&env]() {
            // this is endpoint drop prevention. Non admin ports will drop
            // requests if they are coming too fast, so we manipulate the
            // resource manager here to reset the endpoint balance (for
            // localhost) if we get too close to the drop limit. It would
            // be better if we could add this functionality to Env somehow
            // or otherwise disable endpoint charging for certain test
            // cases.
            using namespace xrpl::Resource;
            using namespace std::chrono;
            using namespace beast::IP;
            auto c = env.app().getResourceManager().newInboundEndpoint(
                Endpoint::fromString(test::getEnvLocalhostAddr()));

            // if we go above the warning threshold, reset
            if (c.balance() > kWarningThreshold)
            {
                using ct = beast::AbstractClock<steady_clock>;
                c.entry().local_balance =
                    DecayingSample<kDecayWindowSeconds, ct>{steady_clock::now()};
            }
        };

        for (auto i = 0; i < xrpl::RPC::Tuning::kNoRippleCheck.rmax + 5; ++i)
        {
            if (!admin)
                checkBalance();

            auto& txq = env.app().getTxQ();
            auto const gw = Account{"gw" + std::to_string(i)};
            env.memoize(gw);
            auto const baseFee = env.current()->fees().base;
            env(pay(env.master, gw, XRP(1000)),
                Seq(kAutofill),
                Fee(toDrops(txq.getMetrics(*env.current()).openLedgerFeeLevel, baseFee) + 1),
                Sig(kAutofill));
            env(fset(gw, asfDefaultRipple),
                Seq(kAutofill),
                Fee(toDrops(txq.getMetrics(*env.current()).openLedgerFeeLevel, baseFee) + 1),
                Sig(kAutofill));
            env(trust(alice, gw["USD"](10)),
                Fee(toDrops(txq.getMetrics(*env.current()).openLedgerFeeLevel, baseFee) + 1));
            env.close();
        }

        // default limit value
        json::Value params;
        params[jss::account] = alice.human();
        params[jss::role] = "user";
        params[jss::ledger] = "current";
        auto result = env.rpc("json", "noripple_check", to_string(params))[jss::result];

        BEAST_EXPECT(result["problems"].size() == 301);

        // one below minimum
        params[jss::limit] = 9;
        result = env.rpc("json", "noripple_check", to_string(params))[jss::result];
        BEAST_EXPECT(result["problems"].size() == (admin ? 10 : 11));

        // at minimum
        params[jss::limit] = 10;
        result = env.rpc("json", "noripple_check", to_string(params))[jss::result];
        BEAST_EXPECT(result["problems"].size() == 11);

        // at max
        params[jss::limit] = 400;
        result = env.rpc("json", "noripple_check", to_string(params))[jss::result];
        BEAST_EXPECT(result["problems"].size() == 401);

        // at max+1
        params[jss::limit] = 401;
        result = env.rpc("json", "noripple_check", to_string(params))[jss::result];
        BEAST_EXPECT(result["problems"].size() == (admin ? 402 : 401));
    }

public:
    void
    run() override
    {
        for (auto admin : {true, false})
            testLimits(admin);
    }
};

BEAST_DEFINE_TESTSUITE(NoRippleCheck, rpc, xrpl);

// These tests that deal with limit amounts are slow because of the
// offer/account setup, so making them manual -- the additional coverage
// provided by them is minimal

BEAST_DEFINE_TESTSUITE_MANUAL_PRIO(NoRippleCheckLimits, rpc, xrpl, 1);

}  // namespace xrpl

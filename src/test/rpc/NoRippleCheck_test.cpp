//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2017 Ripple Labs Inc.

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

#include <ripple/app/misc/TxQ.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>
#include <test/jtx/envconfig.h>
#include <boost/algorithm/string/predicate.hpp>
#include <ripple/beast/utility/temp_dir.h>
#include <ripple/resource/ResourceManager.h>
#include <ripple/resource/impl/Entry.h>
#include <ripple/resource/impl/Tuning.h>
#include <ripple/rpc/impl/Tuning.h>

namespace ripple {

class NoRippleCheck_test : public beast::unit_test::suite
{
    void
    testBadInput ()
    {
        testcase ("Bad input to noripple_check");

        using namespace test::jtx;
        Env env {*this};

        auto const alice = Account {"alice"};
        env.fund (XRP(10000), alice);
        env.close ();

        { // missing account field
            auto const result = env.rpc ("json", "noripple_check", "{}")
                [jss::result];
            BEAST_EXPECT (result[jss::error] == "invalidParams");
            BEAST_EXPECT (result[jss::error_message] ==
                "Missing field 'account'.");
        }

        { // missing role field
            Json::Value params;
            params[jss::account] = alice.human();
            auto const result = env.rpc ("json", "noripple_check",
                boost::lexical_cast<std::string>(params)) [jss::result];
            BEAST_EXPECT (result[jss::error] == "invalidParams");
            BEAST_EXPECT (result[jss::error_message] ==
                "Missing field 'role'.");
        }

        { // invalid role field
            Json::Value params;
            params[jss::account] = alice.human();
            params[jss::role] = "not_a_role";
            auto const result = env.rpc ("json", "noripple_check",
                boost::lexical_cast<std::string>(params)) [jss::result];
            BEAST_EXPECT (result[jss::error] == "invalidParams");
            BEAST_EXPECT (result[jss::error_message] ==
                "Invalid field 'role'.");
        }

        { // invalid limit
            Json::Value params;
            params[jss::account] = alice.human();
            params[jss::role] = "user";
            params[jss::limit] = -1;
            auto const result = env.rpc ("json", "noripple_check",
                boost::lexical_cast<std::string>(params)) [jss::result];
            BEAST_EXPECT (result[jss::error] == "invalidParams");
            BEAST_EXPECT (result[jss::error_message] ==
                "Invalid field 'limit', not unsigned integer.");
        }

        { // invalid ledger (hash)
            Json::Value params;
            params[jss::account] = alice.human();
            params[jss::role] = "user";
            params[jss::ledger_hash] = 1;
            auto const result = env.rpc ("json", "noripple_check",
                boost::lexical_cast<std::string>(params)) [jss::result];
            BEAST_EXPECT (result[jss::error] == "invalidParams");
            BEAST_EXPECT (result[jss::error_message] ==
                "ledgerHashNotString");
        }

        { // account not found
            Json::Value params;
            params[jss::account] = Account{"nobody"}.human();
            params[jss::role] = "user";
            params[jss::ledger] = "current";
            auto const result = env.rpc ("json", "noripple_check",
                boost::lexical_cast<std::string>(params)) [jss::result];
            BEAST_EXPECT (result[jss::error] == "actNotFound");
            BEAST_EXPECT (result[jss::error_message] ==
                "Account not found.");
        }

        { // passing an account private key will cause
          // parsing as a seed to fail
            Json::Value params;
            params[jss::account] =
                toBase58 (TokenType::NodePrivate, alice.sk());
            params[jss::role] = "user";
            params[jss::ledger] = "current";
            auto const result = env.rpc ("json", "noripple_check",
                boost::lexical_cast<std::string>(params)) [jss::result];
            BEAST_EXPECT (result[jss::error] == "badSeed");
            BEAST_EXPECT (result[jss::error_message] ==
                "Disallowed seed.");
        }
    }

    void
    testBasic (bool user, bool problems)
    {
        testcase << "Request noripple_check for " <<
            (user ? "user" : "gateway") << " role, expect" <<
            (problems ? "" : " no") << " problems";

        using namespace test::jtx;
        Env env {*this};

        auto const gw = Account {"gw"};
        auto const alice = Account {"alice"};

        env.fund (XRP(10000), gw, alice);
        if ((user && problems) || (!user && !problems))
        {
            env (fset (alice, asfDefaultRipple));
            env (trust (alice, gw["USD"](100)));
        }
        else
        {
            env (fclear (alice, asfDefaultRipple));
            env (trust (alice, gw["USD"](100), gw, tfSetNoRipple));
        }
        env.close ();

        Json::Value params;
        params[jss::account] = alice.human();
        params[jss::role] = (user ? "user" : "gateway");
        params[jss::ledger] = "current";
        auto result = env.rpc ("json", "noripple_check",
            boost::lexical_cast<std::string>(params)) [jss::result];

        auto const pa = result["problems"];
        if (! BEAST_EXPECT (pa.isArray ()))
            return;

        if (problems)
        {
            if (! BEAST_EXPECT (pa.size() == 2))
                return;

            if (user)
            {
                BEAST_EXPECT (
                    boost::starts_with(pa[0u].asString(),
                        "You appear to have set"));
                BEAST_EXPECT (
                    boost::starts_with(pa[1u].asString(),
                        "You should probably set"));
            }
            else
            {
                BEAST_EXPECT (
                    boost::starts_with(pa[0u].asString(),
                        "You should immediately set"));
                BEAST_EXPECT(
                    boost::starts_with(pa[1u].asString(),
                        "You should clear"));
            }
        }
        else
        {
            BEAST_EXPECT (pa.size() == 0);
        }

        // now make a second request asking for the relevant transactions this
        // time.
        params[jss::transactions] = true;
        result = env.rpc ("json", "noripple_check",
            boost::lexical_cast<std::string>(params)) [jss::result];
        if (! BEAST_EXPECT (result[jss::transactions].isArray ()))
            return;

        auto const txs = result[jss::transactions];
        if (problems)
        {
            if (! BEAST_EXPECT (txs.size () == (user ? 1 : 2)))
                return;

            if (! user)
            {
                BEAST_EXPECT (txs[0u][jss::Account] == alice.human());
                BEAST_EXPECT (txs[0u][jss::TransactionType] == jss::AccountSet);
            }

            BEAST_EXPECT (
                result[jss::transactions][txs.size()-1][jss::Account] ==
                alice.human());
            BEAST_EXPECT (
                result[jss::transactions][txs.size()-1][jss::TransactionType] ==
                jss::TrustSet);
            BEAST_EXPECT (
                result[jss::transactions][txs.size()-1][jss::LimitAmount] ==
                gw["USD"](100).value ().getJson (JsonOptions::none));
        }
        else
        {
            BEAST_EXPECT (txs.size () == 0);
        }
    }

public:
    void run () override
    {
        testBadInput ();
        for (auto user : {true, false})
            for (auto problem : {true, false})
                testBasic (user, problem);
    }
};

class NoRippleCheckLimits_test : public beast::unit_test::suite
{
    void
    testLimits(bool admin)
    {
        testcase << "Check limits in returned data, " <<
            (admin ? "admin" : "non-admin");

        using namespace test::jtx;

        Env env {*this, admin ? envconfig () : envconfig(no_admin)};

        auto const alice = Account {"alice"};
        env.fund (XRP (100000), alice);
        env (fset (alice, asfDefaultRipple));
        env.close ();

        auto checkBalance = [&env]()
        {
            // this is endpoint drop prevention. Non admin ports will drop
            // requests if they are coming too fast, so we manipulate the
            // resource manager here to reset the enpoint balance (for
            // localhost) if we get too close to the drop limit. It would
            // be better if we could add this functionality to Env somehow
            // or otherwise disable endpoint charging for certain test
            // cases.
            using namespace ripple::Resource;
            using namespace std::chrono;
            using namespace beast::IP;
            auto c = env.app ().getResourceManager ()
                .newInboundEndpoint (
                    Endpoint::from_string (test::getEnvLocalhostAddr()));

            // if we go above the warning threshold, reset
            if (c.balance() > warningThreshold)
            {
                using clock_type = beast::abstract_clock <steady_clock>;
                c.entry().local_balance =
                    DecayingSample <decayWindowSeconds, clock_type>
                        {steady_clock::now()};
            }
        };

        for (auto i = 0; i < ripple::RPC::Tuning::noRippleCheck.rmax + 5; ++i)
        {
            if (! admin)
                checkBalance();

            auto& txq = env.app().getTxQ();
            auto const gw = Account {"gw" + std::to_string(i)};
            env.memoize(gw);
            env (pay (env.master, gw, XRP(1000)),
                seq (autofill),
                fee (txq.getMetrics(*env.current()).openLedgerFeeLevel + 1),
                sig (autofill));
            env (fset (gw, asfDefaultRipple),
                seq (autofill),
                fee (txq.getMetrics(*env.current()).openLedgerFeeLevel + 1),
                sig (autofill));
            env (trust (alice, gw["USD"](10)),
                fee (txq.getMetrics(*env.current()).openLedgerFeeLevel + 1));
            env.close();
        }

        // default limit value
        Json::Value params;
        params[jss::account] = alice.human();
        params[jss::role] = "user";
        params[jss::ledger] = "current";
        auto result = env.rpc ("json", "noripple_check",
            boost::lexical_cast<std::string>(params)) [jss::result];

        BEAST_EXPECT (result["problems"].size() == 301);

        // one below minimum
        params[jss::limit] = 9;
        result = env.rpc ("json", "noripple_check",
            boost::lexical_cast<std::string>(params)) [jss::result];
        BEAST_EXPECT (result["problems"].size() == (admin ? 10 : 11));

        // at minimum
        params[jss::limit] = 10;
        result = env.rpc ("json", "noripple_check",
            boost::lexical_cast<std::string>(params)) [jss::result];
        BEAST_EXPECT (result["problems"].size() == 11);

        // at max
        params[jss::limit] = 400;
        result = env.rpc ("json", "noripple_check",
            boost::lexical_cast<std::string>(params)) [jss::result];
        BEAST_EXPECT (result["problems"].size() == 401);

        // at max+1
        params[jss::limit] = 401;
        result = env.rpc ("json", "noripple_check",
            boost::lexical_cast<std::string>(params)) [jss::result];
        BEAST_EXPECT (result["problems"].size() == (admin ? 402 : 401));
    }

public:
    void run () override
    {
        for (auto admin : {true, false})
            testLimits (admin);
    }
};

BEAST_DEFINE_TESTSUITE(NoRippleCheck, app, ripple);

// These tests that deal with limit amounts are slow because of the
// offer/account setup, so making them manual -- the additional coverage provided
// by them is minimal

BEAST_DEFINE_TESTSUITE_MANUAL_PRIO(NoRippleCheckLimits, app, ripple, 1);

} // ripple


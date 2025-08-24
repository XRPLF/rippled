//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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

namespace ripple {
namespace test {

struct Simple_test : public beast::unit_test::suite
{

    void
    testSimple(FeatureBitset features)
    {
        testcase("Simple");
        using namespace test::jtx;
        using namespace std::literals;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        // env.fund(XRP(100'000), alice, bob);
        env(pay(env.master, alice, XRP(1000)));
        env.close();

        // create open ledger with 1000 transactions
        // for (int i = 0; i < 2500; ++i)
        //     env(pay(alice, bob, XRP(1)), fee(XRP(1)));
        // env.close();

        // {
        //     Json::Value params;
        //     params[jss::ledger_index] = env.current()->seq() - 1;
        //     params[jss::transactions] = true;
        //     params[jss::expand] = true;
        //     auto const jrr = env.rpc("json", "ledger", to_string(params));
        //     std::cout << jrr << std::endl;
        // }
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{testable_amendments()};
        testSimple(all);
    }
};

BEAST_DEFINE_TESTSUITE(Simple, app, ripple);

}  // namespace test
}  // namespace ripple

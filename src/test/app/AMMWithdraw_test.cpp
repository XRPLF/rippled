//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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
#include <ripple/app/misc/AMMHelpers.h>
#include <ripple/app/paths/AMMContext.h>
#include <ripple/app/paths/AMMOffer.h>
#include <ripple/protocol/AMMCore.h>
#include <ripple/protocol/STParsedJSON.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/RPCHandler.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <test/jtx.h>
#include <test/jtx/AMM.h>
#include <test/jtx/AMMTest.h>
#include <test/jtx/amount.h>
#include <test/jtx/sendmax.h>

#include <chrono>
#include <utility>
#include <vector>

namespace ripple {
namespace test {

struct AMMWithdraw_test : public jtx::AMMTest
{
    void
    testMalformed()
    {
        using namespace jtx;

        // {
        //     Env env{*this};
        //     env.fund(XRP(30'000), alice);
        //     Json::Value jv;
        //     jv[jss::Account] = alice.human();
        //     jv[jss::TransactionType] = jss::AMMWithdraw;
        //     env(jv, fee(drops(-10)), ter(temBAD_FEE));
        // }

        testAMM([&](AMM& ammAlice, Env& env) {
            WithdrawArg args{
                .flags = tfSingleAsset,
                .err = ter(temMALFORMED),
            };
            ammAlice.withdraw(args);
        });

        testAMM([&](AMM& ammAlice, Env& env) {
            WithdrawArg args{
                .flags = tfOneAssetLPToken,
                .err = ter(temMALFORMED),
            };
            ammAlice.withdraw(args);
        });

        testAMM([&](AMM& ammAlice, Env& env) {
            WithdrawArg args{
                .flags = tfLimitLPToken,
                .err = ter(temMALFORMED),
            };
            ammAlice.withdraw(args);
        });

        testAMM([&](AMM& ammAlice, Env& env) {
            WithdrawArg args{
                .asset1Out = XRP(100),
                .asset2Out = XRP(100),
                .err = ter(temBAD_AMM_TOKENS),
            };
            ammAlice.withdraw(args);
        });

        testAMM([&](AMM& ammAlice, Env& env) {
            WithdrawArg args{
                .asset1Out = XRP(100),
                .asset2Out = BAD(100),
                .err = ter(temBAD_CURRENCY),
            };
            ammAlice.withdraw(args);
        });

        testAMM([&](AMM& ammAlice, Env& env) {
            Json::Value jv;
            jv[jss::TransactionType] = jss::AMMWithdraw;
            jv[jss::Flags] = tfLimitLPToken;
            jv[jss::Account] = alice.human();
            ammAlice.setTokens(jv);
            XRP(100).value().setJson(jv[jss::Amount]);
            USD(100).value().setJson(jv[jss::EPrice]);
            env(jv, ter(temBAD_AMM_TOKENS));
        });
    }

    void
    testOther()
    {
        using namespace jtx;

        testAMM(
            [&](AMM& ammAlice, Env& env) {
                WithdrawArg args{
                    .asset1Out = XRP(100),
                    .err = ter(tecAMM_BALANCE),
                };
                ammAlice.withdraw(args);
            },
            {{XRP(99), USD(99)}});

        testAMM(
            [&](AMM& ammAlice, Env& env) {
                WithdrawArg args{
                    .asset1Out = USD(100),
                    .err = ter(tecAMM_BALANCE),
                };
                ammAlice.withdraw(args);
            },
            {{XRP(99), USD(99)}});

        {
            Env env{*this};
            env.fund(XRP(30'000), gw, alice, bob);
            env.close();
            env(fset(gw, asfRequireAuth));
            env(trust(alice, gw["USD"](30'000), 0));
            env(trust(gw, alice["USD"](0), tfSetfAuth));
            // Bob trusts Gateway to owe him USD...
            env(trust(bob, gw["USD"](30'000), 0));
            // ...but Gateway does not authorize Bob to hold its USD.
            env.close();
            env(pay(gw, alice, USD(10'000)));
            env.close();
            AMM ammAlice(env, alice, XRP(10'000), USD(10'000));
            WithdrawArg args{
                .account = bob,
                .asset1Out = USD(100),
                .err = ter(tecNO_AUTH),
            };
            ammAlice.withdraw(args);
        }
    }

    void
    run() override
    {
        testMalformed();
        testOther();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(AMMWithdraw, app, ripple, 1);

}  // namespace test
}  // namespace ripple

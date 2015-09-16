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
#include <ripple/test/jtx.h>
#include <ripple/app/tx/apply.h>

namespace ripple {
namespace test {

struct Regression_test : public beast::unit_test::suite
{
    // OfferCreate, then OfferCreate with cancel
    void testOffer1()
    {
        using namespace jtx;
        Env env(*this);
        auto const gw = Account("gw");
        auto const USD = gw["USD"];
        env.fund(XRP(10000), "alice", gw);
        env(offer("alice", USD(10), XRP(10)), require(owners("alice", 1)));
        env(offer("alice", USD(20), XRP(10)), json(R"raw(
                { "OfferSequence" : 2 }
            )raw"), require(owners("alice", 1)));
    }

    void testLowBalanceDestroy()
    {
        testcase("Account balance < fee destroys correct amount of XRP");
        using namespace jtx;
        Env env(*this);
        env.memoize("alice");

        // The low balance scenario can not deterministically
        // be reproduced against an open ledger. Make a local
        // closed ledger and work with it directly.
        auto closed = std::make_shared<Ledger>(
            create_genesis, env.config, env.app().family());
        auto expectedDrops = SYSTEM_CURRENCY_START;
        expect(closed->info().drops == expectedDrops);

        auto const aliceXRP = 400;
        auto const aliceAmount = XRP(aliceXRP);

        auto next = std::make_shared<Ledger>(
            open_ledger, *closed);
        next->setClosed();
        {
            // Fund alice
            auto const jt = env.jt(pay(env.master, "alice", aliceAmount));
            OpenView accum(&*next);

            auto const result = ripple::apply(env.app(),
                accum, *jt.stx, tapENABLE_TESTING,
                    directSigVerify, env.config,
                        env.journal);
            expect(result.first == tesSUCCESS);
            expect(result.second);

            accum.apply(*next);
        }
        expectedDrops -= next->fees().base;
        expect(next->info().drops == expectedDrops);
        {
            auto const sle = next->read(
                keylet::account(Account("alice").id()));
            expect(sle, "sle");
            auto balance = sle->getFieldAmount(sfBalance);

            expect(balance == aliceAmount );
        }

        {
            // Specify the seq manually since the env's open ledger
            // doesn't know about this account.
            auto const jt = env.jt(noop("alice"), fee(expectedDrops),
                seq(1));
                
            OpenView accum(&*next);

            auto const result = ripple::apply(env.app(),
                accum, *jt.stx, tapENABLE_TESTING,
                    directSigVerify, env.config,
                        env.journal);
            expect(result.first == tecINSUFF_FEE);
            expect(result.second);

            accum.apply(*next);
        }
        {
            auto const sle = next->read(
                keylet::account(Account("alice").id()));
            expect(sle, "sle");
            auto balance = sle->getFieldAmount(sfBalance);

            expect(balance == XRP(0));
        }
        expectedDrops -= aliceXRP * dropsPerXRP<int>::value;
        expect(next->info().drops == expectedDrops,
            "next->info().drops == expectedDrops");
    }

    void run() override
    {
        testOffer1();
        testLowBalanceDestroy();
    }
};

BEAST_DEFINE_TESTSUITE(Regression,app,ripple);

} // test
} // ripple

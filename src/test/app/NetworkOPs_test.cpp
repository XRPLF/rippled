//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Dev Null Productions

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
#include <test/jtx/CaptureLogs.h>
#include <test/jtx/Env.h>

#include <xrpld/app/misc/HashRouter.h>

namespace ripple {
namespace test {

class NetworkOPs_test : public beast::unit_test::suite
{
public:
    void
    run() override
    {
        testAllBadHeldTransactions();
    }

    void
    testAllBadHeldTransactions()
    {
        // All trasactions are already marked as SF_BAD, and we should be able
        // to handle the case properly without an assertion failure
        testcase("No valid transactions in batch");

        std::string logs;

        {
            using namespace jtx;
            auto const alice = Account{"alice"};
            Env env{
                *this,
                envconfig(),
                std::make_unique<CaptureLogs>(&logs),
                beast::severities::kAll};
            env.memoize(env.master);
            env.memoize(alice);

            auto const jtx = env.jt(ticket::create(alice, 1), seq(1), fee(10));

            auto transacionId = jtx.stx->getTransactionID();
            env.app().getHashRouter().setFlags(
                transacionId, HashRouterFlags::HELD);

            env(jtx, json(jss::Sequence, 1), ter(terNO_ACCOUNT));

            env.app().getHashRouter().setFlags(
                transacionId, HashRouterFlags::BAD);

            env.close();
        }

        BEAST_EXPECT(
            logs.find("No transaction to process!") != std::string::npos);
    }
};

BEAST_DEFINE_TESTSUITE(NetworkOPs, app, ripple);

}  // namespace test
}  // namespace ripple

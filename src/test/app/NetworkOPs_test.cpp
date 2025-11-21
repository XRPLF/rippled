// Copyright (c) 2020 Dev Null Productions

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
        // All transactions are already marked as SF_BAD, and we should be able
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

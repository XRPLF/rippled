
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/last_ledger_sequence.h>
#include <test/jtx/noop.h>
#include <test/jtx/seq.h>
#include <test/jtx/ter.h>

#include <xrpld/core/Config.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/protocol/TER.h>

#include <memory>
#include <vector>

namespace xrpl::test {

struct Transaction_ordering_test : public beast::unit_test::Suite
{
    void
    testCorrectOrder()
    {
        using namespace jtx;
        testcase("Correct Order");

        Env env(*this);
        auto const alice = Account("alice");
        env.fund(XRP(1000), noripple(alice));

        auto const aliceSequence = env.seq(alice);

        auto const tx1 = env.jt(noop(alice), Seq(aliceSequence));
        auto const tx2 = env.jt(noop(alice), Seq(aliceSequence + 1), LastLedgerSeq(7));

        env(tx1);
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSequence + 1);
        env(tx2);
        env.close();
        BEAST_EXPECT(env.seq(alice) == aliceSequence + 2);

        env.close();

        {
            auto const result = env.rpc("tx", to_string(tx1.stx->getTransactionID()));
            BEAST_EXPECT(result["result"]["meta"]["TransactionResult"] == "tesSUCCESS");
        }
        {
            auto const result = env.rpc("tx", to_string(tx2.stx->getTransactionID()));
            BEAST_EXPECT(result["result"]["meta"]["TransactionResult"] == "tesSUCCESS");
        }
    }

    void
    testIncorrectOrder()
    {
        using namespace jtx;

        testcase("Incorrect order");

        Env env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg->forceMultiThread = false;
            return cfg;
        }));

        auto const alice = Account("alice");
        env.fund(XRP(1000), noripple(alice));

        auto const aliceSequence = env.seq(alice);

        auto const tx1 = env.jt(noop(alice), Seq(aliceSequence));
        auto const tx2 = env.jt(noop(alice), Seq(aliceSequence + 1), LastLedgerSeq(7));

        env(tx2, Ter(terPRE_SEQ));
        BEAST_EXPECT(env.seq(alice) == aliceSequence);
        env(tx1);
        env.app().getJobQueue().rendezvous();
        BEAST_EXPECT(env.seq(alice) == aliceSequence + 2);

        env.close();

        {
            auto const result = env.rpc("tx", to_string(tx1.stx->getTransactionID()));
            BEAST_EXPECT(result["result"]["meta"]["TransactionResult"] == "tesSUCCESS");
        }
        {
            auto const result = env.rpc("tx", to_string(tx2.stx->getTransactionID()));
            BEAST_EXPECT(result["result"]["meta"]["TransactionResult"] == "tesSUCCESS");
        }
    }

    void
    testIncorrectOrderMultipleIntermediaries()
    {
        using namespace jtx;

        testcase("Incorrect order multiple intermediaries");

        Env env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg->forceMultiThread = true;
            return cfg;
        }));

        auto const alice = Account("alice");
        env.fund(XRP(1000), noripple(alice));

        auto const aliceSequence = env.seq(alice);
        static constexpr auto kSize = 5;

        std::vector<JTx> tx;
        tx.reserve(kSize);
        for (auto i = 0; i < kSize; ++i)
        {
            tx.emplace_back(env.jt(noop(alice), Seq(aliceSequence + i), LastLedgerSeq(7)));
        }

        for (auto i = 1; i < kSize; ++i)
        {
            env(tx[i], Ter(terPRE_SEQ));
            BEAST_EXPECT(env.seq(alice) == aliceSequence);
        }

        env(tx[0]);
        env.app().getJobQueue().rendezvous();
        BEAST_EXPECT(env.seq(alice) == aliceSequence + kSize);

        env.close();

        for (auto i = 0; i < kSize; ++i)
        {
            auto const result = env.rpc("tx", to_string(tx[i].stx->getTransactionID()));
            BEAST_EXPECT(result["result"]["meta"]["TransactionResult"] == "tesSUCCESS");
        }
    }

    void
    run() override
    {
        testCorrectOrder();
        testIncorrectOrder();
        testIncorrectOrderMultipleIntermediaries();
    }
};

BEAST_DEFINE_TESTSUITE(Transaction_ordering, app, xrpl);

}  // namespace xrpl::test

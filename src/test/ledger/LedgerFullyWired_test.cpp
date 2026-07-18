/**
 * @file
 * @brief Tests for Ledger fullyWired functionality.
 */

#include <test/jtx/Env.h>
#include <test/jtx/envconfig.h>
#include <test/unit_test/SuiteJournal.h>

#include <xrpl/beast/unit_test.h>
#include <xrpl/ledger/Ledger.h>

#include <atomic>
#include <thread>
#include <vector>

namespace xrpl::test {

/**
 * Test Ledger::isFullyWired(), setFullyWired(), and fullWireForUse().
 */
class LedgerFullyWired_test : public beast::unit_test::Suite
{
public:
    void
    run() override
    {
        testDefaultState();
        testSetAndGetFullyWired();
        testFullWireForUseSuccess();
        testThreadSafeAccess();
    }

    void
    testDefaultState()
    {
        testcase("default state");

        using namespace jtx;
        Env env{*this};

        auto ledger = std::dynamic_pointer_cast<Ledger const>(env.closed());

        // The genesis ledger created by jtx::Env is fully wired by default
        // (see Ledger.cpp CreateGenesisT constructor calls setFullyWired())
        BEAST_EXPECT(ledger->isFullyWired());
    }

    void
    testSetAndGetFullyWired()
    {
        testcase("set and get fully wired");

        using namespace jtx;
        Env env{*this};

        auto ledger = std::dynamic_pointer_cast<Ledger const>(env.closed());

        // Genesis ledger starts as fully wired
        BEAST_EXPECT(ledger->isFullyWired());

        // Calling setFullyWired again should be idempotent (no crash, still wired)
        ledger->setFullyWired();
        BEAST_EXPECT(ledger->isFullyWired());
        ledger->setFullyWired();
        BEAST_EXPECT(ledger->isFullyWired());
    }

    void
    testFullWireForUseSuccess()
    {
        testcase("fullWireForUse success");

        using namespace jtx;
        Env env{*this};

        auto ledger = std::dynamic_pointer_cast<Ledger const>(env.closed());

        test::SuiteJournal journal{"LedgerFullyWired", *this};

        // fullWireForUse on a wired ledger should return true
        // (without null backend, it always returns true)
        bool result = ledger->fullWireForUse(journal, "test context");
        BEAST_EXPECT(result);
    }

    void
    testThreadSafeAccess()
    {
        testcase("thread-safe access");

        using namespace jtx;
        Env env{*this};

        auto ledger = std::dynamic_pointer_cast<Ledger const>(env.closed());

        // Genesis ledger is already fully wired, so concurrent readers
        // should all observe the wired state consistently
        std::atomic<int> wiredReads{0};
        std::atomic<int> unwiredReads{0};

        std::vector<std::thread> readers;
        for (int i = 0; i < 10; ++i)
        {
            readers.emplace_back([&]() {
                for (int j = 0; j < 20; ++j)
                {
                    if (ledger->isFullyWired())
                        ++wiredReads;
                    else
                        ++unwiredReads;
                }
            });
        }

        for (auto& t : readers)
            t.join();

        // All reads should see the wired state
        BEAST_EXPECT(wiredReads.load() == 200);
        BEAST_EXPECT(unwiredReads.load() == 0);
    }
};

BEAST_DEFINE_TESTSUITE(LedgerFullyWired, ledger, xrpl);

}  // namespace xrpl::test

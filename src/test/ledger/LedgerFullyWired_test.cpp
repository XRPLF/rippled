/**
 * @file
 * @brief Tests for Ledger fullyWired functionality.
 */

#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/pay.h>
#include <test/unit_test/SuiteJournal.h>

#include <xrpld/app/misc/SHAMapStore.h>

#include <xrpl/beast/unit_test.h>
#include <xrpl/config/Constants.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/protocol/jss.h>

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
        testNullBackendEnv();
        testNullBackendIsPerApplication();
    }

    void
    testDefaultState()
    {
        testcase("default state");

        using namespace jtx;
        Env env{*this};

        auto ledger = std::dynamic_pointer_cast<Ledger const>(env.closed());

        // Env's closed ledger is the genesis child accepted via switchLCL,
        // which marks the ledger fully wired after construction.
        BEAST_EXPECT(ledger->isFullyWired());
    }

    void
    testSetAndGetFullyWired()
    {
        testcase("set and get fully wired");

        using namespace jtx;
        Env env{*this};

        auto ledger = std::dynamic_pointer_cast<Ledger const>(env.closed());

        // Closed ledger starts as fully wired after switchLCL
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

        // Closed ledger is already fully wired, so concurrent readers
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

    void
    testNullBackendEnv()
    {
        testcase("null-backend env can close ledgers");

        using namespace jtx;
        Env env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg->section(Sections::kNodeDatabase).set("type", "rwdb");
            cfg->section(Sections::kRelationalDb).set("backend", "rwdb");
            if (cfg->ledgerHistory == 0)
                cfg->ledgerHistory = 256;
            return cfg;
        }));

        BEAST_EXPECT(env.app().getSHAMapStore().isNullBackend());
        BEAST_EXPECT(env.app().getNodeFamily().isNullBackend());

        auto const alice = Account("alice");
        env.fund(XRP(1000), alice);
        env.close();
        env(pay(env.master, alice, XRP(1)));
        env.close();

        auto ledger = std::dynamic_pointer_cast<Ledger const>(env.closed());
        BEAST_EXPECT(ledger);
        if (ledger)
        {
            BEAST_EXPECT(ledger->header().seq >= 3);
            // switchLCL marks accepted ledgers wired; do not fall back
            // to a full state-tree walk.
            BEAST_EXPECT(ledger->isFullyWired());
        }

        auto const info = env.rpc("account_info", alice.human());
        BEAST_EXPECT(info[jss::result][jss::status] == jss::success);
    }

    void
    testNullBackendIsPerApplication()
    {
        testcase("null-backend mode is per Application");

        using namespace jtx;
        Env disk(*this);
        Env rwdb(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg->section(Sections::kNodeDatabase).set("type", "rwdb");
            cfg->section(Sections::kRelationalDb).set("backend", "rwdb");
            if (cfg->ledgerHistory == 0)
                cfg->ledgerHistory = 256;
            return cfg;
        }));

        BEAST_EXPECT(!disk.app().getSHAMapStore().isNullBackend());
        BEAST_EXPECT(!disk.app().getNodeFamily().isNullBackend());
        BEAST_EXPECT(rwdb.app().getSHAMapStore().isNullBackend());
        BEAST_EXPECT(rwdb.app().getNodeFamily().isNullBackend());

        test::SuiteJournal journal{"LedgerFullyWired", *this};

        // A header-only disk ledger must stay usable while an RWDB Env
        // is alive in the same process.
        Ledger diskHeaderOnly(
            disk.closed()->header(), disk.closed()->rules(), disk.app().getNodeFamily());
        BEAST_EXPECT(diskHeaderOnly.fullWireForUse(journal, "disk header-only"));

        Ledger rwdbHeaderOnly(
            rwdb.closed()->header(), rwdb.closed()->rules(), rwdb.app().getNodeFamily());
        BEAST_EXPECT(!rwdbHeaderOnly.isFullyWired());
        BEAST_EXPECT(!rwdbHeaderOnly.fullWireForUse(journal, "rwdb header-only"));
    }
};

BEAST_DEFINE_TESTSUITE(LedgerFullyWired, ledger, xrpl);

}  // namespace xrpl::test

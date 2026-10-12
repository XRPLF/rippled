/**
 * @file
 * @brief Tests for resident ledger lookup used when serving peers.
 */

#include <test/jtx/Env.h>
#include <test/jtx/envconfig.h>

#include <xrpld/app/ledger/InboundLedgers.h>
#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/core/Config.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/config/Constants.h>
#include <xrpl/ledger/Ledger.h>

#include <memory>

namespace xrpl::test {

/**
 * Peer object serving looks up ledgers that are already in memory.
 * The lookup must not treat a peer-supplied hash as a load request.
 */
class InboundLedger_test : public beast::unit_test::Suite
{
public:
    void
    run() override
    {
        testFetchRateStartsAtZero();
        testResidentLedgerLookup();
    }

    void
    testFetchRateStartsAtZero()
    {
        testcase("history fetch rate starts at zero");

        using namespace jtx;
        Env env{*this};

        auto& inboundLedgers = env.app().getInboundLedgers();
        BEAST_EXPECT(inboundLedgers.fetchRate() == 0);
        BEAST_EXPECT(inboundLedgers.cacheSize() == 0);
    }

    void
    testResidentLedgerLookup()
    {
        testcase("resident ledger lookup");

        using namespace jtx;
        Env env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg->section(Sections::kNodeDatabase).set("type", "rwdb");
            cfg->section(Sections::kRelationalDb).set("backend", "rwdb");
            if (cfg->ledgerHistory == 0)
                cfg->ledgerHistory = 256;
            return cfg;
        }));

        auto const ledger = std::dynamic_pointer_cast<Ledger const>(env.closed());
        BEAST_EXPECT(ledger);
        if (!ledger)
            return;

        auto& lm = env.app().getLedgerMaster();
        auto const byHash = lm.getResidentLedgerByHash(ledger->header().hash);
        auto const bySeq = lm.getResidentLedgerBySeq(ledger->header().seq);
        BEAST_EXPECT(byHash && byHash->header().hash == ledger->header().hash);
        BEAST_EXPECT(bySeq && bySeq->header().seq == ledger->header().seq);
        BEAST_EXPECT(!lm.getResidentLedgerByHash(UInt256{std::uint64_t{12345}}));
        BEAST_EXPECT(!lm.getResidentLedgerBySeq(ledger->header().seq + 1000));

        // The null node store must not satisfy this from its object cache.
        BEAST_EXPECT(
            !env.app().getNodeStore().fetchNodeObject(ledger->header().hash, ledger->header().seq));
    }
};

BEAST_DEFINE_TESTSUITE(InboundLedger, app, xrpl);

}  // namespace xrpl::test

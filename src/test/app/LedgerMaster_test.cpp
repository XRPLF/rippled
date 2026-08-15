#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/noop.h>

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/misc/SHAMapStore.h>
#include <xrpld/core/Config.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/config/Constants.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::test {

class LedgerMaster_test : public beast::unit_test::Suite
{
    static std::unique_ptr<Config>
    makeNetworkConfig(uint32_t networkID)
    {
        using namespace jtx;
        return envconfig([&](std::unique_ptr<Config> cfg) {
            cfg->networkId = networkID;
            // This test relies on ledger hash so must lock it to fee 10.
            cfg->fees.referenceFee = 10;
            return cfg;
        });
    }

    void
    testTxnIdFromIndex(FeatureBitset features)
    {
        testcase("tx_id_from_index");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, makeNetworkConfig(11111)};

        auto const alice = Account("alice");
        env.fund(XRP(1000), alice);
        env.close();

        // build ledgers
        std::vector<std::shared_ptr<STTx const>> txns;
        std::vector<std::shared_ptr<STObject const>> metas;
        auto const startLegSeq = env.current()->header().seq;
        for (int i = 0; i < 2; ++i)
        {
            env(noop(alice));
            txns.emplace_back(env.tx());
            env.close();
            metas.emplace_back(env.closed()->txRead(env.tx()->getTransactionID()).second);
        }
        // add last (empty) ledger
        env.close();
        auto const endLegSeq = env.closed()->header().seq;

        // test invalid range
        {
            std::uint32_t const ledgerSeq = -1;
            std::uint32_t const txnIndex = 0;
            auto result = env.app().getLedgerMaster().txnIdFromIndex(ledgerSeq, txnIndex);
            BEAST_EXPECT(!result);
        }
        // test not in ledger
        {
            uint32_t const txnIndex = metas[0]->getFieldU32(sfTransactionIndex);
            auto result = env.app().getLedgerMaster().txnIdFromIndex(0, txnIndex);
            BEAST_EXPECT(!result);
        }
        // test empty ledger
        {
            auto result = env.app().getLedgerMaster().txnIdFromIndex(endLegSeq, 0);
            BEAST_EXPECT(!result);
        }
        // ended without result
        {
            uint32_t const txnIndex = metas[0]->getFieldU32(sfTransactionIndex);
            auto result = env.app().getLedgerMaster().txnIdFromIndex(endLegSeq + 1, txnIndex);
            BEAST_EXPECT(!result);
        }
        // success (first tx)
        {
            uint32_t const txnIndex = metas[0]->getFieldU32(sfTransactionIndex);
            auto result = env.app().getLedgerMaster().txnIdFromIndex(startLegSeq, txnIndex);
            BEAST_EXPECT(
                // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
                *result ==
                uint256(
                    "277F4FD89C20B92457FEF05FF63F6405563AD0563C73D967A29727"
                    "72679ADC65"));
        }
        // success (second tx)
        {
            uint32_t const txnIndex = metas[1]->getFieldU32(sfTransactionIndex);
            auto result = env.app().getLedgerMaster().txnIdFromIndex(startLegSeq + 1, txnIndex);
            BEAST_EXPECT(
                // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
                *result ==
                uint256(
                    "293DF7335EBBAF4420D52E70ABF470EB4C5792CAEA2F91F76193C2"
                    "819F538FDE"));
        }
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{testableAmendments()};
        testWithFeats(all);
    }

    static std::unique_ptr<Config>
    enableRWDB(std::unique_ptr<Config> cfg)
    {
        cfg->section(Sections::kNodeDatabase).set("type", "rwdb");
        cfg->section(Sections::kRelationalDb).set("backend", "rwdb");
        return cfg;
    }

    void
    testCloseTimeDoesNotLoadLedger()
    {
        testcase("close time uses resident or header data only");

        using namespace test::jtx;
        Env env{*this, envconfig([](std::unique_ptr<Config> cfg) {
                    cfg->fees.referenceFee = 10;
                    return cfg;
                })};

        Account const alice{"alice"};
        env.fund(XRP(1000), alice);
        env.close();
        env(noop(alice));
        env.close();

        auto& lm = env.app().getLedgerMaster();
        auto const closed = env.closed();
        auto const seq = closed->header().seq;
        auto const hash = closed->header().hash;
        auto const expected = closed->header().closeTime;

        auto const bySeq = lm.getCloseTimeBySeq(seq);
        BEAST_EXPECT(bySeq && *bySeq == expected);

        auto const byHash = lm.getCloseTimeByHash(hash, seq);
        BEAST_EXPECT(byHash && *byHash == expected);

        // Unknown hash must not start a load/acquire; it returns nothing.
        BEAST_EXPECT(!lm.getCloseTimeByHash(uint256(1), seq));
    }

    void
    testRWDBCloseTimeFromRelationalHeader()
    {
        testcase("RWDB close time from relational header");

        using namespace test::jtx;
        Env env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg = enableRWDB(std::move(cfg));
            cfg->fees.referenceFee = 10;
            if (cfg->ledgerHistory == 0)
                cfg->ledgerHistory = 256;
            return cfg;
        }));

        Account const alice{"alice"};
        env.fund(XRP(1000), alice);
        env.close();
        auto const older = env.closed();
        auto const olderSeq = older->header().seq;
        auto const olderHash = older->header().hash;
        auto const olderClose = older->header().closeTime;

        env(noop(alice));
        env.close();
        env(noop(alice));
        env.close();

        auto& lm = env.app().getLedgerMaster();
        // Older than closedLedger_: node store is null, so this must come
        // from the relational header rather than a full ledger load.
        auto const bySeq = lm.getCloseTimeBySeq(olderSeq);
        BEAST_EXPECT(bySeq && *bySeq == olderClose);
        auto const byHash = lm.getCloseTimeByHash(olderHash, olderSeq);
        BEAST_EXPECT(byHash && *byHash == olderClose);
    }

    void
    testRWDBRetainWindowFollowsHistory()
    {
        testcase("RWDB retain window follows ledger_history");

        using namespace test::jtx;
        constexpr std::uint32_t kHistory = 8;
        Env env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg = enableRWDB(std::move(cfg));
            cfg->fees.referenceFee = 10;
            cfg->ledgerHistory = kHistory;
            return cfg;
        }));

        BEAST_EXPECT(env.app().getSHAMapStore().isNullBackend());
        BEAST_EXPECT(env.app().getSHAMapStore().getDeleteInterval() == kHistory);

        Account const alice{"alice"};
        env.fund(XRP(1000), alice);
        env.close();

        auto& lm = env.app().getLedgerMaster();
        // env.close() goes through standalone switchLCL with isCurrent=false.
        // The retain window must still pin RWDB ledgers.
        std::vector<std::shared_ptr<Ledger const>> closed;
        for (std::uint32_t i = 0; i < kHistory + 4; ++i)
        {
            env(noop(alice));
            env.close();
            closed.push_back(std::dynamic_pointer_cast<Ledger const>(env.closed()));
        }

        auto const last = env.closed()->header().seq;
        // Within the configured history window: still resident and readable.
        auto const keptSeq = last - (kHistory - 1);
        BEAST_EXPECT(lm.haveLedger(keptSeq));
        auto const kept = lm.getLedgerBySeq(keptSeq);
        BEAST_EXPECT(kept);
        if (kept)
        {
            BEAST_EXPECT(kept->exists(keylet::account(alice.id())));
            BEAST_EXPECT(lm.getCloseTimeBySeq(kept->header().seq));
        }

        // Just outside the pin window: stop advertising. shouldAcquire
        // uses `<= ledger_history`, which is this sequence (distance N).
        // If it stayed complete, fetchForHistory would re-acquire it,
        // evict it, and spin. The SHAMap may still sit in TaggedCache
        // until sweep, so do not require getLedgerBySeq to fail.
        auto const outsideSeq = last - kHistory;
        BEAST_EXPECT(!lm.haveLedger(outsideSeq));

        // fetchForHistory shape: re-accept the evicted ledger. It must
        // not remain advertised.
        for (auto const& ledger : closed)
        {
            if (ledger && ledger->header().seq == outsideSeq)
            {
                lm.setFullLedger(ledger, false, false);
                BEAST_EXPECT(!lm.haveLedger(outsideSeq));
                break;
            }
        }
    }

    void
    testRWDBRetainWindowFollowsOnlineDelete()
    {
        testcase("RWDB retain window follows the larger online_delete");

        using namespace test::jtx;
        constexpr std::uint32_t kHistory = 4;
        constexpr std::uint32_t kDelete = 8;
        Env env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg = enableRWDB(std::move(cfg));
            cfg->fees.referenceFee = 10;
            cfg->ledgerHistory = kHistory;
            cfg->section(Sections::kNodeDatabase).set(Keys::kOnlineDelete, std::to_string(kDelete));
            return cfg;
        }));

        BEAST_EXPECT(env.app().getSHAMapStore().isNullBackend());
        BEAST_EXPECT(env.app().getSHAMapStore().getDeleteInterval() == kDelete);

        Account const alice{"alice"};
        env.fund(XRP(1000), alice);
        env.close();

        auto& lm = env.app().getLedgerMaster();
        for (std::uint32_t i = 0; i < kDelete + 4; ++i)
        {
            env(noop(alice));
            env.close();
        }

        auto const last = env.closed()->header().seq;
        // Past ledger_history, still inside online_delete: must stay
        // advertised and readable. This is the gap the pin used to miss.
        auto const keptSeq = last - (kDelete - 1);
        BEAST_EXPECT(lm.haveLedger(keptSeq));
        auto const kept = lm.getLedgerBySeq(keptSeq);
        BEAST_EXPECT(kept);
        if (kept)
            BEAST_EXPECT(kept->exists(keylet::account(alice.id())));

        BEAST_EXPECT(!lm.haveLedger(last - kDelete));
    }

    void
    testRWDBHistoryBackfillMarksComplete()
    {
        testcase("RWDB history backfill marks sequence complete");

        using namespace test::jtx;
        Env env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg = enableRWDB(std::move(cfg));
            cfg->fees.referenceFee = 10;
            return cfg;
        }));

        Account const alice{"alice"};
        env.fund(XRP(1000), alice);
        env.close();
        env(noop(alice));
        env.close();

        auto ledger = std::dynamic_pointer_cast<Ledger const>(env.closed());
        BEAST_EXPECT(ledger);
        if (!ledger)
            return;

        auto& lm = env.app().getLedgerMaster();
        // fetchForHistory calls setFullLedger(..., false, false).
        lm.setFullLedger(ledger, false, false);
        BEAST_EXPECT(lm.haveLedger(ledger->header().seq));
        auto const byHash = lm.getLedgerByHash(ledger->header().hash);
        BEAST_EXPECT(byHash);
        if (byHash)
            BEAST_EXPECT(byHash->header().seq == ledger->header().seq);
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testTxnIdFromIndex(features);
        testCloseTimeDoesNotLoadLedger();
        testRWDBCloseTimeFromRelationalHeader();
        testRWDBRetainWindowFollowsHistory();
        testRWDBRetainWindowFollowsOnlineDelete();
        testRWDBHistoryBackfillMarksComplete();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerMaster, app, xrpl);

}  // namespace xrpl::test

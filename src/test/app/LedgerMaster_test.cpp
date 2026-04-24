#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/noop.h>
#include <test/jtx/seq.h>

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/core/Config.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace xrpl::test {

class LedgerMaster_test : public beast::unit_test::suite
{
    static std::shared_ptr<Ledger const>
    asLedger(std::shared_ptr<ReadView const> const& ledger)
    {
        return std::dynamic_pointer_cast<Ledger const>(ledger);
    }

    static std::unique_ptr<Config>
    makeNetworkConfig(uint32_t networkID)
    {
        using namespace jtx;
        return envconfig([&](std::unique_ptr<Config> cfg) {
            cfg->NETWORK_ID = networkID;
            // This test relies on ledger hash so must lock it to fee 10.
            cfg->FEES.reference_fee = 10;
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

    void
    testLedgerAgeAndCatchup()
    {
        testcase("ledger age and catchup");

        using namespace std::chrono_literals;

        test::jtx::Env env{*this, makeNetworkConfig(11111)};
        auto& ledgerMaster = env.app().getLedgerMaster();

        std::string reason;
        BEAST_EXPECT(ledgerMaster.getPublishedLedgerAge() >= std::chrono::weeks{2});
        BEAST_EXPECT(ledgerMaster.getValidatedLedgerAge() >= std::chrono::weeks{2});
        BEAST_EXPECT(!ledgerMaster.isCaughtUp(reason));
        BEAST_EXPECT(reason == "No recently-published ledger");

        env.close();

        reason.clear();
        BEAST_EXPECT(ledgerMaster.getPublishedLedgerAge() == 0s);
        BEAST_EXPECT(ledgerMaster.getValidatedLedgerAge() == 0s);
        BEAST_EXPECT(ledgerMaster.isCaughtUp(reason));

        env.timeKeeper().set(env.timeKeeper().now() + 4min);
        reason.clear();
        BEAST_EXPECT(!ledgerMaster.isCaughtUp(reason));
        BEAST_EXPECT(reason == "No recently-published ledger");
    }

    void
    testCurrentLedgerChecks()
    {
        testcase("current ledger checks");

        using namespace std::chrono_literals;

        test::jtx::Env env{*this, makeNetworkConfig(22222)};
        auto& ledgerMaster = env.app().getLedgerMaster();

        std::vector<std::shared_ptr<Ledger const>> history;
        history.push_back(asLedger(env.closed()));
        for (int i = 0; i < 3; ++i)
        {
            env.close();
            history.push_back(asLedger(env.closed()));
        }

        for (auto const& ledger : history)
            BEAST_EXPECT(ledger);

        BEAST_EXPECT(!ledgerMaster.canBeCurrent(history.front()));

        env.timeKeeper().set(history.back()->header().closeTime + 10min);
        auto farFuture =
            std::make_shared<Ledger>(*history.back(), env.app().getTimeKeeper().closeTime());
        BEAST_EXPECT(!ledgerMaster.canBeCurrent(farFuture));

        env.timeKeeper().set(history.back()->header().closeTime + 30s);
        auto acceptable =
            std::make_shared<Ledger>(*history.back(), env.app().getTimeKeeper().closeTime());
        BEAST_EXPECT(ledgerMaster.canBeCurrent(acceptable));

        auto highSeq = acceptable;
        for (int i = 0; i < 32; ++i)
            highSeq = std::make_shared<Ledger>(*highSeq, env.app().getTimeKeeper().closeTime());
        BEAST_EXPECT(!ledgerMaster.canBeCurrent(highSeq));
    }

    void
    testValidatedRanges()
    {
        testcase("validated ranges");

        test::jtx::Env env{*this, makeNetworkConfig(33333)};
        auto& ledgerMaster = env.app().getLedgerMaster();

        std::uint32_t minVal = 0;
        std::uint32_t maxVal = 0;
        (void)ledgerMaster.getFullValidatedRange(minVal, maxVal);
        (void)ledgerMaster.getValidatedRange(minVal, maxVal);

        for (int i = 0; i < 4; ++i)
            env.close();

        auto published = asLedger(env.closed());
        BEAST_EXPECT(published);
        ledgerMaster.setLedgerRangePresent(1, published->seq());

        BEAST_EXPECT(ledgerMaster.getFullValidatedRange(minVal, maxVal));
        BEAST_EXPECT(minVal == 1);
        BEAST_EXPECT(maxVal == published->seq());

        BEAST_EXPECT(ledgerMaster.getValidatedRange(minVal, maxVal));
        BEAST_EXPECT(minVal == 1);
        BEAST_EXPECT(maxVal == published->seq());

        ledgerMaster.clearLedger(published->seq() - 1);
        BEAST_EXPECT(ledgerMaster.getFullValidatedRange(minVal, maxVal));
        BEAST_EXPECT(minVal == published->seq());
        BEAST_EXPECT(maxVal == published->seq());
    }

    void
    testLedgerLookupsAndHeldTransactions()
    {
        testcase("ledger lookups and held transactions");

        using namespace test::jtx;

        Env env{*this, makeNetworkConfig(44444)};
        auto& ledgerMaster = env.app().getLedgerMaster();
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        env.fund(XRP(1000), alice, bob);
        env.close();

        auto const validated = asLedger(env.closed());
        BEAST_EXPECT(validated);
        if (!validated)
            return;

        auto const published = asLedger(ledgerMaster.getPublishedLedger());
        BEAST_EXPECT(published);
        if (!published)
            return;

        BEAST_EXPECT(ledgerMaster.getCurrentLedgerIndex() == env.current()->seq());
        BEAST_EXPECT(ledgerMaster.getValidLedgerIndex() == validated->seq());
        BEAST_EXPECT(ledgerMaster.getClosedLedger()->seq() == validated->seq());
        auto const validatedLedger = ledgerMaster.getValidatedLedger();
        auto const bySeq = ledgerMaster.getLedgerBySeq(validated->seq());
        auto const byHash = ledgerMaster.getLedgerByHash(validated->header().hash);
        BEAST_EXPECT(validatedLedger);
        BEAST_EXPECT(bySeq);
        BEAST_EXPECT(byHash);
        if (!validatedLedger || !bySeq || !byHash)
            return;
        BEAST_EXPECT(validatedLedger->header().hash == validated->header().hash);
        BEAST_EXPECT(published->header().hash == validated->header().hash);
        BEAST_EXPECT(ledgerMaster.getHashBySeq(validated->seq()) == validated->header().hash);
        BEAST_EXPECT(bySeq->header().hash == validated->header().hash);
        BEAST_EXPECT(byHash->seq() == validated->seq());
        BEAST_EXPECT(!ledgerMaster.getLedgerBySeq(validated->seq() + 100));
        BEAST_EXPECT(!ledgerMaster.getLedgerByHash(uint256{1}));

        auto const closeBySeq = ledgerMaster.getCloseTimeBySeq(validated->seq());
        auto const closeByHash =
            ledgerMaster.getCloseTimeByHash(validated->header().hash, validated->seq());
        BEAST_EXPECT(closeBySeq && *closeBySeq == validated->header().closeTime);
        BEAST_EXPECT(closeByHash && *closeByHash == validated->header().closeTime);

        auto const walked =
            ledgerMaster.walkHashBySeq(validated->seq(), InboundLedger::Reason::GENERIC);
        BEAST_EXPECT(walked && *walked == validated->header().hash);

        ledgerMaster.setLedgerRangePresent(1, validated->seq());
        BEAST_EXPECT(ledgerMaster.haveLedger(validated->seq()));
        BEAST_EXPECT(ledgerMaster.isValidated(*validated));
        BEAST_EXPECT(!ledgerMaster.isValidated(*env.current()));
        BEAST_EXPECT(!ledgerMaster.getCompleteLedgers().empty());
        BEAST_EXPECT(ledgerMaster.getEarliestFetch() == 0);

        auto const seq1 = env.seq(alice);
        auto const seq2 = seq1 + 1;
        auto const jt1 = env.jt(noop(alice), seq(seq1));
        auto const jt2 = env.jt(noop(alice), seq(seq2));

        std::string reason1;
        std::string reason2;
        auto const tx1 = std::make_shared<Transaction>(jt1.stx, reason1, env.app());
        auto const tx2 = std::make_shared<Transaction>(jt2.stx, reason2, env.app());

        ledgerMaster.addHeldTransaction(tx2);
        auto const popped = ledgerMaster.popAcctTransaction(jt1.stx);
        BEAST_EXPECT(popped);
        BEAST_EXPECT(popped && popped->getTransactionID() == jt2.stx->getTransactionID());

        ledgerMaster.addHeldTransaction(tx1);
        ledgerMaster.addHeldTransaction(tx2);
        ledgerMaster.applyHeldTransactions();
        BEAST_EXPECT(env.current()->txExists(jt1.stx->getTransactionID()));
        BEAST_EXPECT(env.current()->txExists(jt2.stx->getTransactionID()));

        BEAST_EXPECT(!ledgerMaster.newPathRequest());
        BEAST_EXPECT(!ledgerMaster.isNewPathRequest());
        BEAST_EXPECT(!ledgerMaster.newOrderBookDB());
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{testable_amendments()};
        testWithFeats(all);
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testTxnIdFromIndex(features);
        testLedgerAgeAndCatchup();
        testCurrentLedgerChecks();
        testValidatedRanges();
        testLedgerLookupsAndHeldTransactions();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerMaster, app, xrpl);

}  // namespace xrpl::test

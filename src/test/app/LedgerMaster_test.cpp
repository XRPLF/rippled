//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 XRPLF

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
#include <test/jtx/Env.h>

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/misc/SHAMapStore.h>

namespace ripple {
namespace test {

class LedgerMaster_test : public beast::unit_test::suite
{
    std::unique_ptr<Config>
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
        auto const startLegSeq = env.current()->info().seq;
        for (int i = 0; i < 2; ++i)
        {
            env(noop(alice));
            txns.emplace_back(env.tx());
            env.close();
            metas.emplace_back(
                env.closed()->txRead(env.tx()->getTransactionID()).second);
        }
        // add last (empty) ledger
        env.close();
        auto const endLegSeq = env.closed()->info().seq;

        // test invalid range
        {
            std::uint32_t ledgerSeq = -1;
            std::uint32_t txnIndex = 0;
            auto result =
                env.app().getLedgerMaster().txnIdFromIndex(ledgerSeq, txnIndex);
            BEAST_EXPECT(!result);
        }
        // test not in ledger
        {
            uint32_t txnIndex = metas[0]->getFieldU32(sfTransactionIndex);
            auto result =
                env.app().getLedgerMaster().txnIdFromIndex(0, txnIndex);
            BEAST_EXPECT(!result);
        }
        // test empty ledger
        {
            auto result =
                env.app().getLedgerMaster().txnIdFromIndex(endLegSeq, 0);
            BEAST_EXPECT(!result);
        }
        // ended without result
        {
            uint32_t txnIndex = metas[0]->getFieldU32(sfTransactionIndex);
            auto result = env.app().getLedgerMaster().txnIdFromIndex(
                endLegSeq + 1, txnIndex);
            BEAST_EXPECT(!result);
        }
        // success (first tx)
        {
            uint32_t txnIndex = metas[0]->getFieldU32(sfTransactionIndex);
            auto result = env.app().getLedgerMaster().txnIdFromIndex(
                startLegSeq, txnIndex);
            BEAST_EXPECT(
                *result ==
                uint256("277F4FD89C20B92457FEF05FF63F6405563AD0563C73D967A29727"
                        "72679ADC65"));
        }
        // success (second tx)
        {
            uint32_t txnIndex = metas[1]->getFieldU32(sfTransactionIndex);
            auto result = env.app().getLedgerMaster().txnIdFromIndex(
                startLegSeq + 1, txnIndex);
            BEAST_EXPECT(
                *result ==
                uint256("293DF7335EBBAF4420D52E70ABF470EB4C5792CAEA2F91F76193C2"
                        "819F538FDE"));
        }
    }

    void
    testCompleteLedgerRange(FeatureBitset features)
    {
        // Note that this test is intentionally very similar to
        // SHAMapStore_test::testLedgerGaps, but has a different
        // focus.

        testcase("Complete Ledger operations");

        using namespace test::jtx;

        auto const deleteInterval = 8;

        Env env{*this, envconfig([](auto cfg) {
                    return online_delete(std::move(cfg), deleteInterval);
                })};

        auto const alice = Account("alice");
        env.fund(XRP(1000), alice);
        env.close();

        auto& lm = env.app().getLedgerMaster();
        LedgerIndex minSeq = 2;
        LedgerIndex maxSeq = env.closed()->info().seq;
        auto& store = env.app().getSHAMapStore();
        LedgerIndex lastRotated = store.getLastRotated();
        BEAST_EXPECTS(maxSeq == 3, to_string(maxSeq));
        BEAST_EXPECTS(
            lm.getCompleteLedgers() == "2-3", lm.getCompleteLedgers());
        BEAST_EXPECTS(lastRotated == 3, to_string(lastRotated));
        BEAST_EXPECT(lm.missingFromCompleteLedgerRange(minSeq, maxSeq) == 0);
        BEAST_EXPECT(
            lm.missingFromCompleteLedgerRange(minSeq + 1, maxSeq - 1) == 0);
        BEAST_EXPECT(
            lm.missingFromCompleteLedgerRange(minSeq - 1, maxSeq + 1) == 2);
        BEAST_EXPECT(
            lm.missingFromCompleteLedgerRange(minSeq - 2, maxSeq - 2) == 2);
        BEAST_EXPECT(
            lm.missingFromCompleteLedgerRange(minSeq + 2, maxSeq + 2) == 2);

        // Close enough ledgers to rotate a few times
        for (int i = 0; i < 24; ++i)
        {
            for (int t = 0; t < 3; ++t)
            {
                env(noop(alice));
            }
            env.close();
            store.rendezvous();

            ++maxSeq;

            if (maxSeq == lastRotated + deleteInterval)
            {
                minSeq = lastRotated;
                lastRotated = maxSeq;
            }
            BEAST_EXPECTS(
                env.closed()->info().seq == maxSeq,
                to_string(env.closed()->info().seq));
            BEAST_EXPECTS(
                store.getLastRotated() == lastRotated,
                to_string(store.getLastRotated()));
            std::stringstream expectedRange;
            expectedRange << minSeq << "-" << maxSeq;
            BEAST_EXPECTS(
                lm.getCompleteLedgers() == expectedRange.str(),
                lm.getCompleteLedgers());
            BEAST_EXPECT(
                lm.missingFromCompleteLedgerRange(minSeq, maxSeq) == 0);
            BEAST_EXPECT(
                lm.missingFromCompleteLedgerRange(minSeq + 1, maxSeq - 1) == 0);
            BEAST_EXPECT(
                lm.missingFromCompleteLedgerRange(minSeq - 1, maxSeq + 1) == 2);
            BEAST_EXPECT(
                lm.missingFromCompleteLedgerRange(minSeq - 2, maxSeq - 2) == 2);
            BEAST_EXPECT(
                lm.missingFromCompleteLedgerRange(minSeq + 2, maxSeq + 2) == 2);
        }
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
        testCompleteLedgerRange(features);
    }
};

BEAST_DEFINE_TESTSUITE(LedgerMaster, app, ripple);

}  // namespace test
}  // namespace ripple

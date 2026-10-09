
#include <test/jtx/Env.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/tx/apply.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace xrpl::test {

struct PseudoTx_test : public beast::unit_test::Suite
{
    static std::vector<STTx>
    getPseudoTxs(Rules const& rules, std::uint32_t seq)
    {
        std::vector<STTx> res;

        res.emplace_back(ttFEE, [&](auto& obj) {
            obj[sfAccount] = AccountID();
            obj[sfLedgerSequence] = seq;
            if (rules.enabled(featureXRPFees))
            {
                obj[sfBaseFeeDrops] = XRPAmount{0};
                obj[sfReserveBaseDrops] = XRPAmount{0};
                obj[sfReserveIncrementDrops] = XRPAmount{0};
            }
            else
            {
                obj[sfBaseFee] = 0;
                obj[sfReserveBase] = 0;
                obj[sfReserveIncrement] = 0;
                obj[sfReferenceFeeUnits] = 0;
            }
        });

        res.emplace_back(ttAMENDMENT, [&](auto& obj) {
            obj.setAccountID(sfAccount, AccountID());
            obj.setFieldH256(sfAmendment, UInt256(2));
            obj.setFieldU32(sfLedgerSequence, seq);
        });

        res.emplace_back(ttBATCH_RESULT, [&](auto& obj) {
            obj.setAccountID(sfAccount, AccountID());
            obj.setFieldH256(sfParentBatchID, UInt256(3));
            obj.setFieldArray(sfBatchResults, STArray(sfBatchResults));
        });

        return res;
    }

    static std::vector<STTx>
    getRealTxs()
    {
        std::vector<STTx> res;

        res.emplace_back(ttACCOUNT_SET, [&](auto& obj) { obj[sfAccount] = AccountID(1); });

        res.emplace_back(ttPAYMENT, [&](auto& obj) {
            obj.setAccountID(sfAccount, AccountID(2));
            obj.setAccountID(sfDestination, AccountID(3));
        });

        return res;
    }

    void
    testPrevented(FeatureBitset features)
    {
        using namespace jtx;
        Env env(*this, features);

        for (auto const& stx : getPseudoTxs(env.closed()->rules(), env.closed()->seq() + 1))
        {
            std::string reason;
            BEAST_EXPECT(isPseudoTx(stx));
            BEAST_EXPECT(!passesLocalChecks(stx, reason));
            BEAST_EXPECT(reason == "Cannot submit pseudo transactions.");
            env.app().getOpenLedger().modify([&](OpenView& view, beast::Journal j) {
                auto const result = xrpl::apply(env.app(), view, stx, TapNone, j);
                BEAST_EXPECT(!result.applied && result.ter == temINVALID);
                return result.applied;
            });
        }
    }

    void
    testBatchResultRejectedOnClosedView(FeatureBitset features)
    {
        using namespace jtx;
        Env env(*this, features);

        // A BatchResult placed in a proposed transaction set is applied to a closed view.
        auto const pseudoTxs = getPseudoTxs(env.closed()->rules(), env.closed()->seq() + 1);
        auto const record = std::ranges::find_if(
            pseudoTxs, [](STTx const& stx) { return stx.getTxnType() == ttBATCH_RESULT; });
        if (!BEAST_EXPECT(record != pseudoTxs.end()))
            return;

        OpenView closedView(&*env.closed());
        BEAST_EXPECT(!closedView.open());
        auto const result = xrpl::apply(env.app(), closedView, *record, TapNone, env.journal);
        BEAST_EXPECT(!result.applied && result.ter == temINVALID);
        BEAST_EXPECT(closedView.txCount() == 0);
    }

    void
    testAllowed()
    {
        for (auto const& stx : getRealTxs())
        {
            std::string reason;
            BEAST_EXPECT(!isPseudoTx(stx));
            BEAST_EXPECT(passesLocalChecks(stx, reason));
        }
    }

    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{testableAmendments()};
        FeatureBitset const xrpFees{featureXRPFees};

        testPrevented(all - featureXRPFees);
        testPrevented(all);
        testBatchResultRejectedOnClosedView(all);
        testBatchResultRejectedOnClosedView(all - featureBatchV2);
        testAllowed();
    }
};

BEAST_DEFINE_TESTSUITE(PseudoTx, app, xrpl);

}  // namespace xrpl::test

#include <test/app/invariants/InvariantsBase.h>
#include <test/jtx/AMM.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/mpt.h>
#include <test/jtx/vault.h>
#include <test/unit_test/SuiteJournal.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/applySteps.h>
#include <xrpl/tx/invariants/AMMInvariant.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>

namespace xrpl::test {

class InvariantsAMM_test : public InvariantsBase
{
    FeatureBitset const all_{test::jtx::testableAmendments()};

    void
    testAMMDeleteInvariants(FeatureBitset features)
    {
        using namespace test::jtx;

        bool const enforceAMMDelete = features[fixCleanup3_3_0];
        testcase << "AMM delete invariants" + std::string(enforceAMMDelete ? " fix" : "");

        Env env(*this, features);
        Account const issuer{"issuer"};
        Issue const lptIssue{Currency(0x4c50540000000000), issuer.id()};
        STAmount const zeroLP{lptIssue, 0};
        STAmount const nonZeroLP{lptIssue, 1};

        auto const makeAMM = [](STAmount const& lptBalance) {
            auto sleAMM = std::make_shared<SLE>(keylet::amm(uint256(1)));
            sleAMM->setFieldAmount(sfLPTokenBalance, lptBalance);
            return sleAMM;
        };

        auto const checkInvariant = [&](TxType txType,
                                        TER result,
                                        std::optional<STAmount> const& deletedLPBalance,
                                        bool expected,
                                        std::string const& expectedLog) {
            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            ValidAMM invariant;

            if (deletedLPBalance)
                invariant.visitEntry(true, makeAMM(*deletedLPBalance), nullptr);

            bool const actual = invariant.finalize(
                STTx{txType, [](STObject&) {}}, result, XRPAmount{}, *env.current(), jlog);

            BEAST_EXPECTS(actual == expected, "unexpected AMM delete invariant result");
            auto const messages = sink.messages().str();
            auto const expectedLogWhenEnforced = enforceAMMDelete ? expectedLog : "";
            if (!expectedLogWhenEnforced.empty())
            {
                BEAST_EXPECTS(messages.contains(expectedLogWhenEnforced), expectedLogWhenEnforced);
            }
            else
            {
                BEAST_EXPECTS(messages.empty(), messages);
            }
        };

        checkInvariant(
            ttPAYMENT,
            tesSUCCESS,
            nonZeroLP,
            !enforceAMMDelete,
            "Invariant failed: AMM failed, unexpected AMM deletion by");
        checkInvariant(
            ttAMM_DELETE,
            tesSUCCESS,
            std::nullopt,
            !enforceAMMDelete,
            "Invariant failed: AMMDelete failed, AMM object remained on tesSUCCESS");
        checkInvariant(
            ttAMM_DELETE,
            tesSUCCESS,
            nonZeroLP,
            !enforceAMMDelete,
            "Invariant failed: AMMDelete failed, AMM object deleted with non-zero LP balance");
        checkInvariant(
            ttAMM_DELETE,
            tecINCOMPLETE,
            zeroLP,
            !enforceAMMDelete,
            "Invariant failed: AMMDelete failed, AMM object deleted when result is not tesSUCCESS");

        checkInvariant(ttAMM_WITHDRAW, tesSUCCESS, nonZeroLP, true, "");
        checkInvariant(ttAMM_CLAWBACK, tesSUCCESS, nonZeroLP, true, "");

        checkInvariant(ttAMM_DELETE, tesSUCCESS, zeroLP, true, "");
        checkInvariant(ttAMM_WITHDRAW, tesSUCCESS, zeroLP, true, "");
        checkInvariant(ttAMM_CLAWBACK, tesSUCCESS, zeroLP, true, "");
    }

    void
    testAMM()
    {
        testcase << "AMM";
        using namespace jtx;

        MPTID mptID{};
        uint256 ammID{};
        AccountID ammAccountID{};
        Account const gw{"gw"};
        Issue lptIssue{};
        PrettyAsset poolAsset{xrpIssue()};

        auto deleteAMMAccount = [&](ApplyContext& ac, bool) {
            auto sle = ac.view().peek(keylet::account(ammAccountID));
            if (!sle)
                return false;
            ac.view().erase(sle);
            return true;
        };

        auto updateLPTokensBalance = [&](ApplyContext& ac, std::int64_t amount) {
            auto sle = ac.view().peek(keylet::amm(ammID));
            if (!sle)
                return false;
            sle->setFieldAmount(sfLPTokenBalance, STAmount{lptIssue, amount});
            ac.view().update(sle);
            return true;
        };
        auto updateLPTokensBadAmount = [&](ApplyContext& ac, bool) {
            return updateLPTokensBalance(ac, -1);
        };
        auto updateLPTokensBadBalance = [&](ApplyContext& ac, bool) {
            return updateLPTokensBalance(ac, 200'000'000);
        };
        auto updateAMM = [&](ApplyContext& ac, bool) { return updateLPTokensBalance(ac, 10); };

        auto updateAMMPool = [&](ApplyContext& ac, bool isMPT) {
            if (isMPT)
            {
                auto sle = ac.view().peek(keylet::mptoken(mptID, ammAccountID));
                if (!sle)
                    return false;
                sle->setFieldU64(sfMPTAmount, 1);
                ac.view().update(sle);
                return true;
            }
            auto sle = ac.view().peek(keylet::account(ammAccountID));
            if (!sle)
                return false;
            sle->setFieldAmount(sfBalance, XRP(1));
            ac.view().update(sle);
            return true;
        };

        auto test = [&](auto const txType,
                        auto&& update,
                        bool isMPT,
                        TER error = tecINVARIANT_FAILED) {
            doInvariantCheck(
                {{"AMM"}},
                [&](Account const&, Account const&, ApplyContext& ac) { return update(ac, isMPT); },
                XRPAmount{},
                STTx{txType, [&](STObject& tx) {}},
                {tecINVARIANT_FAILED, error},
                [&](Account const&, Account const&, Env& env) {
                    env.fund(XRP(1'000), gw);
                    poolAsset = [&]() -> PrettyAsset {
                        if (isMPT)
                        {
                            MPT const mpt = MPTTester({.env = env, .issuer = gw});
                            mptID = mpt.issuanceID;
                            return mpt;
                        }
                        return gw["USD"];
                    }();
                    AMM const amm(env, gw, XRP(100), poolAsset(100));
                    ammAccountID = amm.ammAccount();
                    ammID = amm.ammID();
                    lptIssue = amm.lptIssue();
                    return true;
                });
        };

        for (bool const isMPT : {false, true})
        {
            // Under fixCleanup3_4_0 the MPT balance invariants also fire on the
            // second pass, so both IOU and MPT pools now escalate to tef.
            auto const error = TER(tefINVARIANT_FAILED);
            for (auto txType : {ttAMM_CREATE, ttAMM_DEPOSIT, ttAMM_CLAWBACK, ttAMM_WITHDRAW})
            {
                test(txType, deleteAMMAccount, isMPT, tefINVARIANT_FAILED);
                test(txType, updateLPTokensBadAmount, isMPT);
                test(txType, updateLPTokensBadBalance, isMPT);
            }
            for (auto txType : {ttAMM_BID, ttAMM_VOTE})
            {
                test(txType, updateAMMPool, isMPT, error);
                test(txType, updateLPTokensBadAmount, isMPT);
                test(txType, updateLPTokensBadBalance, isMPT);
            }
            for (auto txType : {ttAMM_DELETE, ttCHECK_CASH, ttOFFER_CREATE, ttPAYMENT})
            {
                test(txType, updateAMM, isMPT);
            }
        }
    }

    // Test the invariant overwrite fix for both pre- and post-amendment
    // behavior. With the fix enabled, |= accumulates violations across
    // entries so a later valid entry cannot clear an earlier violation.
    // Without the fix, = assignment means the last-visited entry wins.

    void
    run() override
    {
        testAMMDeleteInvariants(all_);
        testAMMDeleteInvariants(all_ - fixCleanup3_3_0);
        testAMM();
    }
};

BEAST_DEFINE_TESTSUITE(InvariantsAMM, app, xrpl);

}  // namespace xrpl::test

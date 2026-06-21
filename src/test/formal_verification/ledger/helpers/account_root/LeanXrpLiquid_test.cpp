#include <test/formal_verification/ffi/ledger/helpers/AccountRootHelpersFFI.h>
#include <test/formal_verification/ledger/LedgerSuite.h>
#include <test/jtx.h>

#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>

#include <cstdint>
#include <exception>
#include <limits>
#include <string>

namespace xrpl::test {

using namespace formal_verification;

class LeanXrpLiquid_test : public LedgerSuite
{
    void
    runXrpLiquid(
        ReadView const& view,
        AccountID const& id,
        int32_t ownerCountAdj,
        XRPAmount expected,
        char const* label)
    {
        runLedgerTest(view, label, [&](LedgerFFI const& ledger) {
            XRPAmount const cppRes =
                xrpLiquid(view, id, ownerCountAdj, beast::Journal{beast::Journal::getNullSink()});
            LeanXRPAmountResult const leanRes =
                formal_verification::xrpLiquid(ledger, id, ownerCountAdj);
            BEAST_EXPECT(cppRes == expected);
            BEAST_EXPECTS(!leanRes.threw, leanRes.error);
            BEAST_EXPECT(leanRes.value == cppRes);
        });
    }

    void
    runXrpLiquidThrows(
        ReadView const& view,
        AccountID const& id,
        int32_t ownerCountAdj,
        char const* label)
    {
        runLedgerTest(view, label, [&](LedgerFFI const& ledger) {
            bool cppThrew = false;
            std::string cppError;
            try
            {
                xrpLiquid(view, id, ownerCountAdj, beast::Journal{beast::Journal::getNullSink()});
            }
            catch (std::exception const& e)
            {
                cppThrew = true;
                cppError = e.what();
            }
            LeanXRPAmountResult const leanRes =
                formal_verification::xrpLiquid(ledger, id, ownerCountAdj);
            BEAST_EXPECT(cppThrew);
            BEAST_EXPECT(leanRes.threw);
            BEAST_EXPECTS(
                !cppError.empty() && cppError == leanRes.error,
                "cpp=[" + cppError + "] lean=[" + leanRes.error + "]");
        });
    }

    void
    testXrpLiquid()
    {
        using namespace jtx;
        Account const alice("alice");
        Account const ghost("ghost");
        Env env(*this);
        env.fund(XRP(1000), alice);
        env.close();
        auto const& fees = env.current()->fees();
        XRPAmount const base = fees.accountReserve(0);
        constexpr uint32_t u32max = std::numeric_limits<uint32_t>::max();
        constexpr int32_t i32min = std::numeric_limits<int32_t>::min();
        XRPAmount const maxXrp{100'000'000'000'000'000};  // 10^17 drops

        {
            Sandbox sb(&*env.current(), TapNone);
            runXrpLiquid(sb, ghost.id(), 0, XRPAmount{0}, "xrpLiquid.absent");
        }

        // Stage a known balance + ownerCount on alice, then query liquid XRP.
        auto runStaged = [&](XRPAmount balance,
                             uint32_t ownerCount,
                             int32_t adj,
                             XRPAmount expected,
                             char const* label) {
            Sandbox sb(&*env.current(), TapNone);
            auto sle = sb.peek(keylet::account(alice.id()));
            sle->setFieldAmount(sfBalance, STAmount{balance});
            sle->setFieldU32(sfOwnerCount, ownerCount);
            sb.update(sle);
            runXrpLiquid(sb, alice.id(), adj, expected, label);
        };

        // confineOwnerCount: ownerCount + adj (increment, decrement, underflow clamp)
        runStaged(maxXrp, 1, 3, maxXrp - fees.accountReserve(4), "xrpLiquid.adj_increment");
        runStaged(maxXrp, 5, -2, maxXrp - fees.accountReserve(3), "xrpLiquid.adj_decrement");
        runStaged(maxXrp, 3, i32min, maxXrp - base, "xrpLiquid.owner_count_underflow_clamp");
        // pseudo-account -> reserve is zero regardless of owner count
        {
            Sandbox sb(&*env.current(), TapNone);
            auto sle = sb.peek(keylet::account(alice.id()));
            sle->setFieldAmount(sfBalance, STAmount{base});
            sle->setFieldU32(sfOwnerCount, 5);
            sle->setFieldH256(sfAMMID, uint256{1});  // marks alice a pseudo-account
            sb.update(sle);
            runXrpLiquid(sb, alice.id(), 0, base, "xrpLiquid.pseudo_account");
        }
        // reserve = accountReserve(ownerCount)
        runStaged(maxXrp, 2, 0, maxXrp - fees.accountReserve(2), "xrpLiquid.owner_count_scaling");
        // ownerCount + adj overflows uint32 -> clamp to UINT32_MAX -> reserve out of range
        {
            Sandbox sb(&*env.current(), TapNone);
            auto sle = sb.peek(keylet::account(alice.id()));
            sle->setFieldAmount(sfBalance, STAmount{maxXrp});
            sle->setFieldU32(sfOwnerCount, u32max - 1);
            sb.update(sle);
            runXrpLiquidThrows(sb, alice.id(), 5, "xrpLiquid.owner_count_overflow_clamp");
        }
        // balance < reserve -> zero, otherwise balance - reserve
        runStaged(XRPAmount{0}, 0, 0, XRPAmount{0}, "xrpLiquid.zero_balance");
        runStaged(base, 0, 0, XRPAmount{0}, "xrpLiquid.balance_eq_reserve");
        runStaged(base + XRPAmount{1}, 0, 0, XRPAmount{1}, "xrpLiquid.balance_just_over_reserve");
        runStaged(maxXrp, 0, 0, maxXrp - base, "xrpLiquid.max_balance");
    }

    void
    runTests() override
    {
        testXrpLiquid();
    }
};

BEAST_DEFINE_TESTSUITE(LeanXrpLiquid, formal_verification, xrpl);

}  // namespace xrpl::test

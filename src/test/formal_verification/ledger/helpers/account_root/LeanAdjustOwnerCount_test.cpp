#include <test/formal_verification/ffi/ledger/helpers/AccountRootHelpersFFI.h>
#include <test/formal_verification/ledger/LedgerSuite.h>
#include <test/jtx.h>

#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>

#include <cstdint>
#include <limits>
#include <memory>

namespace xrpl::test {

using namespace formal_verification;

class LeanAdjustOwnerCount_test : public LedgerSuite
{
    void
    runAdjustOwnerCount(
        jtx::Env& env,
        AccountID const& acct,
        uint32_t current,
        int32_t amount,
        uint32_t expected,
        char const* label)
    {
        // Stage a known starting ownerCount on the sandbox before mirroring.
        Sandbox sb(&*env.current(), TapNone);
        auto sle = sb.peek(keylet::account(acct));
        sle->at(sfOwnerCount) = current;
        sb.update(sle);
        AccountRootFFI const acctFFI =
            AccountRootFFIBuilder().fromCpp(ledger_entries::AccountRoot(sle)).build(sle->key());

        runLedgerTest(sb, label, [&](LedgerFFI& ledger) {
            adjustOwnerCount(sb, sle, amount, beast::Journal{beast::Journal::getNullSink()});
            auto const err = formal_verification::adjustOwnerCount(ledger, &acctFFI, amount);
            BEAST_EXPECT(sle->getFieldU32(sfOwnerCount) == expected);
            BEAST_EXPECTS(!err, err.value_or(""));
        });
    }

    void
    testAdjustOwnerCount()
    {
        using namespace jtx;
        Env env(*this);
        Account const alice("alice");
        env.fund(XRP(1000), alice);
        env.close();

        constexpr uint32_t max = std::numeric_limits<std::uint32_t>::max();
        {
            Sandbox sb(&*env.current(), TapNone);
            runLedgerTest(sb, "adjustOwnerCount.null", [&](LedgerFFI& ledger) {
                std::shared_ptr<SLE> nullSle;
                adjustOwnerCount(sb, nullSle, 1, beast::Journal{beast::Journal::getNullSink()});
                auto const err = formal_verification::adjustOwnerCount(ledger, nullptr, 1);
                BEAST_EXPECTS(!err, err.value_or(""));
            });
        }
        runAdjustOwnerCount(env, alice.id(), 5, 3, 8, "adjustOwnerCount.increment");
        runAdjustOwnerCount(env, alice.id(), 5, -2, 3, "adjustOwnerCount.decrement");
        runAdjustOwnerCount(env, alice.id(), 3, -3, 0, "adjustOwnerCount.to_zero");
        runAdjustOwnerCount(env, alice.id(), max - 1, 5, max, "adjustOwnerCount.overflow_clamp");
        runAdjustOwnerCount(env, alice.id(), 0, -1, 0, "adjustOwnerCount.underflow_clamp");
    }

    void
    runTests() override
    {
        testAdjustOwnerCount();
    }
};

BEAST_DEFINE_TESTSUITE(LeanAdjustOwnerCount, formal_verification, xrpl);

}  // namespace xrpl::test

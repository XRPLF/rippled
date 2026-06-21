#include <test/formal_verification/ffi/ledger/ViewFFI.h>
#include <test/formal_verification/ffi/protocol/TerFFI.h>
#include <test/formal_verification/ledger/LedgerSuite.h>
#include <test/jtx.h>

#include <xrpl/ledger/View.h>

#include <cstdint>
#include <optional>

namespace xrpl::test {

using namespace formal_verification;

class LeanHasExpired_test : public LedgerSuite
{
    void
    runHasExpired(
        ReadView const& view,
        std::optional<uint32_t> const& exp,
        bool expected,
        char const* label)
    {
        runLedgerTest(view, label, [&](LedgerFFI const& ledger) {
            bool const cppResult = hasExpired(view, exp);
            LeanBoolResult const leanRes = formal_verification::hasExpired(ledger, exp);
            BEAST_EXPECT(cppResult == expected);
            BEAST_EXPECTS(!leanRes.threw, leanRes.error);
            BEAST_EXPECT(leanRes.value == cppResult);
        });
    }

    void
    testHasExpired()
    {
        using namespace jtx;
        Env env(*this);
        env.close();
        // exp is compared against the ledger's parentCloseTime
        auto const now = env.current()->parentCloseTime().time_since_epoch().count();

        runHasExpired(*env.current(), std::nullopt, false, "hasExpired.none");
        runHasExpired(*env.current(), std::optional<uint32_t>(now), true, "hasExpired.boundary");
        runHasExpired(*env.current(), std::optional<uint32_t>(now - 1), true, "hasExpired.past");
        runHasExpired(
            *env.current(), std::optional<uint32_t>(now + 100000), false, "hasExpired.future");
    }

    void
    runTests() override
    {
        testHasExpired();
    }
};

BEAST_DEFINE_TESTSUITE(LeanHasExpired, formal_verification, xrpl);

}  // namespace xrpl::test

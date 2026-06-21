#include <test/formal_verification/ffi/ledger/helpers/RippleStateHelpersFFI.h>
#include <test/formal_verification/ffi/protocol/TerFFI.h>
#include <test/formal_verification/ledger/LedgerSuite.h>
#include <test/jtx.h>

#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/protocol/Indexes.h>

#include <algorithm>

namespace xrpl::test {

using namespace formal_verification;

class LeanTrustDelete_test : public LedgerSuite
{
    void
    runTrustDelete(
        Sandbox& sb,
        AccountID const& a,
        AccountID const& b,
        Currency const& currency,
        TER expected,
        char const* label)
    {
        AccountID const uLow = std::min(a, b);
        AccountID const uHigh = std::max(a, b);
        auto sle = sb.peek(keylet::line(uLow, uHigh, currency));
        RippleStateFFI const rsFFI =
            RippleStateFFIBuilder().fromCpp(ledger_entries::RippleState(sle)).build(sle->key());
        runLedgerTest(sb, label, [&](LedgerFFI& ledger) {
            TER const cppTer =
                trustDelete(sb, sle, uLow, uHigh, beast::Journal{beast::Journal::getNullSink()});
            LeanTerResult const leanRes =
                formal_verification::trustDelete(ledger, rsFFI, uLow, uHigh);
            BEAST_EXPECT(cppTer == expected);
            BEAST_EXPECTS(!leanRes.threw, leanRes.error);
            BEAST_EXPECT(leanRes.value == cppTerByte(cppTer));
        });
    }

    void
    testTrustDelete()
    {
        using namespace jtx;
        Account const gw("gw");
        Account const alice("alice");
        Env env(*this);
        env.fund(XRP(1000), gw, alice);
        env(trust(alice, gw["USD"](100)));
        env.close();
        Currency const usd = toCurrency("USD");

        Sandbox sb(&*env.current(), TapNone);
        runTrustDelete(sb, alice.id(), gw.id(), usd, tesSUCCESS, "trustDelete.success");
    }

    void
    runTests() override
    {
        testTrustDelete();
    }
};

BEAST_DEFINE_TESTSUITE(LeanTrustDelete, formal_verification, xrpl);

}  // namespace xrpl::test

#include <test/formal_verification/ffi/ledger/helpers/RippleStateHelpersFFI.h>
#include <test/formal_verification/ffi/protocol/TerFFI.h>
#include <test/formal_verification/ledger/LedgerSuite.h>
#include <test/jtx.h>

#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/protocol/Indexes.h>

namespace xrpl::test {

using namespace formal_verification;

class LeanCreditLimit_test : public LedgerSuite
{
    void
    runCreditLimit(
        Sandbox& sb,
        AccountID const& account,
        AccountID const& issuer,
        Currency const& currency,
        STAmount const& expected,
        char const* label)
    {
        runLedgerTest(sb, label, [&](LedgerFFI const& ledger) {
            STAmount const cppRes = creditLimit(sb, account, issuer, currency);
            LeanSTAmountResult const leanRes =
                formal_verification::creditLimit(ledger, account, issuer, currency);
            BEAST_EXPECTS(!leanRes.threw, leanRes.error);
            BEAST_EXPECT(cppRes == expected);
            BEAST_EXPECT(cppRes.getIssuer() == account);
            BEAST_EXPECT(leanRes.value == cppRes);
            BEAST_EXPECT(leanRes.value.getIssuer() == account);
        });
    }

    void
    testCreditLimit()
    {
        using namespace jtx;
        Account const gw("gw");
        Account const alice("alice");
        Account const bob("bob");      // gw trusts bob; bob sets no limit
        Account const carol("carol");  // no line with alice
        Env env(*this);
        env.fund(XRP(1000), gw, alice, bob, carol);
        env(trust(alice, gw["USD"](100)));  // alice's limit toward gw
        env(trust(gw, alice["USD"](200)));  // gw's limit toward alice
        env(trust(gw, bob["USD"](50)));     // gw's limit toward bob; bob sets none
        env.close();
        Currency const usd = toCurrency("USD");

        Sandbox sb(&*env.current(), TapNone);
        runCreditLimit(sb, alice.id(), gw.id(), usd, alice["USD"](100), "creditLimit.account");
        runCreditLimit(sb, gw.id(), alice.id(), usd, gw["USD"](200), "creditLimit.peer");
        runCreditLimit(sb, bob.id(), gw.id(), usd, bob["USD"](0), "creditLimit.line_zero");
        runCreditLimit(sb, alice.id(), carol.id(), usd, alice["USD"](0), "creditLimit.no_line");
    }

    void
    runTests() override
    {
        testCreditLimit();
    }
};

BEAST_DEFINE_TESTSUITE(LeanCreditLimit, formal_verification, xrpl);

}  // namespace xrpl::test

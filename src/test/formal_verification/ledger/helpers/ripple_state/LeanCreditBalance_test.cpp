#include <test/formal_verification/ffi/ledger/helpers/RippleStateHelpersFFI.h>
#include <test/formal_verification/ffi/protocol/TerFFI.h>
#include <test/formal_verification/ledger/LedgerSuite.h>
#include <test/jtx.h>

#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/protocol/Indexes.h>

namespace xrpl::test {

using namespace formal_verification;

class LeanCreditBalance_test : public LedgerSuite
{
    void
    runCreditBalance(
        Sandbox& sb,
        AccountID const& account,
        AccountID const& issuer,
        Currency const& currency,
        STAmount const& expected,
        char const* label)
    {
        runLedgerTest(sb, label, [&](LedgerFFI const& ledger) {
            STAmount const cppRes = creditBalance(sb, account, issuer, currency);
            LeanSTAmountResult const leanRes =
                formal_verification::creditBalance(ledger, account, issuer, currency);
            BEAST_EXPECTS(!leanRes.threw, leanRes.error);
            BEAST_EXPECT(cppRes == expected);
            BEAST_EXPECT(cppRes.getIssuer() == account);
            BEAST_EXPECT(leanRes.value == cppRes);
            BEAST_EXPECT(leanRes.value.getIssuer() == account);
        });
    }

    void
    testCreditBalance()
    {
        using namespace jtx;
        Account const gw("gw");
        Account const alice("alice");
        Account const carol("carol");
        Env env(*this);
        env.fund(XRP(1000), gw, alice, carol);
        env(trust(alice, gw["USD"](100)));
        env(pay(gw, alice, gw["USD"](30)));
        env.close();
        Currency const usd = toCurrency("USD");

        Sandbox sb(&*env.current(), TapNone);
        runCreditBalance(sb, alice.id(), gw.id(), usd, alice["USD"](-30), "creditBalance.account");
        runCreditBalance(sb, gw.id(), alice.id(), usd, gw["USD"](30), "creditBalance.peer");
        runCreditBalance(sb, alice.id(), carol.id(), usd, alice["USD"](0), "creditBalance.no_line");
    }

    void
    runTests() override
    {
        testCreditBalance();
    }
};

BEAST_DEFINE_TESTSUITE(LeanCreditBalance, formal_verification, xrpl);

}  // namespace xrpl::test

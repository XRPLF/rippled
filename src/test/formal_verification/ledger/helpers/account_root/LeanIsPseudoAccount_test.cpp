#include <test/formal_verification/ffi/ledger/helpers/AccountRootHelpersFFI.h>
#include <test/formal_verification/ledger/LedgerSuite.h>
#include <test/jtx.h>

#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>

namespace xrpl::test {

using namespace formal_verification;

class LeanIsPseudoAccount_test : public LedgerSuite
{
    void
    runIsPseudoAccount(
        ReadView const& view,
        AccountID const& accountId,
        bool expected,
        char const* label)
    {
        runLedgerTest(view, label, [&](LedgerFFI const& ledger) {
            bool const cpp = isPseudoAccount(view, accountId);
            LeanBoolResult const leanRes = formal_verification::isPseudoAccount(ledger, accountId);
            BEAST_EXPECT(cpp == expected);
            BEAST_EXPECTS(!leanRes.threw, leanRes.error);
            BEAST_EXPECT(leanRes.value == cpp);
        });
    }

    // Stage a single pseudo-marker field on alice and assert it marks the account.
    void
    runStagedPseudoField(SField const& field, char const* label)
    {
        using namespace jtx;
        Env env(*this);
        Account const alice("alice");
        env.fund(XRP(1000), alice);
        env.close();
        Sandbox sb(&*env.current(), TapNone);
        auto acct = sb.peek(keylet::account(alice.id()));
        acct->setFieldH256(field, uint256{1});
        sb.update(acct);
        runIsPseudoAccount(sb, alice.id(), true, label);
    }

    void
    testIsPseudoAccount()
    {
        using namespace jtx;
        {  // account never funded
            Env env(*this);
            Account const ghost("ghost");
            env.fund(XRP(1000), "alice");
            env.close();
            runIsPseudoAccount(*env.current(), ghost.id(), false, "isPseudoAccount.absent");
        }
        {  // normal funded account, no pseudo-marker fields
            Env env(*this);
            Account const alice("alice");
            env.fund(XRP(1000), alice);
            env.close();
            runIsPseudoAccount(*env.current(), alice.id(), false, "isPseudoAccount.normal");
        }
        // each pseudo-marker field independently marks the account
        runStagedPseudoField(sfVaultID, "isPseudoAccount.vault_id");
        runStagedPseudoField(sfAMMID, "isPseudoAccount.amm_id");
        runStagedPseudoField(sfLoanBrokerID, "isPseudoAccount.loan_broker_id");
    }

    void
    runTests() override
    {
        testIsPseudoAccount();
    }
};

BEAST_DEFINE_TESTSUITE(LeanIsPseudoAccount, formal_verification, xrpl);

}  // namespace xrpl::test

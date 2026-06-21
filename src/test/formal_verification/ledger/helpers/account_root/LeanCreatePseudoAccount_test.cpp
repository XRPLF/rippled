#include <test/formal_verification/ffi/ledger/helpers/AccountRootHelpersFFI.h>
#include <test/formal_verification/ffi/protocol/UInt256FFI.h>
#include <test/formal_verification/ledger/LedgerSuite.h>
#include <test/jtx.h>

#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>

#include <cstdint>

namespace xrpl::test {

using namespace formal_verification;

class LeanCreatePseudoAccount_test : public LedgerSuite
{
    void
    runCreatePseudoAccount(
        Sandbox& sb,
        uint256 const& key,
        SField const& ownerField,
        uint8_t ownerFieldCode,
        char const* label)
    {
        runLedgerTest(sb, label, [&](LedgerFFI& ledger) {
            auto const cpp = createPseudoAccount(sb, key, ownerField);
            LeanViewResult r = ledger.leanApplyView(
                lean_create_pseudo_account, UInt256FFI::build(key), ownerFieldCode);
            BEAST_EXPECTS(r.ok(), r.error());  // model did not raise
            lean_object* inner = r.okValue();  // Except TER AccountRoot
            BEAST_EXPECT(static_cast<bool>(cpp) == exceptOk(inner));
            if (!cpp)
                BEAST_EXPECT(terTag(exceptVal(inner)) == cppTerByte(cpp.error()));
            // success path: the created account is verified by expectLedgersMatch
        });
    }

    void
    testCreatePseudoAccount()
    {
        using namespace jtx;
        {  // ownerField = sfVaultID
            Env env(*this);
            env.fund(XRP(1000), "alice");
            env.close();
            Sandbox sb(&*env.current(), TapNone);
            runCreatePseudoAccount(sb, uint256{1}, sfVaultID, 0, "createPseudoAccount.vault_id");
        }
        {  // ownerField = sfLoanBrokerID
            Env env(*this);
            env.fund(XRP(1000), "alice");
            env.close();
            Sandbox sb(&*env.current(), TapNone);
            runCreatePseudoAccount(
                sb, uint256{2}, sfLoanBrokerID, 1, "createPseudoAccount.loan_broker_id");
        }
        {  // same key twice: the second derivation skips the occupied address (i=0) to i=1
            Env env(*this);
            env.fund(XRP(1000), "alice");
            env.close();
            Sandbox sb(&*env.current(), TapNone);
            runCreatePseudoAccount(sb, uint256{3}, sfVaultID, 0, "createPseudoAccount.same_key_1");
            runCreatePseudoAccount(sb, uint256{3}, sfVaultID, 0, "createPseudoAccount.same_key_2");
        }
        {  // all 256 candidate addresses occupied -> tecDUPLICATE
            Env env(*this);
            env.fund(XRP(1000), "alice");
            env.close();
            Sandbox sb(&*env.current(), TapNone);
            uint256 const key{7};
            for (int i = 0; i < 256; ++i)
                (void)createPseudoAccount(sb, key, sfVaultID);
            runCreatePseudoAccount(sb, key, sfVaultID, 0, "createPseudoAccount.duplicate");
        }
    }

    void
    runTests() override
    {
        testCreatePseudoAccount();
    }
};

BEAST_DEFINE_TESTSUITE(LeanCreatePseudoAccount, formal_verification, xrpl);

}  // namespace xrpl::test

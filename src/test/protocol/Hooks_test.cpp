

#include <test/jtx/Env.h>  // IWYU pragma: keep

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STObject.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace xrpl {

class Hooks_test : public beast::unit_test::Suite
{
    /**
     * This unit test was requested here:
     * https://github.com/XRPLF/rippled/pull/4089#issuecomment-1050274539
     * These are tests that exercise facilities that are reserved for when Hooks
     * is merged in the future.
     **/

    void
    testHookFields()
    {
        testcase("Test Hooks fields");

        using namespace test::jtx;

        std::vector<std::reference_wrapper<SField const>> const fieldsToTest = {
            sfHookResult,
            sfHookStateChangeCount,
            sfHookEmitCount,
            sfHookExecutionIndex,
            sfHookApiVersion,
            sfHookStateCount,
            sfEmitGeneration,
            sfHookOn,
            sfHookInstructionCount,
            sfEmitBurden,
            sfHookReturnCode,
            sfReferenceCount,
            sfEmitParentTxnID,
            sfEmitNonce,
            sfEmitHookHash,
            sfHookStateKey,
            sfHookHash,
            sfHookNamespace,
            sfHookSetTxnID,
            sfHookStateData,
            sfHookReturnString,
            sfHookParameterName,
            sfHookParameterValue,
            sfEmitCallback,
            sfHookAccount,
            sfEmittedTxn,
            sfHook,
            sfHookDefinition,
            sfHookParameter,
            sfHookGrant,
            sfEmitDetails,
            sfHookExecutions,
            sfHookExecution,
            sfHookParameters,
            sfHooks,
            sfHookGrants};

        for (auto const& rf : fieldsToTest)
        {
            SField const& f = rf.get();

            STObject dummy{kSfGeneric};

            BEAST_EXPECT(!dummy.isFieldPresent(f));

            switch (f.fieldType)
            {
                case STI_UINT8: {
                    dummy.setFieldU8(f, 0);
                    BEAST_EXPECT(dummy.getFieldU8(f) == 0);

                    dummy.setFieldU8(f, 255);
                    BEAST_EXPECT(dummy.getFieldU8(f) == 255);

                    BEAST_EXPECT(dummy.isFieldPresent(f));
                    break;
                }

                case STI_UINT16: {
                    dummy.setFieldU16(f, 0);
                    BEAST_EXPECT(dummy.getFieldU16(f) == 0);

                    dummy.setFieldU16(f, 0xFFFFU);
                    BEAST_EXPECT(dummy.getFieldU16(f) == 0xFFFFU);

                    BEAST_EXPECT(dummy.isFieldPresent(f));
                    break;
                }

                case STI_UINT32: {
                    dummy.setFieldU32(f, 0);
                    BEAST_EXPECT(dummy.getFieldU32(f) == 0);

                    dummy.setFieldU32(f, 0xFFFFFFFFU);
                    BEAST_EXPECT(dummy.getFieldU32(f) == 0xFFFFFFFFU);

                    BEAST_EXPECT(dummy.isFieldPresent(f));
                    break;
                }

                case STI_UINT64: {
                    dummy.setFieldU64(f, 0);
                    BEAST_EXPECT(dummy.getFieldU64(f) == 0);

                    dummy.setFieldU64(f, 0xFFFFFFFFFFFFFFFFU);
                    BEAST_EXPECT(dummy.getFieldU64(f) == 0xFFFFFFFFFFFFFFFFU);

                    BEAST_EXPECT(dummy.isFieldPresent(f));
                    break;
                }

                case STI_UINT256: {
                    uint256 const u = uint256::fromVoid(
                        "DEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBE"
                        "EFDEADBEEF");
                    dummy.setFieldH256(f, u);
                    BEAST_EXPECT(dummy.getFieldH256(f) == u);
                    BEAST_EXPECT(dummy.isFieldPresent(f));
                    break;
                }

                case STI_VL: {
                    std::vector<uint8_t> const v{1, 2, 3};
                    dummy.setFieldVL(f, v);
                    BEAST_EXPECT(dummy.getFieldVL(f) == v);
                    BEAST_EXPECT(dummy.isFieldPresent(f));
                    break;
                }

                case STI_ACCOUNT: {
                    // NOLINTBEGIN(bugprone-unchecked-optional-access)
                    AccountID const id =
                        *parseBase58<AccountID>("rwfSjJNK2YQuN64bSWn7T2eY9FJAyAPYJT");
                    // NOLINTEND(bugprone-unchecked-optional-access)
                    dummy.setAccountID(f, id);
                    BEAST_EXPECT(dummy.getAccountID(f) == id);
                    BEAST_EXPECT(dummy.isFieldPresent(f));
                    break;
                }

                case STI_OBJECT: {
                    dummy.emplaceBack(STObject{f});
                    BEAST_EXPECT(dummy.getField(f).getFName() == f);
                    BEAST_EXPECT(dummy.isFieldPresent(f));
                    break;
                }

                case STI_ARRAY: {
                    STArray dummy2{f, 2};
                    dummy2.pushBack(STObject{kSfGeneric});
                    dummy2.pushBack(STObject{kSfGeneric});
                    dummy.setFieldArray(f, dummy2);
                    BEAST_EXPECT(dummy.getFieldArray(f) == dummy2);
                    BEAST_EXPECT(dummy.isFieldPresent(f));
                    break;
                }

                default:
                    BEAST_EXPECT(false);
            }
        }
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        testHookFields();
    }
};

BEAST_DEFINE_TESTSUITE(Hooks, protocol, xrpl);

}  // namespace xrpl

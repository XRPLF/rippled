//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2012-2017 Ripple Labs Inc.

  Permission to use, copy, modify, and/or distribute this software for any
  purpose  with  or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
  MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/protocol/Feature.h>
#include <ripple/protocol/jss.h>
#include <functional>
#include <test/jtx.h>
#include <vector>

namespace ripple {

class Hooks_test : public beast::unit_test::suite
{
    /**
     * This unit test was requested here:
     * https://github.com/ripple/rippled/pull/4089#issuecomment-1050274539
     * These are tests that exercise facilities that are reserved for when Hooks
     * is merged in the future.
     **/

    void
    testHookFields()
    {
        testcase("Test Hooks fields");

        using namespace test::jtx;

        std::vector<std::reference_wrapper<SField const>> fields_to_test = {
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

        for (auto const& rf : fields_to_test)
        {
            SField const& f = rf.get();

            STObject dummy{sfGeneric};

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
                    uint256 u = uint256::fromVoid(
                        "DEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBEEFDEADBE"
                        "EFDEADBEEF");
                    dummy.setFieldH256(f, u);
                    BEAST_EXPECT(dummy.getFieldH256(f) == u);
                    BEAST_EXPECT(dummy.isFieldPresent(f));
                    break;
                }

                case STI_VL: {
                    std::vector<uint8_t> v{1, 2, 3};
                    dummy.setFieldVL(f, v);
                    BEAST_EXPECT(dummy.getFieldVL(f) == v);
                    BEAST_EXPECT(dummy.isFieldPresent(f));
                    break;
                }

                case STI_ACCOUNT: {
                    AccountID id = *parseBase58<AccountID>(
                        "rwfSjJNK2YQuN64bSWn7T2eY9FJAyAPYJT");
                    dummy.setAccountID(f, id);
                    BEAST_EXPECT(dummy.getAccountID(f) == id);
                    BEAST_EXPECT(dummy.isFieldPresent(f));
                    break;
                }

                case STI_OBJECT: {
                    dummy.emplace_back(STObject{f});
                    BEAST_EXPECT(dummy.getField(f).getFName() == f);
                    BEAST_EXPECT(dummy.isFieldPresent(f));
                    break;
                }

                case STI_ARRAY: {
                    STArray dummy2{f, 2};
                    dummy2.push_back(STObject{sfGeneric});
                    dummy2.push_back(STObject{sfGeneric});
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

BEAST_DEFINE_TESTSUITE(Hooks, protocol, ripple);

}  // namespace ripple

//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#include <test/jtx.h>

#include <xrpld/app/misc/ContractHostFuncImpl.h>

namespace ripple {
namespace test {

static ApplyContext
createApplyContext(
    test::jtx::Env& env,
    OpenView& ov,
    STTx const& tx = STTx(ttCONTRACT_CALL, [](STObject&) {}))
{
    ApplyContext ac{
        env.app(),
        ov,
        tx,
        tesSUCCESS,
        env.current()->fees().base,
        tapNONE,
        env.journal};
    return ac;
}

struct ContractHostFuncImpl_test : public beast::unit_test::suite
{
    void
    testBuildTxn()
    {
        testcase("buildTxn");
        using namespace test::jtx;

        Account const contract("contract");
        Account const otxn("otxn");

        Env env{*this};
        OpenView ov{*env.current()};

        STTx const stx = STTx(ttCONTRACT_CALL, [&](auto& obj) {
            obj.setAccountID(sfAccount, env.master.id());
        });
        ApplyContext ac = createApplyContext(env, ov, stx);
    
        ripple::ContractDataMap dataMap;
        ripple::ContractEventMap eventMap;
        std::vector<ripple::ParameterValueVec> instanceParameters;
        std::vector<ripple::ParameterValueVec> functionParameters;
        uint256 contractHash = uint256{1};

        auto const k = keylet::contract(contractHash, 0);
        ContractContext contractCtx = {
            .applyCtx = ac,
            .instanceParameters = instanceParameters,
            .functionParameters = functionParameters,
            .expected_etxn_count = 0,
            .generation = 0,
            .burden = 0,
            .result =
                {
                    .contractHash = contractHash,
                    .contractKeylet = k,
                    .contractSourceKeylet = k,
                    .contractAccountKeylet = k,
                    .contractAccount = contract.id(),
                    .nextSequence = 0,
                    .otxnAccount = otxn.id(),
                    .exitType = ripple::ExitType::ROLLBACK,
                    .exitCode = -1,
                    .dataMap = dataMap,
                    .eventMap = eventMap,
                    .changedDataCount = 0,
                },
        };

        ContractHostFunctionsImpl cfs(contractCtx);

        BEAST_EXPECT(true == true);

        auto const result = cfs.buildTxn(0);
        std::cout << "buildTxn result: " << result.value() << std::endl;


        auto const result1 = cfs.addTxnField(0, sfAccount, Slice(otxn.id().data(), otxn.id().size()));
        std::cout << "addTxnField result: " << result1.value() << std::endl;
        // if (BEAST_EXPECT(result.has_value()))
        //     BEAST_EXPECT(result.value() == env.current()->info().seq);
    }

    void
    run() override
    {
        testBuildTxn();
    }
};

BEAST_DEFINE_TESTSUITE(ContractHostFuncImpl, app, ripple);

}  // namespace test
}  // namespace ripple
